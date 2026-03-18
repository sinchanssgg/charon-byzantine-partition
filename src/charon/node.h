#pragma once
#include "messages.h"
#include <map>
#include <set>
#include <vector>

namespace charon {

// ============================================================================
//  Decision  —  output of the Assess function
// ============================================================================
enum class Decision {
    NOT_PARTITIONABLE,
    PARTITIONABLE
};

// ============================================================================
//  AssessResult  —  returned by assess() each round
// ============================================================================
struct AssessResult {
    Decision decision;
    bool     confirmed;  // true if direct disconnection observed
                         // i.e. |Reach(H,i)| < |A|
};

// ============================================================================
//  CharonNode  —  core algorithm implementation (Algorithm 1 + 2)
//
//  One instance per simulated node. The OMNeT++ module wrapper
//  (CharonNodeModule) owns this object and calls runRound() once per
//  simulation round, passing in the current neighbor set and inbox,
//  and receiving back the outbox of messages to send.
// ============================================================================
class CharonNode {
public:
    // ── Construction ─────────────────────────────────────────────────────
    CharonNode(NodeId id, int t, int c, int delta_max);

    // ── Main per-round entry point ────────────────────────────────────────
    // neighbors : current neighbor set with their neighborhood proofs
    // inbox     : messages received this round from the OMNeT++ module
    // returns   : outbox — (destination NodeId, message) pairs to send
    std::vector<std::pair<NodeId, CharonMsg>>
    runRound(Round r,
             const std::vector<std::pair<NodeId, NeighProof>>& neighbors,
             const std::vector<CharonMsg>& inbox);

    // ── Accessors ─────────────────────────────────────────────────────────
    Decision lastDecision()  const { return decision_; }
    NodeId   id()            const { return id_; }
    int      getT()          const { return t_; }
    int      getC()          const { return c_; }
    int      getDeltaMax()   const { return delta_max_; }
    int      getF()          const { return F_; }
    int      getW()          const { return W_; }
    Round    currentRound()  const { return currentRound_; }
    int      anchorSetSize() const { return (int)B_.size(); }

    // ── Byzantine control (used by adversary / OMNeT++ module) ───────────
    enum class ByzBehavior {
        SILENT,           // B1: transmit nothing
        SELECTIVE_FWD,    // B2: drop ~50% of relayed tokens
        STALE_REPLAY,     // B3: broadcast fabricated old-round beacons
        FLOOD             // B4: flood neighbors with garbage tokens
    };

    bool       isByzantine()  const { return byzantine_; }
    void       setByzantine(bool b) { byzantine_ = b; }
    ByzBehavior byzBehavior() const { return byzBehavior_; }
    void       setByzBehavior(ByzBehavior b) { byzBehavior_ = b; }

private:
    // ── Node identity and parameters ──────────────────────────────────────
    NodeId id_;
    int    t_;              // max Byzantine nodes per round
    int    c_;              // max churn events per round
    int    delta_max_;      // upper bound on stable subgraph diameter
    int    F_;              // freshness horizon = 4(t+c)+2+δmax
    int    W_;              // stabilization window = 4(t+c)+2

    // ── Algorithm state (may start in any arbitrary value) ────────────────
    std::map<NodeId, Beacon> B_;   // beacon store: NodeId → most recent beacon
    std::vector<Token>       T_;   // token set

    // ── Output ───────────────────────────────────────────────────────────
    Decision decision_ = Decision::PARTITIONABLE;

    // ── Byzantine state ───────────────────────────────────────────────────
    bool        byzantine_   = false;
    ByzBehavior byzBehavior_ = ByzBehavior::SILENT;

    // ── Adaptive δmax state ───────────────────────────────────────────────
    // Tracks the deepest token depth seen in the current window.
    // Updated every W_ rounds to self-tune δmax and F_.
    int observedMaxDepth_ = 0;

    // ── Round tracking ────────────────────────────────────────────────────
    Round currentRound_ = 0;

    // ── Neighbor proof cache (refreshed each round from runRound args) ────
    std::map<NodeId, NeighProof> neighborProofs_;

    // ── Private helpers ───────────────────────────────────────────────────

    // Algorithm 2: UpdateBeacon
    // Stores beacon only if strictly newer than stored value.
    void updateBeacon(NodeId x, const Beacon& b);

    // Algorithm 2: Fresh
    // Returns true iff both endpoint beacons are within F rounds of r.
    bool fresh(const Token& tok, Round r) const;

    // Algorithm 2: Accept
    // Five-part validity check — conditions (a) through (e).
    bool accept(const Token& tok, Round r) const;

    // Algorithm 2: Assess
    // Builds local graph H and checks κ(H) > t+c and full reachability.
    AssessResult assess() const;

    // Algorithm 2: Reach
    // BFS reachability from id_ within the anchor graph H.
    std::set<NodeId> reach(
        const std::map<std::pair<NodeId,NodeId>, bool>& edges,
        const std::set<NodeId>& nodes) const;

    // Vertex connectivity via node-splitting max-flow (Dinic's algorithm).
    // Three-tier strategy from Appendix 6.1:
    //   Tier 0  : BFS disconnection check
    //   Tier 1b : Tarjan articulation-point detection
    //   Tier 2/3: exact or sampled max-flow
    int vertexConnectivity(
        const std::set<NodeId>& nodes,
        const std::map<std::pair<NodeId,NodeId>, bool>& edges) const;

    // Adaptive δmax: called at the start of each round.
    // Every W_ rounds, updates delta_max_ and F_ based on
    // the deepest token depth observed in the previous window.
    void updateAdaptiveDeltaMax(Round r);
};

} // namespace charon
