#include "CharonNodeModule.h"
#include <omnetpp.h>

Define_Module(CharonNodeModule);

void CharonNodeModule::initialize() {
    int id       = par("nodeId");
    t_           = par("t");
    c_           = par("c");
    deltaMax_    = par("deltaMax");
    bool byz     = par("byzantine");

    algo_ = new charon::CharonNode(id, t_, c_, deltaMax_);
    if (byz) {
        algo_->setByzantine(true);
        int beh = par("byzBehavior");
        algo_->setByzBehavior(
            static_cast<charon::CharonNode::ByzBehavior>(beh));
    }

    // Self-message to trigger each round
    roundTick_ = new cMessage("roundTick");
    scheduleAt(simTime() + 1.0, roundTick_);
}

void CharonNodeModule::handleMessage(cMessage* msg) {
    if (msg == roundTick_) {
        startNextRound();
        scheduleAt(simTime() + 1.0, roundTick_);
        return;
    }
    // Incoming CharonMsg from a neighbor — buffer for this round
    // (In a real impl, deserialize the cMessage payload into CharonMsg here)
    // inbox_.push_back(deserialize(msg));
    delete msg;
}

void CharonNodeModule::startNextRound() {
    // Build neighbor list from gates
    std::vector<std::pair<charon::NodeId, charon::NeighProof>> neighbors;
    for (int i = 0; i < gateSize("port"); i++) {
        cGate* g = gate("port$o", i);
        if (g->isConnected()) {
            // In a full impl, retrieve neighbor id and proof from gate metadata
            // neighbors.push_back({neighborId, proof});
        }
    }

    auto outbox = algo_->runRound(round_++, neighbors, inbox_);
    inbox_.clear();

    // Send outgoing messages
    for (auto& [dest, charMsg] : outbox) {
        // Serialize charMsg into a cMessage and send to the right gate
        // cMessage* omnetMsg = serialize(charMsg);
        // send(omnetMsg, "port$o", gateIndexTo(dest));
    }

    recordStats();
}

void CharonNodeModule::recordStats() {
    recordScalar("decision",
        algo_->lastDecision() ==
        charon::Decision::NOT_PARTITIONABLE ? 1.0 : 0.0);
}

void CharonNodeModule::finish() {
    delete algo_;
}
