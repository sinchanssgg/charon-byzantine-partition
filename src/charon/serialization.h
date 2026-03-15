#pragma once
#include "messages.h"
#include <omnetpp.h>
#include <sstream>
#include <stdexcept>

using namespace omnetpp;

namespace charon {

// ── Wire format ────────────────────────────────────────────────────────────
// We serialize CharonMsg into a cMessage using a simple binary layout
// stored in a cMessage bytearray parameter.
//
// Beacon wire format:
//   [1 byte kind=0] [4 bytes sender] [8 bytes round] [N bytes sig]
//
// Token wire format:
//   [1 byte kind=1]
//   [4 bytes u] [4 bytes v]
//   [2 bytes proof_len] [proof_len bytes pi_uv]
//   [4 bytes beacon_u.sender] [8 bytes beacon_u.round]
//     [2 bytes sig_len] [sig_len bytes beacon_u.sig]
//   [4 bytes beacon_v.sender] [8 bytes beacon_v.round]
//     [2 bytes sig_len] [sig_len bytes beacon_v.sig]
//   [4 bytes depth]
//   [1 byte chain_len]
//     for each chain entry: [2 bytes sig_len] [sig_len bytes sig]

class WireBuffer {
public:
    // ── Write helpers ──────────────────────────────────────────────────────
    void writeU8(uint8_t v)   { buf_.push_back(v); }

    void writeU16(uint16_t v) {
        buf_.push_back((v >> 8) & 0xFF);
        buf_.push_back(v & 0xFF);
    }

    void writeU32(uint32_t v) {
        for (int i = 24; i >= 0; i -= 8)
            buf_.push_back((v >> i) & 0xFF);
    }

    void writeU64(uint64_t v) {
        for (int i = 56; i >= 0; i -= 8)
            buf_.push_back((v >> i) & 0xFF);
    }

    void writeBytes(const std::string& s) {
        writeU16((uint16_t)s.size());
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    std::vector<uint8_t> buf_;

    // ── Read helpers ──────────────────────────────────────────────────────
    struct Reader {
        const std::vector<uint8_t>& buf;
        size_t pos = 0;

        uint8_t  readU8()  { return buf[pos++]; }

        uint16_t readU16() {
            uint16_t v = ((uint16_t)buf[pos] << 8) | buf[pos+1];
            pos += 2; return v;
        }

        uint32_t readU32() {
            uint32_t v = 0;
            for (int i = 0; i < 4; i++) v = (v << 8) | buf[pos++];
            return v;
        }

        uint64_t readU64() {
            uint64_t v = 0;
            for (int i = 0; i < 8; i++) v = (v << 8) | buf[pos++];
            return v;
        }

        std::string readBytes() {
            uint16_t len = readU16();
            std::string s(buf.begin()+pos, buf.begin()+pos+len);
            pos += len; return s;
        }
    };
};

// ── Serialize CharonMsg → cMessage ────────────────────────────────────────
inline cMessage* serialize(const CharonMsg& msg, NodeId dest) {
    WireBuffer wb;

    if (msg.kind == MsgKind::BEACON) {
        wb.writeU8(0); // kind
        wb.writeU32(msg.beacon.sender);
        wb.writeU64(msg.beacon.round);
        wb.writeBytes(msg.beacon.sig);
    } else {
        const Token& tok = msg.token;
        wb.writeU8(1); // kind
        wb.writeU32(tok.u);
        wb.writeU32(tok.v);
        wb.writeBytes(tok.pi_uv);
        // beacon_u
        wb.writeU32(tok.beacon_u.sender);
        wb.writeU64(tok.beacon_u.round);
        wb.writeBytes(tok.beacon_u.sig);
        // beacon_v
        wb.writeU32(tok.beacon_v.sender);
        wb.writeU64(tok.beacon_v.round);
        wb.writeBytes(tok.beacon_v.sig);
        // depth + chain
        wb.writeU32((uint32_t)tok.depth);
        wb.writeU8((uint8_t)tok.chain.size());
        for (auto& s : tok.chain)
            wb.writeBytes(s);
    }

    // Pack into cMessage as a byte parameter
    cMessage* omnetMsg = new cMessage("CharonMsg");
    omnetMsg->addPar("dest").setLongValue(dest);
    omnetMsg->addPar("len").setLongValue((long)wb.buf_.size());

    // Store raw bytes as a string parameter
    std::string raw(wb.buf_.begin(), wb.buf_.end());
    omnetMsg->addPar("payload").setStringValue(raw.c_str());
    return omnetMsg;
}

// ── Deserialize cMessage → CharonMsg ──────────────────────────────────────
inline CharonMsg deserialize(cMessage* omnetMsg) {
    std::string raw = omnetMsg->par("payload").stringValue();
    std::vector<uint8_t> buf(raw.begin(), raw.end());
    WireBuffer::Reader rd{buf, 0};

    CharonMsg msg;
    uint8_t kind = rd.readU8();

    if (kind == 0) {
        // Beacon
        msg.kind           = MsgKind::BEACON;
        msg.beacon.sender  = rd.readU32();
        msg.beacon.round   = rd.readU64();
        msg.beacon.sig     = rd.readBytes();
    } else {
        // Token
        msg.kind       = MsgKind::TOKEN;
        Token& tok     = msg.token;
        tok.u          = rd.readU32();
        tok.v          = rd.readU32();
        tok.pi_uv      = rd.readBytes();
        // beacon_u
        tok.beacon_u.sender = rd.readU32();
        tok.beacon_u.round  = rd.readU64();
        tok.beacon_u.sig    = rd.readBytes();
        // beacon_v
        tok.beacon_v.sender = rd.readU32();
        tok.beacon_v.round  = rd.readU64();
        tok.beacon_v.sig    = rd.readBytes();
        // depth + chain
        tok.depth = (int)rd.readU32();
        uint8_t chainLen = rd.readU8();
        tok.chain.resize(chainLen);
        for (auto& s : tok.chain)
            s = rd.readBytes();
    }
    return msg;
}

} // namespace charon
