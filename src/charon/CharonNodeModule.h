#pragma once
#include <omnetpp.h>
#include <map>
#include <vector>
#include "node.h"

using namespace omnetpp;

class CharonNodeModule : public cSimpleModule {
protected:
    void initialize() override;
    void handleMessage(cMessage* msg) override;
    void finish() override;

private:
    charon::CharonNode* algo_      = nullptr;
    int           t_               = 0;
    int           c_               = 0;
    int           deltaMax_        = 0;
    charon::Round round_           = 0;

    std::vector<charon::CharonMsg>         inbox_;
    std::map<charon::NodeId, int>          gateMap_;
    std::map<charon::NodeId,
             charon::NeighProof>           neighProofMap_; // ← NEW
    cMessage*                              roundTick_ = nullptr;

    simsignal_t decision_signal_;
    simsignal_t msgSent_signal_;
    simsignal_t anchorSize_signal_;                        // ← NEW

    void startNextRound();
    void recordStats(int msgsSent);
    void applyChurn(charon::Round r);                      // ← NEW
};
