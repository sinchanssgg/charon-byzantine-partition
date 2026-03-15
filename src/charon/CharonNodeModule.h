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
    charon::CharonNode* algo_     = nullptr;
    int     t_, c_, deltaMax_;
    charon::Round round_          = 0;

    std::vector<charon::CharonMsg> inbox_;
    std::map<charon::NodeId, int>  gateMap_;
    cMessage* roundTick_          = nullptr;

    // OMNeT++ statistics signals
    simsignal_t decision_signal_;
    simsignal_t msgCount_signal_;

    void startNextRound();
    void recordStats(int msgsSent);
};
