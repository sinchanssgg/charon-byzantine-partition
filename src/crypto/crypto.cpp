#include "crypto.h"
#include <sstream>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <map>

namespace charon {

// In a real deployment, each node holds a persistent EC key pair.
// Here we generate deterministic keys from node IDs for simulation.
static std::map<NodeId, EC_KEY*> keyStore;

static EC_KEY* getKey(NodeId id) {
    if (keyStore.count(id)) return keyStore[id];
    EC_KEY* key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_KEY_generate_key(key);
    keyStore[id] = key;
    return key;
}

Sig sign(NodeId i, const std::string& m) {
    EC_KEY* key = getKey(i);
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(m.data()), m.size(), hash);
    unsigned int sigLen = ECDSA_size(key);
    std::vector<unsigned char> sig(sigLen);
    ECDSA_sign(0, hash, SHA256_DIGEST_LENGTH, sig.data(), &sigLen, key);
    return std::string(sig.begin(), sig.begin() + sigLen);
}

bool verify(NodeId i, const std::string& m, const Sig& sig) {
    EC_KEY* key = getKey(i);
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(m.data()), m.size(), hash);
    int result = ECDSA_verify(0, hash,
        SHA256_DIGEST_LENGTH,
        reinterpret_cast<const unsigned char*>(sig.data()),
        static_cast<int>(sig.size()), key);
    return result == 1;
}

NeighProof makeNeighProof(NodeId u, NodeId v) {
    // Mutual handshake: sign sorted (u,v) pair with both keys
    std::string msg = std::to_string(std::min(u,v)) + ":" + std::to_string(std::max(u,v));
    return sign(u, msg) + "|" + sign(v, msg);
}

bool verifyNeighProof(NodeId u, NodeId v, const NeighProof& proof) {
    auto sep = proof.find('|');
    if (sep == std::string::npos) return false;
    std::string msg = std::to_string(std::min(u,v)) + ":" + std::to_string(std::max(u,v));
    return verify(u, proof.substr(0, sep), msg) ||
           verify(v, proof.substr(sep+1), msg);
}

std::string concat(NodeId id, Round r) {
    return std::to_string(id) + "||" + std::to_string(r);
}
std::string concat(NodeId u, NodeId v, int depth, Round r) {
    return std::to_string(u) + "||" + std::to_string(v)
         + "||" + std::to_string(depth) + "||" + std::to_string(r);
}

} // namespace charon
