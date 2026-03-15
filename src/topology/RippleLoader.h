#pragma once
#include <string>
#include <vector>
#include <set>
#include <map>
#include "../charon/messages.h"

namespace charon {

struct Edge {
    NodeId u, v;
    NeighProof proof;
};

struct TopologySnapshot {
    std::set<NodeId>              nodes;
    std::vector<Edge>             edges;
    std::map<NodeId,int>          degree;
};

class RippleLoader {
public:
    // Load from a CSV file with lines: "node_u,node_v"
    // Optionally subsample to maxNodes nodes (preserving degree distribution)
    static TopologySnapshot load(const std::string& csvPath,
                                 int maxNodes = -1,
                                 unsigned seed = 42);

    // Apply one round of churn: remove and add up to c nodes
    static TopologySnapshot applychurn(const TopologySnapshot& prev,
                                       int c,
                                       unsigned seed = 0);

    // Write a .ned file for OMNeT++ from a snapshot
    static void writeNed(const TopologySnapshot& topo,
                         const std::string& outPath,
                         int t, int c, int deltaMax,
                         float byzantineFraction = 0.05f);
};

} // namespace charon
