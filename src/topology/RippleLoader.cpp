#include "RippleLoader.h"
#include "../crypto/crypto.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <random>
#include <stdexcept>
#include <iostream>

namespace charon {

TopologySnapshot RippleLoader::load(const std::string& csvPath,
                                    int maxNodes,
                                    unsigned seed)
{
    std::ifstream file(csvPath);
    if (!file.is_open())
        throw std::runtime_error("Cannot open topology file: " + csvPath);

    TopologySnapshot topo;
    std::string line;
    // Skip header if present
    std::getline(file, line);
    if (line.find_first_not_of("0123456789,") != std::string::npos) {
        // Header detected — skip it and continue
    } else {
        // No header — reprocess this line as data
        std::istringstream ss(line);
        std::string su, sv;
        if (std::getline(ss, su, ',') && std::getline(ss, sv)) {
            NodeId u = (NodeId)std::stoul(su);
            NodeId v = (NodeId)std::stoul(sv);
            topo.nodes.insert(u); topo.nodes.insert(v);
            topo.degree[u]++; topo.degree[v]++;
            topo.edges.push_back({u, v, makeNeighProof(u, v)});
        }
    }

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string su, sv;
        if (!std::getline(ss, su, ',') || !std::getline(ss, sv)) continue;
        try {
            NodeId u = (NodeId)std::stoul(su);
            NodeId v = (NodeId)std::stoul(sv);
            topo.nodes.insert(u);
            topo.nodes.insert(v);
            topo.degree[u]++;
            topo.degree[v]++;
            topo.edges.push_back({u, v, makeNeighProof(u, v)});
        } catch (...) { continue; }
    }

    // ── Subsample while preserving degree distribution ────────────────────
    if (maxNodes > 0 && (int)topo.nodes.size() > maxNodes) {
        std::mt19937 rng(seed);

        // Build degree-weighted sampling distribution
        std::vector<NodeId> nodeVec(topo.nodes.begin(), topo.nodes.end());
        std::vector<double> weights;
        for (NodeId n : nodeVec)
            weights.push_back((double)topo.degree.count(n) ?
                              topo.degree.at(n) : 1.0);

        std::discrete_distribution<int> dist(weights.begin(), weights.end());

        std::set<NodeId> sampled;
        int attempts = 0;
        while ((int)sampled.size() < maxNodes &&
               attempts < maxNodes * 10) {
            sampled.insert(nodeVec[dist(rng)]);
            attempts++;
        }

        // Rebuild topology with only sampled nodes
        TopologySnapshot sub;
        sub.nodes = sampled;
        for (auto& e : topo.edges) {
            if (sampled.count(e.u) && sampled.count(e.v)) {
                sub.edges.push_back(e);
                sub.degree[e.u]++;
                sub.degree[e.v]++;
            }
        }
        std::cout << "[RippleLoader] Subsampled: "
                  << sub.nodes.size() << " nodes, "
                  << sub.edges.size() << " edges\n";
        return sub;
    }

    std::cout << "[RippleLoader] Loaded: "
              << topo.nodes.size() << " nodes, "
              << topo.edges.size() << " edges\n";
    return topo;
}

TopologySnapshot RippleLoader::applychurn(const TopologySnapshot& prev,
                                          int c,
                                          unsigned seed)
{
    std::mt19937 rng(seed);
    TopologySnapshot next = prev;

    // ── Remove c nodes (prefer low-degree nodes — realistic churn) ────────
    std::vector<NodeId> nodeVec(next.nodes.begin(), next.nodes.end());
    std::vector<double> leaveWeights;
    for (NodeId n : nodeVec) {
        int deg = next.degree.count(n) ? next.degree.at(n) : 0;
        // Low-degree nodes more likely to leave
        leaveWeights.push_back(1.0 / (deg + 1.0));
    }

    std::discrete_distribution<int> leaveDist(
        leaveWeights.begin(), leaveWeights.end());

    std::set<NodeId> leaving;
    int attempts = 0;
    while ((int)leaving.size() < c &&
           attempts < c * 10 &&
           leaving.size() < nodeVec.size()) {
        leaving.insert(nodeVec[leaveDist(rng)]);
        attempts++;
    }

    // Remove leaving nodes and their edges
    for (NodeId n : leaving) {
        next.nodes.erase(n);
        next.degree.erase(n);
    }
    next.edges.erase(
        std::remove_if(next.edges.begin(), next.edges.end(),
            [&](const Edge& e){
                return leaving.count(e.u) || leaving.count(e.v);
            }),
        next.edges.end());
    // Update degrees after edge removal
    for (auto& e : next.edges) {
        next.degree[e.u] = 0;
        next.degree[e.v] = 0;
    }
    for (auto& e : next.edges) {
        next.degree[e.u]++;
        next.degree[e.v]++;
    }

    // ── Add c fresh nodes, each connecting to 2 random existing nodes ─────
    static NodeId nextId = 1000000; // fresh node IDs start high
    std::uniform_int_distribution<int> pickNode(
        0, (int)next.nodes.size()-1);
    std::vector<NodeId> existing(next.nodes.begin(), next.nodes.end());

    for (int i = 0; i < c && !existing.empty(); i++) {
        NodeId fresh = nextId++;
        next.nodes.insert(fresh);
        // Connect to 2 random existing nodes
        for (int k = 0; k < 2 && !existing.empty(); k++) {
            NodeId peer = existing[pickNode(rng) % existing.size()];
            next.edges.push_back({fresh, peer, makeNeighProof(fresh, peer)});
            next.degree[fresh]++;
            next.degree[peer]++;
        }
        existing.push_back(fresh);
    }

    return next;
}

void RippleLoader::writeNed(const TopologySnapshot& topo,
                            const std::string& outPath,
                            int t, int c, int deltaMax,
                            float byzantineFraction)
{
    std::ofstream out(outPath);
    if (!out.is_open())
        throw std::runtime_error("Cannot write NED file: " + outPath);

    // Map NodeId → sequential index for OMNeT++
    std::map<NodeId, int> idx;
    int i = 0;
    for (NodeId n : topo.nodes) idx[n] = i++;
    int N = (int)topo.nodes.size();

    // Determine which nodes are Byzantine (worst-case: highest degree)
    int numByz = std::max(1, (int)(N * byzantineFraction));
    std::vector<std::pair<int,NodeId>> byDeg;
    for (NodeId n : topo.nodes)
        byDeg.push_back({topo.degree.count(n) ? topo.degree.at(n) : 0, n});
    std::sort(byDeg.rbegin(), byDeg.rend());
    std::set<NodeId> byzSet;
    for (int b = 0; b < numByz && b < (int)byDeg.size(); b++)
        byzSet.insert(byDeg[b].second);

    out << "package charon;\n\n";
    out << "network RippleNetwork {\n";
    out << "    parameters:\n";
    out << "        int t = " << t << ";\n";
    out << "        int c = " << c << ";\n";
    out << "        int deltaMax = " << deltaMax << ";\n";
    out << "    submodules:\n";

    for (NodeId n : topo.nodes) {
        int id = idx[n];
        bool byz = byzSet.count(n) > 0;
        out << "        node_" << id << ": CharonNodeModule {\n";
        out << "            nodeId    = " << id << ";\n";
        out << "            t         = " << t << ";\n";
        out << "            c         = " << c << ";\n";
        out << "            deltaMax  = " << deltaMax << ";\n";
        out << "            byzantine = " << (byz ? "true" : "false") << ";\n";
        out << "            byzBehavior = 1;\n"; // selective fwd as default
        out << "        }\n";
    }

    out << "    connections:\n";
    for (auto& e : topo.edges) {
        if (!idx.count(e.u) || !idx.count(e.v)) continue;
        out << "        node_" << idx[e.u]
            << ".port++ <--> node_" << idx[e.v] << ".port++;\n";
    }
    out << "}\n";

    std::cout << "[RippleLoader] Wrote NED: " << outPath
              << " (" << N << " nodes, "
              << topo.edges.size() << " edges, "
              << numByz << " Byzantine)\n";
}

} // namespace charon
