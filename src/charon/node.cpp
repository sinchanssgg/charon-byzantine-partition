#include "node.h"
#include <algorithm>
#include <queue>
#include <climits>
#include <functional>
#include <random>
#include <set>

namespace charon {

// ============================================================================
//  Constructor
// ============================================================================
CharonNode::CharonNode(NodeId id, int t, int c, int delta_max)
    : id_(id), t_(t), c_(c), delta_max_(delta_max)
{
    F_ = 4 * (t + c) + 2 + delta_max;
    W_ = 4 * (t + c) + 2;
}

// ============================================================================
//  UpdateBeacon
//  Only stores beacon if it is strictly newer than what we already have.
//  This defeats stale-beacon replay (B3): a Byzantine node cannot roll back
//  a correct node's beacon to an older round.
// ============================================================================
void CharonNode::updateBeacon(NodeId x, const Beacon& b) {
    auto it = B_.find(x);
    if (it == B_.end() || b.round > it->second.round)
        B_[x] = b;
}

// ============================================================================
//  Fresh
//  A token is fresh iff both endpoint beacons are within F rounds of now.
// ============================================================================
bool CharonNode::fresh(const Token& tok, Round r) const {
    return (r - tok.beacon_u.round <= (Round)F_) &&
           (r - tok.beacon_v.round <= (Round)F_);
}

// ============================================================================
//  Accept
//  Five-part validity check as specified in the paper (Algorithm 2).
// ============================================================================
bool CharonNode::accept(const Token& tok, Round r) const {
    // (a) Neighborhood proof must be valid — cannot be forged if at least
    //     one endpoint is correct
    if (!verifyNeighProof(tok.u, tok.v, tok.pi_uv)) return false;

    // (b) Both endpoint beacon signatures must be valid
    if (!tok.beacon_u.valid()) return false;
    if (!tok.beacon_v.valid()) return false;

    // (c) Both beacons must be within the freshness horizon
    if (r - tok.beacon_u.round > (Round)F_) return false;
    if (r - tok.beacon_v.round > (Round)F_) return false;

    // (d) Relay chain length must equal declared depth, all sigs distinct
    if ((int)tok.chain.size() != tok.depth) return false;
    std::set<std::string> seen;
    for (auto& s : tok.chain) {
        if (seen.count(s)) return false;
        seen.insert(s);
    }

    // (e) Token depth must not exceed local membership estimate
    int delta_i = (int)B_.size();
    if (tok.depth > delta_i) return false;

    return true;
}

// ============================================================================
//  Max-Flow: Push-Relabel (Dinic's algorithm) on node-split graph
//
//  Each original node v becomes:
//      v_in  = 2 * index(v)
//      v_out = 2 * index(v) + 1
//  Internal edge v_in → v_out has capacity 1 (except source/sink: INF).
//  Each original undirected edge (u,v) becomes:
//      u_out → v_in  (capacity INF)
//      v_out → u_in  (capacity INF)
//
//  The minimum s-t max-flow over all sink choices equals vertex connectivity.
// ============================================================================
struct MaxFlowGraph {
    struct Edge {
        int to, rev, cap;
    };

    int N;
    std::vector<std::vector<Edge>> graph;
    std::vector<int> level, iter;

    explicit MaxFlowGraph(int n)
        : N(n), graph(n), level(n), iter(n) {}

    void addEdge(int from, int to, int cap) {
        graph[from].push_back({to,   (int)graph[to].size(),   cap});
        graph[to  ].push_back({from, (int)graph[from].size()-1, 0});
    }

    // BFS to build level graph
    bool bfs(int s, int t) {
        std::fill(level.begin(), level.end(), -1);
        std::queue<int> q;
        level[s] = 0;
        q.push(s);
        while (!q.empty()) {
            int v = q.front(); q.pop();
            for (auto& e : graph[v])
                if (e.cap > 0 && level[e.to] < 0) {
                    level[e.to] = level[v] + 1;
                    q.push(e.to);
                }
        }
        return level[t] >= 0;
    }

    // DFS blocking flow
    int dfs(int v, int t, int f) {
        if (v == t) return f;
        for (int& i = iter[v]; i < (int)graph[v].size(); i++) {
            Edge& e = graph[v][i];
            if (e.cap > 0 && level[v] < level[e.to]) {
                int d = dfs(e.to, t, std::min(f, e.cap));
                if (d > 0) {
                    e.cap -= d;
                    graph[e.to][e.rev].cap += d;
                    return d;
                }
            }
        }
        return 0;
    }

    // Dinic's max-flow — O(V^2 * E)
    int maxflow(int s, int t) {
        int flow = 0;
        while (bfs(s, t)) {
            std::fill(iter.begin(), iter.end(), 0);
            int d;
            while ((d = dfs(s, t, INT_MAX)) > 0)
                flow += d;
        }
        return flow;
    }
};

// ============================================================================
//  vertexConnectivity
//
//  Three-tier strategy from Appendix 6.1 of the paper:
//
//  Tier 0 — BFS: if graph is disconnected, return 0 immediately.
//  Tier 1b — Tarjan articulation-point check: if κ = 1, return 1 immediately.
//  Tier 2/3 — Node-splitting max-flow (Dinic's):
//      • n ≤ 1000  : exact, all sink pairs
//      • n ≤ 5000  : exact, all sink pairs
//      • n > 5000  : sample 200 random sinks (Tier 3)
//  Early termination: stop as soon as minKappa ≤ t + c (already partitionable).
// ============================================================================
int CharonNode::vertexConnectivity(
    const std::set<NodeId>& nodes,
    const std::map<std::pair<NodeId,NodeId>, bool>& edges) const
{
    int N = (int)nodes.size();
    if (N <= 1) return N;

    // ── Tier 0: BFS disconnection check (O(V + E)) ───────────────────────
    {
        auto reachable = reach(edges, nodes);
        if ((int)reachable.size() < N) return 0;
    }

    // ── Build index maps once ─────────────────────────────────────────────
    std::map<NodeId, int> idx;
    std::vector<NodeId>   nodeVec(nodes.begin(), nodes.end());
    for (int i = 0; i < N; i++) idx[nodeVec[i]] = i;

    // ── Build adjacency list for Tarjan ───────────────────────────────────
    std::vector<std::vector<int>> adj(N);
    for (auto& [e, _] : edges) {
        if (!idx.count(e.first) || !idx.count(e.second)) continue;
        int a = idx[e.first], b = idx[e.second];
        adj[a].push_back(b);
        adj[b].push_back(a);
    }

    // ── Tier 1b: Tarjan articulation-point check (O(V + E)) ──────────────
    // If any articulation point exists, κ = 1 → immediately Partitionable.
    {
        std::vector<int>  disc(N, -1), low(N, 0), parent(N, -1);
        std::vector<bool> isAP(N, false);
        int timer = 0;

        std::function<void(int)> dfs = [&](int u) {
            int children = 0;
            disc[u] = low[u] = timer++;
            for (int v : adj[u]) {
                if (disc[v] == -1) {
                    children++;
                    parent[v] = u;
                    dfs(v);
                    low[u] = std::min(low[u], low[v]);
                    // Root with multiple children
                    if (parent[u] == -1 && children > 1)
                        isAP[u] = true;
                    // Non-root: subtree has no back-edge above u
                    if (parent[u] != -1 && low[v] >= disc[u])
                        isAP[u] = true;
                } else if (v != parent[u]) {
                    low[u] = std::min(low[u], disc[v]);
                }
            }
        };

        dfs(0);
        for (int i = 0; i < N; i++)
            if (isAP[i]) return 1;
    }

    // ── Tier 2 / 3: Node-splitting max-flow ──────────────────────────────
    const int INF = 1e9;

    // Choose how many sinks to test based on graph size
    int maxSinks = (N <= 5000) ? N - 1   // Tiers 1 & 2: exact
                               : 200;    // Tier 3: random sample

    std::vector<int> sinks;
    sinks.reserve(N - 1);
    for (int i = 1; i < N; i++) sinks.push_back(i);

    if ((int)sinks.size() > maxSinks) {
        std::mt19937 rng(42);
        std::shuffle(sinks.begin(), sinks.end(), rng);
        sinks.resize(maxSinks);
    }

    int minKappa = INT_MAX;
    const int source = 0; // fixed source node index

    for (int t_idx : sinks) {
        // Build node-split graph: 2 * N nodes
        MaxFlowGraph fg(2 * N);

        // Internal edges: v_in → v_out
        for (int i = 0; i < N; i++) {
            // Source and sink nodes get infinite internal capacity —
            // they cannot themselves be removed to disconnect the graph
            int cap = (i == source || i == t_idx) ? INF : 1;
            fg.addEdge(2*i, 2*i+1, cap);
        }

        // Original undirected edges: u_out → v_in and v_out → u_in
        for (auto& [e, _] : edges) {
            if (!idx.count(e.first) || !idx.count(e.second)) continue;
            int a = idx[e.first], b = idx[e.second];
            fg.addEdge(2*a+1, 2*b,   INF);
            fg.addEdge(2*b+1, 2*a,   INF);
        }

        int flow = fg.maxflow(2*source+1, 2*t_idx);
        minKappa = std::min(minKappa, flow);

        // Early termination: already know network is partitionable
        if (minKappa <= t_ + c_) return minKappa;
    }

    return (minKappa == INT_MAX) ? N - 1 : minKappa;
}

// ============================================================================
//  BFS reachability from id_ within the anchor graph H
// ============================================================================
std::set<NodeId> CharonNode::reach(
    const std::map<std::pair<NodeId,NodeId>, bool>& edges,
    const std::set<NodeId>& nodes) const
{
    std::set<NodeId> visited;
    if (!nodes.count(id_)) return visited;

    std::queue<NodeId> q;
    q.push(id_);
    visited.insert(id_);

    while (!q.empty()) {
        NodeId cur = q.front(); q.pop();
        for (auto& [e, _] : edges) {
            NodeId nb = (NodeId)-1;
            if (e.first  == cur && nodes.count(e.second)) nb = e.second;
            if (e.second == cur && nodes.count(e.first))  nb = e.first;
            if (nb != (NodeId)-1 && !visited.count(nb)) {
                visited.insert(nb);
                q.push(nb);
            }
        }
    }
    return visited;
}

// ============================================================================
//  Assess
//  Builds local graph H from anchor set A and fresh tokens, then checks
//  whether κ(H) > t + c and all known members are reachable from id_.
// ============================================================================
AssessResult CharonNode::assess() const {
    Round r = currentRound_;

    // Anchor set A: all nodes with a fresh beacon
    std::set<NodeId> A;
    for (auto& [uid, b] : B_) A.insert(uid);

    // Edge set: fresh tokens whose both endpoints are in A
    std::map<std::pair<NodeId,NodeId>, bool> edges;
    for (const Token& tok : T_) {
        if (!fresh(tok, r)) continue;
        if (A.count(tok.u) && A.count(tok.v)) {
            NodeId lo = std::min(tok.u, tok.v);
            NodeId hi = std::max(tok.u, tok.v);
            edges[{lo, hi}] = true;
        }
    }

    int  kappa       = vertexConnectivity(A, edges);
    auto reachable   = reach(edges, A);
    bool allReachable = (reachable.size() == A.size());

    if (kappa > t_ + c_ && allReachable)
        return {Decision::NOT_PARTITIONABLE, false};

    return {Decision::PARTITIONABLE, !allReachable};
}

// ============================================================================
//  runRound  —  main per-round entry point
//
//  Implements the six logical steps of Algorithm 1:
//    1. Broadcast presence beacon
//    2. Process incoming messages (beacons + tokens)
//    3. Assemble self-tokens for incident edges
//    4. Expire stale beacons and tokens
//    5. Relay tokens (bounded flood)
//    6. Assess partition status
//
//  Byzantine nodes short-circuit here based on their assigned behavior.
// ============================================================================
std::vector<std::pair<NodeId, CharonMsg>>
CharonNode::runRound(
    Round r,
    const std::vector<std::pair<NodeId, NeighProof>>& neighbors,
    const std::vector<CharonMsg>& inbox)
{
    currentRound_ = r;

    // Refresh neighbor maps
    neighborProofs_.clear();
    std::vector<NodeId> neighborIds;
    for (auto& [nid, proof] : neighbors) {
        neighborProofs_[nid] = proof;
        neighborIds.push_back(nid);
    }

    std::vector<std::pair<NodeId, CharonMsg>> outbox;

    // ── Byzantine short-circuit ───────────────────────────────────────────
    if (byzantine_) {
        switch (byzBehavior_) {

        // B1: Silent — transmit nothing whatsoever
        case ByzBehavior::SILENT:
            return outbox;

        // B3: Stale-beacon replay
        //   Broadcasts its own beacon with a fabricated old round number,
        //   and replays stale beacons extracted from stored tokens.
        //   The monotonicity guard in UpdateBeacon defeats this for correct
        //   nodes that have already seen a fresher beacon; lost nodes nearby
        //   may be deceived, inflating their anchor sets with phantom entries.
        case ByzBehavior::STALE_REPLAY: {
            Round fakeRound = (r > (Round)F_) ? r - F_ : 0;
            Beacon staleBeacon{
                id_,
                fakeRound,
                sign(id_, concat(id_, fakeRound))
            };
            CharonMsg bm;
            bm.kind   = MsgKind::BEACON;
            bm.beacon = staleBeacon;
            for (NodeId nb : neighborIds)
                outbox.push_back({nb, bm});

            // Also replay any stale beacons we happen to have stored
            for (auto& [uid, b] : B_) {
                if (b.round < r && uid != id_) {
                    CharonMsg rm;
                    rm.kind   = MsgKind::BEACON;
                    rm.beacon = b;
                    for (NodeId nb : neighborIds)
                        outbox.push_back({nb, rm});
                }
            }
            return outbox;
        }

        // B4: Token flooding
        //   Generates syntactically well-formed but semantically invalid
        //   tokens (bad neighborhood proof) and floods all neighbors.
        //   Accept condition (a) rejects them at every correct receiver,
        //   but the flooding consumes bandwidth and processing time.
        //   First sends a valid beacon so we remain in neighbors' Bi.
        case ByzBehavior::FLOOD: {
            // Valid beacon so we stay visible
            Beacon myBeacon{id_, r, sign(id_, concat(id_, r))};
            CharonMsg bm;
            bm.kind   = MsgKind::BEACON;
            bm.beacon = myBeacon;
            for (NodeId nb : neighborIds)
                outbox.push_back({nb, bm});

            // Flood garbage tokens
            static std::mt19937 floodRng(std::random_device{}());
            const int floodCount = 10;
            for (int f = 0; f < floodCount; f++) {
                NodeId fakeU = floodRng() % 10000;
                NodeId fakeV = floodRng() % 10000;
                if (fakeU == fakeV) continue;

                Token tok;
                tok.u        = fakeU;
                tok.v        = fakeV;
                tok.pi_uv    = "INVALID_PROOF"; // fails Accept (a)
                tok.beacon_u = myBeacon;
                tok.beacon_v = myBeacon;
                tok.depth    = 1;
                tok.chain    = { sign(id_, concat(fakeU, fakeV, 1, r)) };

                CharonMsg tm;
                tm.kind  = MsgKind::TOKEN;
                tm.token = tok;
                for (NodeId nb : neighborIds)
                    outbox.push_back({nb, tm});
            }
            return outbox;
        }

        // B2: Selective forwarding — falls through to normal execution,
        //     drops ~50 % of relayed tokens in the relay step below.
        case ByzBehavior::SELECTIVE_FWD:
            break;
        }
    }

    // ── Step 1: Broadcast own presence beacon ────────────────────────────
    Beacon myBeacon{id_, r, sign(id_, concat(id_, r))};
    {
        CharonMsg bm;
        bm.kind   = MsgKind::BEACON;
        bm.beacon = myBeacon;
        for (NodeId nb : neighborIds)
            outbox.push_back({nb, bm});
    }

    // ── Step 2: Process incoming messages ────────────────────────────────
    for (const auto& msg : inbox) {
        if (msg.kind == MsgKind::BEACON) {
            const Beacon& b = msg.beacon;
            // Validate signature before storing
            if (b.valid())
                updateBeacon(b.sender, b);

        } else { // MsgKind::TOKEN
            const Token& tok = msg.token;
            if (accept(tok, r)) {
                // Beacon extraction: liveness evidence propagates with tokens
                updateBeacon(tok.u, tok.beacon_u);
                updateBeacon(tok.v, tok.beacon_v);

                // Store token only if we don't already have one for this edge
                bool have = false;
                for (auto& t : T_)
                    if (t.u == tok.u && t.v == tok.v) { have = true; break; }
                if (!have)
                    T_.push_back(tok);
            }
        }
    }

    // ── Step 3: Assemble self-tokens for each incident edge ───────────────
    // Continuously refreshes edge certificates — essential for recovering
    // from churn and corrupted initial states.
    for (NodeId nb : neighborIds) {
        auto bIt = B_.find(nb);
        if (bIt == B_.end()) continue;
        if (r - bIt->second.round > (Round)F_) continue;

        // Skip if we already have a fresh token for edge (id_, nb)
        bool have = false;
        for (auto& t : T_)
            if ((t.u == id_ && t.v == nb) ||
                (t.u == nb  && t.v == id_)) { have = true; break; }
        if (have) continue;

        Token tok;
        tok.u        = id_;
        tok.v        = nb;
        tok.pi_uv    = neighborProofs_[nb];
        tok.beacon_u = myBeacon;
        tok.beacon_v = bIt->second;
        tok.depth    = 1;
        tok.chain    = { sign(id_, concat(id_, nb, 1, r)) };
        T_.push_back(tok);
    }

    // ── Step 4: Expire stale beacons and tokens ───────────────────────────
    // This is the self-stabilizing core: any corrupted initial state carries
    // old timestamps and expires within F rounds, regardless of content.
    for (auto it = B_.begin(); it != B_.end(); ) {
        if (r - it->second.round > (Round)F_)
            it = B_.erase(it);
        else
            ++it;
    }
    T_.erase(
        std::remove_if(T_.begin(), T_.end(),
            [&](const Token& tok){ return !fresh(tok, r); }),
        T_.end());

    // ── Step 5: Relay tokens (bounded flood) ─────────────────────────────
    // δ_i = current anchor set size — used as local network size estimate.
    // A token is relayed only while depth < δ_i, bounding the flood to the
    // perceived stable subgraph diameter.
    static std::mt19937 relayRng(std::random_device{}());
    int delta_i = (int)B_.size();

    for (const Token& tok : T_) {
        if (tok.depth >= delta_i) continue;

        for (NodeId nb : neighborIds) {
            // B2: Selective forwarding — drop approximately half of tokens.
            // Byzantine nodes do this to slow token propagation and delay
            // convergence without triggering Accept rejection (they still
            // send valid beacons, staying in neighbors' Bi).
            if (byzantine_ &&
                byzBehavior_ == ByzBehavior::SELECTIVE_FWD &&
                relayRng() % 2 == 0)
                continue;

            Token relayed = tok;
            relayed.depth++;
            relayed.chain.push_back(
                sign(id_, concat(tok.u, tok.v, relayed.depth, r)));

            CharonMsg m;
            m.kind  = MsgKind::TOKEN;
            m.token = relayed;
            outbox.push_back({nb, m});
        }
    }

    // ── Step 6: Assess partition status ───────────────────────────────────
    auto res  = assess();
    decision_ = res.decision;

    return outbox;
}

} // namespace charon
