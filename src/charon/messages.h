#pragma once
#include "../crypto/crypto.h"
#include <vector>
#include <string>

namespace charon {

// ============================================================================
//  Beacon  —  β_i^r = (i, r, σ_i(i‖r))
//
//  Signed per-round liveness broadcast. The round number is included in
//  the signature so beacons cannot be replayed in later rounds.
// ============================================================================
struct Beacon {
    NodeId sender;
    Round  round;
    Sig    sig;       // σ_i(i ‖ r)

    // Returns true iff the signature is valid for this sender and round.
    bool valid() const {
        return verify(sender, concat(sender, round), sig);
    }

    // Default constructor — produces an invalid beacon
    Beacon() : sender(0), round(0) {}

    Beacon(NodeId s, Round r, Sig signature)
        : sender(s), round(r), sig(std::move(signature)) {}
};

// ============================================================================
//  Token  —  τ = (u, v, π_{u,v}, β_u, β_v, d, S)
//
//  Certified link record. Certifies that edge (u,v) exists and both
//  endpoints were recently active. Expires when either beacon falls
//  outside the freshness horizon F.
//
//  Fields:
//    u, v        — edge endpoints (stored with u < v for canonical form)
//    pi_uv       — neighborhood proof, established at connection time,
//                  unforgeable if at least one endpoint is correct
//    beacon_u    — most recent beacon seen for u
//    beacon_v    — most recent beacon seen for v
//    depth       — relay hop count (d ≥ 1)
//    chain       — ordered list of d relay signatures
//                  chain[k] = σ_{relay_k}(u ‖ v ‖ k+1 ‖ r)
//    signerIds   — parallel list to chain storing the NodeId of each
//                  relay signer; used for loop prevention in the relay
//                  step so we never send a token back to a node already
//                  in the chain
// ============================================================================
struct Token {
    NodeId   u, v;
    NeighProof          pi_uv;
    Beacon              beacon_u;
    Beacon              beacon_v;
    int                 depth;          // d ≥ 1
    std::vector<Sig>    chain;          // relay signature chain S
    std::vector<NodeId> signerIds;      // parallel to chain — who signed

    // Default constructor
    Token() : u(0), v(0), depth(0) {}

    Token(NodeId u_, NodeId v_,
          NeighProof proof,
          Beacon bu, Beacon bv,
          int d,
          std::vector<Sig> s,
          std::vector<NodeId> ids)
        : u(u_), v(v_)
        , pi_uv(std::move(proof))
        , beacon_u(std::move(bu))
        , beacon_v(std::move(bv))
        , depth(d)
        , chain(std::move(s))
        , signerIds(std::move(ids))
    {}
};

// ============================================================================
//  MsgKind  —  discriminator for CharonMsg
// ============================================================================
enum class MsgKind {
    BEACON,
    TOKEN
};

// ============================================================================
//  CharonMsg  —  the single message type exchanged between nodes
//
//  Used as the in-memory representation. Serialized to/from cMessage
//  by the wire format in serialization.h before being sent over OMNeT++
//  gates.
// ============================================================================
struct CharonMsg {
    MsgKind kind;
    Beacon  beacon;   // valid when kind == BEACON
    Token   token;    // valid when kind == TOKEN

    // Convenience constructors
    static CharonMsg makeBeacon(Beacon b) {
        CharonMsg m;
        m.kind   = MsgKind::BEACON;
        m.beacon = std::move(b);
        return m;
    }

    static CharonMsg makeToken(Token t) {
        CharonMsg m;
        m.kind  = MsgKind::TOKEN;
        m.token = std::move(t);
        return m;
    }
};

} // namespace charon
