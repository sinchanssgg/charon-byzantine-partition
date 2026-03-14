#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Simulated ECDSA for OMNeT++ — swap with real OpenSSL calls for production
namespace charon {

using NodeId   = uint32_t;
using Round    = uint64_t;
using Sig      = std::string;   // base64-encoded signature stub
using NeighProof = std::string; // static neighborhood proof

// Sign message m with node i's private key
Sig sign(NodeId i, const std::string& m);

// Verify signature
bool verify(NodeId i, const std::string& m, const Sig& sig);

// Generate a neighborhood proof for edge (u,v)
// Called once at connection time; unforgeable if one endpoint is correct
NeighProof makeNeighProof(NodeId u, NodeId v);
bool verifyNeighProof(NodeId u, NodeId v, const NeighProof& proof);

// Helpers
std::string concat(NodeId id, Round r);
std::string concat(NodeId u, NodeId v, int depth, Round r);

} // namespace charon
