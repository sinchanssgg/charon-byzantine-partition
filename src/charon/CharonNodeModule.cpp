#include "CharonNodeModule.h"
#include "serialization.h"
#include <omnetpp.h>

Define_Module(CharonNodeModule);

// ── Gate index lookup ──────────────────────────────────────────────────────
// Maps neighbor node id → output gate index
// Populated during initialize() by inspecting connected gates
std::map<charon::NodeId, int> gateMap_;

void CharonNodeModule::initialize() {
    int id    = par("nodeId");
    t_        = par("t");
    c_        = par("c");
    deltaMax_ = par("deltaMax");
    bool byz  = par("byzantine");

    algo_ = new charon::CharonNode(id, t_, c_, deltaMax_);

    if (byz) {
        algo_->setByzantine(true);
        int beh = par("byzBehavior");
        algo_->setByzBehavior(
            static_cast<charon::CharonNode::ByzBehavior>(beh));
    }

    // Build gate → neighbor id map by reading gate parameters
    // Each gate's far-end module exposes a "nodeId" parameter
    for (int i = 0; i < gateSize("port"); i++) {
        cGate* g = gate("port$o", i);
        if (g->isConnected()) {
            cModule* peer = g->getNextGate()->getOwnerModule();
            int peerId = peer->par("nodeId");
            gateMap_[(charon::NodeId)peerId] = i;
        }
    }

    // Register statistics
    decision_signal_ = registerSignal("decision");
    msgCount_signal_ = registerSignal("msgCount");

    roundTick_ = new cMessage("roundTick");
    scheduleAt(simTime() + 1.0, roundTick_);
}

void CharonNodeModule::handleMessage(cMessage* msg) {
    if (msg == roundTick_) {
        startNextRound();
        scheduleAt(simTime() + 1.0, roundTick_);
        return;
    }

    // Deserialize incoming cMessage into CharonMsg and buffer it
    try {
        charon::CharonMsg charMsg = charon::deserialize(msg);
        inbox_.push_back(charMsg);
    } catch (...) {
        EV_WARN << "Node " << par("nodeId").longValue()
                << ": failed to deserialize message, dropping.\n";
    }
    delete msg;
}

void CharonNodeModule::startNextRound() {
    // Build neighbor list with proofs from gate map
    std::vector<std::pair<charon::NodeId, charon::NeighProof>> neighbors;
    for (auto& [nid, gateIdx] : gateMap_) {
        cGate* g = gate("port$o", gateIdx);
        if (g->isConnected()) {
            // Neighborhood proof: stored as a gate parameter set at
            // connection time (see topology loader)
            std::string proof = "";
            if (g->hasPar("neighProof"))
                proof = g->par("neighProof").stringValue();
            neighbors.push_back({nid, proof});
        }
    }

    // Run one round of the Charon algorithm
    auto outbox = algo_->runRound(round_++, neighbors, inbox_);
    inbox_.clear();

    // Send outgoing messages
    int sent = 0;
    for (auto& [dest, charMsg] : outbox) {
        auto it = gateMap_.find(dest);
        if (it == gateMap_.end()) continue; // neighbor may have churned

        cMessage* omnetMsg = charon::serialize(charMsg, dest);
        send(omnetMsg, "port$o", it->second);
        sent++;
    }

    recordStats(sent);
}

void CharonNodeModule::recordStats(int msgsSent) {
    using charon::Decision;
    double dec = (algo_->lastDecision() ==
                  Decision::NOT_PARTITIONABLE) ? 1.0 : 0.0;
    emit(decision_signal_, dec);
    emit(msgCount_signal_,  (double)msgsSent);
}

void CharonNodeModule::finish() {
    delete algo_;
    cancelAndDelete(roundTick_);
}
