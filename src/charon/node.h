#pragma once
#include "messages.h"
#include <map>
#include <set>
#include <vector>

namespace charon {

enum class Decision { NOT_PARTITIONABLE, PARTITIONABLE };

struct AssessResult {
    Decision decision;
    bool     confirmed;  // true if direct disconnection observed
};

class CharonNode {
public:
    CharonNode(NodeId id, int t, int c, int delta_max);

    // Called once per round by the simulator
    // neighbors: current neighbor set with their proofs
    // inbox:     messages received this round
    // Returns outbox: messages to send (dest → msg)
    std::vector<std::pair<NodeId, CharonMsg>>
    runRound(Round r,
             const std::vector<std::pair<NodeId, NeighProof>>& neighbors,
             const std::vector<CharonMsg>& inbox);

    Decision     lastDecision() const { return decision_; }
    bool         isByzantine()  const { return byzantine_; }
    void         setByzantine(bool b) { byzantine_ = b; }

    // Byzantine behaviors (used by adversary)
    enum class ByzBehavior { SILENT, SELECTIVE_FWD, STALE_REPLAY, FLOOD };
    void         setByzBehavior(ByzBehavior b) { byzBehavior_ = b; }

private:
    // --- algorithm state ---
    NodeId  id_;
    int     t_, c_, delta_max_;
    int     F_, W_;                         // freshness horizon, stabilization window
    bool    byzantine_  = false;
    ByzBehavior byzBehavior_ = ByzBehavior::SILENT;

    std::map<NodeId, Beacon> B_;            // beacon store
    std::vector<Token>       T_;            // token set
    Decision                 decision_ = Decision::PARTITIONABLE;

    // --- helpers ---
    void   updateBeacon(NodeId x, const Beacon& b);
    bool   fresh(const Token& tok, Round r) const;
    bool   accept(const Token& tok, Round r) const;
    AssessResult assess() const;
    std::set<NodeId> reach(
        const std::map<std::pair<NodeId,NodeId>, bool>& edges,
        const std::set<NodeId>& nodes) const;
    int    vertexConnectivity(
        const std::set<NodeId>& nodes,
        const std::map<std::pair<NodeId,NodeId>, bool>& edges) const;

    // Cache of neighbor proofs
    std::map<NodeId, NeighProof> neighborProofs_;
    Round currentRound_ = 0;
};

} // namespace charon
