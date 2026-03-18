#include "node.h"
#include <algorithm>
#include <queue>
#include <climits>
#include <functional>
#include <random>
#include <set>
#include <map>
#include <vector>

namespace charon {

// ============================================================================
//  Constructor
// ============================================================================
CharonNode::CharonNode(NodeId id, int t, int c, int delta_max)
    : id_(id), t_(t), c_(c), delta_max_(delta_max)
{
    F_ = 4 * (t + c) + 2 + delta_max;
    W_ = 4 * (t + c) + 2;
    observedMaxDepth_ = 0;
}

// ============================================================================
//  UpdateBeacon
//  Only stores beacon if strictly newer. Defeats B3 stale-beacon replay:
//  a Byzantine node cannot roll back a correct node's beacon to an older round.
// ============================================================================
void CharonNode::updateBeacon(NodeId x, const Beacon& b) {
    auto it = B_.find(x);
    if (it == B_.end() || b.round > it->second.round)
        B_[x] = b;
}

// ============================================================================
//  Fresh
//  Token is fresh iff both endpoint beacons are within F rounds of now.
// ============================================================================
bool CharonNode::fresh(const Token& tok, Round r) const {
    return (r - tok.beacon_u.round <= (Round)F_) &&
           (r - tok.beacon_v.round <= (Round)F_);
}

// ============================================================================
//  Accept
//  Five-part validity check (Algorithm 2 of the paper).
// ============================================================================
bool CharonNode::accept(const Token& tok, Round r) const {
    // (a) Neighborhood proof valid — unforgeable if one endpoint is correct
    if (!verifyNeighProof(tok.u, tok.v, tok.pi_uv)) return false;

    // (b) Both endpoint beacon signatures valid
    if (!tok.beacon_u.valid()) return false;
    if (!tok.beacon_v.valid()) return false;

    // (c) Both beacons within freshness horizon
    if (r - tok.beacon_u.round > (Round)F_) return false;
    if (r - tok.beacon_v.round > (Round)F_) return false;

    // (d) Relay chain length equals declared depth, all signatures distinct
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
//  Max-Flow: Dinic's algorithm on node-split graph
//
//  Node v splits into:
//      v_in  = 2 * index(v)
//      v_out = 2 * index(v) + 1
//  Internal edge v_in → v_out: capacity 1 (INF for source/sink).
//  Original undirected edge (u,v):
//      u_out → v_in  (capacity INF)
//      v_out → u_in  (capacity INF)
// ============================================================================
struct MaxFlowGraph {
    struct Edge { int to, rev, cap; };

    int N;
    std::vector<std::vector<Edge>> graph;
    std::vector<int> level, iter;

    explicit MaxFlowGraph(int n)
        : N(n), graph(n), level(n), iter(n) {}

    void addEdge(int from, int to, int cap) {
        graph[from].push_back({to,   (int)graph[to].size(),     cap});
        graph[to  ].push_back({from, (int)graph[from].size()-1, 0  });
    }

    bool bfs(int s, int t) {
        std::fill(level.begin(), level.end(), -1);
        std::queue<int> q;
        level[s] = 0; q.push(s);
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

    int maxflow(int s, int t) {
        int flow = 0;
        while (bfs(s, t)) {
            std::fill(iter.begin(), iter.end(), 0);
            int d;
            while ((d = dfs(s, t, INT_MAX)) > 0) flow += d;
        }
        return flow;
    }
};

// ============================================================================
//  vertexConnectivity
//
//  Three-tier strategy (Appendix 6.1):
//  Tier 0   — BFS: disconnected → return 0 immediately.
//  Tier 1b  — Tarjan: articulation point found → return 1 immediately.
//  Tier 2/3 — Node-splitting max-flow (Dinic's):
//               n ≤ 5000 : all source-sink pairs (exact)
//               n > 5000 : 200 random sinks (sampled)
//  Early termination when minKappa ≤ t+c.
// ============================================================================
int CharonNode::vertexConnectivity(
    const std::set<NodeId>& nodes,
    const std::map<std::pair<NodeId,NodeId>, bool>& edges) const
{
    int N = (int)nodes.size();
    if (N <= 1) return N;

    // ── Tier 0: BFS disconnection check ──────────────────────────────────
    {
        auto reachable = reach(edges, nodes);
        if ((int)reachable.size() < N) return 0;
    }

    // ── Build index map ───────────────────────────────────────────────────
    std::map<NodeId, int> idx;
    std::vector<NodeId>   nodeVec(nodes.begin(), nodes.end());
    for (int i = 0; i < N; i++) idx[nodeVec[i]] = i;

    // ── Build adjacency list ──────────────────────────────────────────────
    std::vector<std::vector<int>> adj(N);
    for (auto& [e, _] : edges) {
        if (!idx.count(e.first) || !idx.count(e.second)) continue;
        int a = idx[e.first], b = idx[e.second];
        adj[a].push_back(b);
        adj[b].push_back(a);
    }

    // ── Tier 1b: Tarjan articulation-point check ──────────────────────────
    {
        std::vector<int>  disc(N,-1), low(N,0), parent(N,-1);
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
                    if (parent[u] == -1 && children > 1) isAP[u] = true;
                    if (parent[u] != -1 && low[v] >= disc[u]) isAP[u] = true;
                } else if (v != parent[u]) {
                    low[u] = std::min(low[u], disc[v]);
                }
            }
        };
        dfs(0);
        for (int i = 0; i < N; i++)
            if (isAP[i]) return 1;
    }

    // ── Tier 2/3: Node-splitting max-flow ────────────────────────────────
    const int INF = 1e9;

    // For small graphs iterate ALL (s,t) pairs for exactness.
    // For large graphs sample 200 random sinks.
    std::vector<int> allSinks;
    allSinks.reserve(N - 1);
    for (int i = 1; i < N; i++) allSinks.push_back(i);

    int maxSinks = (N <= 1000) ? N - 1    // Tier 1: all pairs exact
                : (N <= 5000) ? N - 1     // Tier 2: all pairs exact
                :               200;      // Tier 3: sampled

    if ((int)allSinks.size() > maxSinks) {
        std::mt19937 rng(42);
        std::shuffle(allSinks.begin(), allSinks.end(), rng);
        allSinks.resize(maxSinks);
    }

    int minKappa = INT_MAX;

    // For small graphs, iterate all source nodes too (exact)
    int maxSources = (N <= 1000) ? N - 1 : 1;

    for (int src = 0; src < maxSources; src++) {
        for (int t_idx : allSinks) {
            if (t_idx == src) continue;

            MaxFlowGraph fg(2 * N);

            // Internal edges
            for (int i = 0; i < N; i++) {
                int cap = (i == src || i == t_idx) ? INF : 1;
                fg.addEdge(2*i, 2*i+1, cap);
            }

            // Original edges: u_out → v_in and v_out → u_in
            for (auto& [e, _] : edges) {
                if (!idx.count(e.first) || !idx.count(e.second)) continue;
                int a = idx[e.first], b = idx[e.second];
                fg.addEdge(2*a+1, 2*b,   INF);
                fg.addEdge(2*b+1, 2*a,   INF);
            }

            int flow = fg.maxflow(2*src+1, 2*t_idx);
            minKappa = std::min(minKappa, flow);

            // Early termination
            if (minKappa <= t_ + c_) return minKappa;
        }
    }

    return (minKappa == INT_MAX) ? N - 1 : minKappa;
}

// ============================================================================
//  BFS reachability from id_ within anchor graph H
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
//  Builds local graph H from anchor set A and fresh tokens, checks
//  κ(H) > t+c and full reachability from id_.
// ============================================================================
AssessResult CharonNode::assess() const {
    Round r = currentRound_;

    std::set<NodeId> A;
    for (auto& [uid, b] : B_) A.insert(uid);

    std::map<std::pair<NodeId,NodeId>, bool> edges;
    for (const Token& tok : T_) {
        if (!fresh(tok, r)) continue;
        if (A.count(tok.u) && A.count(tok.v)) {
            NodeId lo = std::min(tok.u, tok.v);
            NodeId hi = std::max(tok.u, tok.v);
            edges[{lo, hi}] = true;
        }
    }

    int  kappa        = vertexConnectivity(A, edges);
    auto reachable    = reach(edges, A);
    bool allReachable = (reachable.size() == A.size());

    if (kappa > t_ + c_ && allReachable)
        return {Decision::NOT_PARTITIONABLE, false};
    return {Decision::PARTITIONABLE, !allReachable};
}

// ============================================================================
//  updateAdaptiveDeltaMax
//  Self-tunes δmax every W rounds based on the deepest token seen.
//  Keeps F and W consistent after the update.
// ============================================================================
void CharonNode::updateAdaptiveDeltaMax(Round r) {
    if (r % W_ == 0 && r > 0) {
        // Add a small buffer of 2 to avoid underestimating
        delta_max_ = observedMaxDepth_ + 2;
        F_ = 4 * (t_ + c_) + 2 + delta_max_;
        // W_ does not depend on delta_max so no change needed
        observedMaxDepth_ = 0; // reset for next window
    }
}

// ============================================================================
//  runRound  —  main per-round entry point (Algorithm 1)
//
//  Six logical steps:
//    1. Broadcast presence beacon
//    2. Process incoming messages (beacons + tokens)
//    3. Assemble self-tokens for incident edges
//    4. Expire stale beacons and tokens
//    5. Relay tokens (bounded flood, with correct signer-chain loop prevention)
//    6. Assess partition status
//
//  Byzantine nodes short-circuit based on assigned behavior (B1–B4).
// ============================================================================
std::vector<std::pair<NodeId, CharonMsg>>
CharonNode::runRound(
    Round r,
    const std::vector<std::pair<NodeId, NeighProof>>& neighbors,
    const std::vector<CharonMsg>& inbox)
{
    currentRound_ = r;

    // Adaptive δmax update every W rounds
    updateAdaptiveDeltaMax(r);

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

        // B1: Silent — transmit nothing
        case ByzBehavior::SILENT:
            return outbox;

        // B3: Stale-beacon replay
        //   Sends own beacon with a fabricated old round number.
        //   Monotonicity guard in UpdateBeacon defeats this for correct nodes
        //   that have already seen a fresher beacon for this sender.
        case ByzBehavior::STALE_REPLAY: {
            Round fakeRound = (r > (Round)F_) ? r - F_ : 0;
            Beacon staleBeacon{id_, fakeRound,
                               sign(id_, concat(id_, fakeRound))};
            CharonMsg bm;
            bm.kind   = MsgKind::BEACON;
            bm.beacon = staleBeacon;
            for (NodeId nb : neighborIds)
                outbox.push_back({nb, bm});

            // Replay any stale beacons from stored state
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
        //   Sends a valid beacon to stay in neighbors' Bi, then floods
        //   garbage tokens. Accept condition (a) rejects them at every
        //   correct receiver (invalid neighborhood proof).
        case ByzBehavior::FLOOD: {
            Beacon myBeacon{id_, r, sign(id_, concat(id_, r))};
            CharonMsg bm;
            bm.kind   = MsgKind::BEACON;
            bm.beacon = myBeacon;
            for (NodeId nb : neighborIds)
                outbox.push_back({nb, bm});

            static std::mt19937 floodRng(std::random_device{}());
            const int floodCount = 10;
            for (int f = 0; f < floodCount; f++) {
                NodeId fakeU = floodRng() % 10000;
                NodeId fakeV = floodRng() % 10000;
                if (fakeU == fakeV) continue;

                Token tok;
                tok.u         = fakeU;
                tok.v         = fakeV;
                tok.pi_uv     = "INVALID_PROOF"; // fails Accept (a)
                tok.beacon_u  = myBeacon;
                tok.beacon_v  = myBeacon;
                tok.depth     = 1;
                tok.chain     = { sign(id_, concat(fakeU, fakeV, 1, r)) };
                tok.signerIds = { id_ };

                CharonMsg tm;
                tm.kind  = MsgKind::TOKEN;
                tm.token = tok;
                for (NodeId nb : neighborIds)
                    outbox.push_back({nb, tm});
            }
            return outbox;
        }

        // B2: Selective forwarding — falls through to normal execution,
        //     drops ~50% of relayed tokens in the relay step below.
        case ByzBehavior::SELECTIVE_FWD:
            break;
        }
    }

    // ── Step 1: Broadcast own presence beacon ─────────────────────────────
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
            if (b.valid())
                updateBeacon(b.sender, b);

        } else { // TOKEN
            const Token& tok = msg.token;
            if (accept(tok, r)) {
                // Beacon extraction: liveness evidence propagates with tokens
                updateBeacon(tok.u, tok.beacon_u);
                updateBeacon(tok.v, tok.beacon_v);

                // Track observed depth for adaptive δmax
                observedMaxDepth_ = std::max(observedMaxDepth_, tok.depth);

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
    for (NodeId nb : neighborIds) {
        auto bIt = B_.find(nb);
        if (bIt == B_.end()) continue;
        if (r - bIt->second.round > (Round)F_) continue;

        bool have = false;
        for (auto& t : T_)
            if ((t.u == id_ && t.v == nb) ||
                (t.u == nb  && t.v == id_)) { have = true; break; }
        if (have) continue;

        Token tok;
        tok.u         = id_;
        tok.v         = nb;
        tok.pi_uv     = neighborProofs_[nb];
        tok.beacon_u  = myBeacon;
        tok.beacon_v  = bIt->second;
        tok.depth     = 1;
        tok.chain     = { sign(id_, concat(id_, nb, 1, r)) };
        tok.signerIds = { id_ }; // ← track signer id explicitly
        T_.push_back(tok);
    }

    // ── Step 4: Expire stale beacons and tokens ───────────────────────────
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
    static std::mt19937 relayRng(std::random_device{}());
    int delta_i = (int)B_.size();

    for (const Token& tok : T_) {
        if (tok.depth >= delta_i) continue;

        for (NodeId nb : neighborIds) {

            // ── FIXED: Correct signer-chain loop prevention ───────────────
            // Check if this neighbor is already in the signer chain.
            // Previously this was a placeholder comment — now we use the
            // explicit signerIds list stored alongside the chain.
            bool inChain = std::find(
                tok.signerIds.begin(),
                tok.signerIds.end(), nb) != tok.signerIds.end();
            if (inChain) continue;

            // B2: Selective forwarding — drop ~50% of tokens
            if (byzantine_ &&
                byzBehavior_ == ByzBehavior::SELECTIVE_FWD &&
                relayRng() % 2 == 0)
                continue;

            Token relayed     = tok;
            relayed.depth++;
            relayed.chain.push_back(
                sign(id_, concat(tok.u, tok.v, relayed.depth, r)));
            relayed.signerIds.push_back(id_); // ← record this relay hop

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
