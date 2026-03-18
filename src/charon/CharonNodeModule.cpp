#include "CharonNodeModule.h"
#include "serialization.h"
#include <omnetpp.h>
#include <stdexcept>

Define_Module(CharonNodeModule);

// ============================================================================
//  initialize
//  Called once by OMNeT++ before the simulation starts.
//  Reads parameters, constructs the CharonNode instance, builds the
//  gate→neighbor map, registers statistics signals, and schedules the
//  first round tick.
// ============================================================================
void CharonNodeModule::initialize() {
    // ── Read module parameters from omnetpp.ini / NED ─────────────────────
    int id    = (int)par("nodeId");
    t_        = (int)par("t");
    c_        = (int)par("c");
    deltaMax_ = (int)par("deltaMax");
    bool byz  = (bool)par("byzantine");

    // ── Construct core algorithm instance ─────────────────────────────────
    algo_ = new charon::CharonNode(
                (charon::NodeId)id, t_, c_, deltaMax_);

    if (byz) {
        algo_->setByzantine(true);
        int beh = (int)par("byzBehavior");
        algo_->setByzBehavior(
            static_cast<charon::CharonNode::ByzBehavior>(beh));
    }

    // ── Build gate → neighbor NodeId map ─────────────────────────────────
    // Each peer module exposes a "nodeId" parameter we can read here.
    // We also read the neighborhood proof stored on the gate if present.
    for (int i = 0; i < gateSize("port"); i++) {
        cGate* g = gate("port$o", i);
        if (!g->isConnected()) continue;

        cModule* peer   = g->getNextGate()->getOwnerModule();
        charon::NodeId peerId = (charon::NodeId)(int)peer->par("nodeId");
        gateMap_[peerId] = i;

        // Store neighborhood proof if the gate carries one.
        // In the generated NED files the proof is not attached to gates
        // directly — instead we generate it here at init time using the
        // same deterministic makeNeighProof() used by the crypto layer.
        charon::NodeId myId = (charon::NodeId)id;
        neighProofMap_[peerId] = charon::makeNeighProof(myId, peerId);
    }

    // ── Register OMNeT++ statistics signals ───────────────────────────────
    decision_signal_  = registerSignal("decision");
    msgSent_signal_   = registerSignal("msgSent");
    anchorSize_signal_= registerSignal("anchorSize");

    // ── Schedule first round tick ─────────────────────────────────────────
    roundTick_ = new cMessage("roundTick");
    scheduleAt(simTime() + 1.0, roundTick_);

    EV_INFO << "CharonNodeModule " << id
            << " initialized. Byzantine=" << byz
            << " t=" << t_ << " c=" << c_
            << " deltaMax=" << deltaMax_ << "\n";
}

// ============================================================================
//  handleMessage
//  Called by OMNeT++ for every message arriving at this module.
//  Round ticks drive the algorithm; all other messages are incoming
//  CharonMsgs from neighbors, deserialized and buffered for the round.
// ============================================================================
void CharonNodeModule::handleMessage(cMessage* msg) {
    if (msg == roundTick_) {
        startNextRound();
        scheduleAt(simTime() + 1.0, roundTick_);
        return;
    }

    // ── Deserialize incoming CharonMsg and buffer it ──────────────────────
    try {
        charon::CharonMsg charMsg = charon::deserialize(msg);
        inbox_.push_back(std::move(charMsg));
    } catch (const std::exception& e) {
        EV_WARN << "Node " << par("nodeId").intValue()
                << ": deserialization failed: " << e.what()
                << " — dropping message.\n";
    } catch (...) {
        EV_WARN << "Node " << par("nodeId").intValue()
                << ": unknown deserialization error — dropping message.\n";
    }
    delete msg;
}

// ============================================================================
//  startNextRound
//  Drives one round of the Charon algorithm:
//    1. Build the current neighbor list (checks gate connectivity for churn)
//    2. Call CharonNode::runRound with buffered inbox
//    3. Serialize and send each outgoing message
//    4. Clear inbox for next round
//    5. Emit statistics
// ============================================================================
void CharonNodeModule::startNextRound() {
    // ── 1. Build neighbor list from currently connected gates ─────────────
    // Gates may have been disconnected since last round (churn).
    // We re-scan every round rather than trusting the cached gateMap_.
    std::vector<std::pair<charon::NodeId, charon::NeighProof>> neighbors;

    for (auto& [nid, gateIdx] : gateMap_) {
        cGate* g = gate("port$o", gateIdx);
        if (!g->isConnected()) continue;

        auto proofIt = neighProofMap_.find(nid);
        charon::NeighProof proof =
            (proofIt != neighProofMap_.end()) ? proofIt->second : "";

        neighbors.push_back({nid, proof});
    }

    // ── 2. Run one round of the Charon algorithm ──────────────────────────
    auto outbox = algo_->runRound(round_++, neighbors, inbox_);
    inbox_.clear();

    // ── 3. Serialize and send outgoing messages ───────────────────────────
    int sent = 0;
    for (auto& [dest, charMsg] : outbox) {
        auto gateIt = gateMap_.find(dest);
        if (gateIt == gateMap_.end()) continue;

        cGate* g = gate("port$o", gateIt->second);
        if (!g->isConnected()) continue;  // neighbor may have churned

        cMessage* omnetMsg = nullptr;
        try {
            omnetMsg = charon::serialize(charMsg, dest);
        } catch (const std::exception& e) {
            EV_WARN << "Node " << par("nodeId").intValue()
                    << ": serialization failed for dest=" << dest
                    << ": " << e.what() << "\n";
            continue;
        }

        send(omnetMsg, "port$o", gateIt->second);
        sent++;
    }

    // ── 4. Emit statistics ────────────────────────────────────────────────
    recordStats(sent);
}

// ============================================================================
//  recordStats
//  Emits per-round statistics to OMNeT++ scalar and vector recorders.
//    decision    : 1.0 = NotPartitionable, 0.0 = Partitionable
//    msgSent     : number of messages sent this round
//    anchorSize  : current size of the beacon store (δ_i)
// ============================================================================
void CharonNodeModule::recordStats(int msgsSent) {
    double dec = (algo_->lastDecision() ==
                  charon::Decision::NOT_PARTITIONABLE) ? 1.0 : 0.0;

    emit(decision_signal_,   dec);
    emit(msgSent_signal_,    (double)msgsSent);
    emit(anchorSize_signal_, (double)algo_->anchorSetSize());
}

// ============================================================================
//  applyChurn
//  Disconnects up to c_ gates at random to simulate node departure,
//  and reconnects others to simulate node arrival.
//  Called from startNextRound() when churn simulation is enabled.
//
//  Note: full dynamic topology changes (adding new cModule instances)
//  require OMNeT++ 6 dynamic module creation APIs and are handled by
//  a separate ChurnController module in the network. This method handles
//  the simpler case of gate disconnection on the node side.
// ============================================================================
void CharonNodeModule::applyChurn(charon::Round r) {
    std::mt19937 rng((unsigned)(r * 1000 +
                     (unsigned)par("nodeId").intValue()));
    std::uniform_real_distribution<> dist(0.0, 1.0);

    // Probability that any given neighbor churns this round
    double churnProb = (double)c_ /
                       std::max(1, (int)gateMap_.size());

    for (auto it = gateMap_.begin(); it != gateMap_.end(); ) {
        if (dist(rng) < churnProb) {
            cGate* g = gate("port$o", it->second);
            if (g->isConnected()) {
                g->disconnect();
                EV_INFO << "Node " << par("nodeId").intValue()
                        << ": disconnected from neighbor "
                        << it->first << " (churn)\n";
            }
            neighProofMap_.erase(it->first);
            it = gateMap_.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
//  finish
//  Called by OMNeT++ at the end of the simulation.
//  Records final scalar statistics and cleans up.
// ============================================================================
void CharonNodeModule::finish() {
    // Record final decision as a scalar for result analysis
    recordScalar("finalDecision",
        algo_->lastDecision() ==
        charon::Decision::NOT_PARTITIONABLE ? 1.0 : 0.0);

    recordScalar("totalRounds", (double)round_);
    recordScalar("finalAnchorSize", (double)algo_->anchorSetSize());
    recordScalar("isByzantine", algo_->isByzantine() ? 1.0 : 0.0);

    EV_INFO << "Node " << par("nodeId").intValue()
            << " finished after " << round_ << " rounds. "
            << "Final decision: "
            << (algo_->lastDecision() ==
                charon::Decision::NOT_PARTITIONABLE
                ? "NotPartitionable" : "Partitionable")
            << "\n";

    delete algo_;
    algo_ = nullptr;

    cancelAndDelete(roundTick_);
    roundTick_ = nullptr;
}
