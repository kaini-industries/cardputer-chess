#ifndef CHESS_NET_PROTOCOL_H
#define CHESS_NET_PROTOCOL_H

#include "chess_types.h"
#include <cstdint>

// =====================================================================
// Chess Network Protocol: Message definitions for ESP-NOW multiplayer.
// All messages start with a 1-byte type tag.
// All structs are packed to match wire format exactly.
// =====================================================================

static constexpr uint8_t NET_PROTOCOL_VERSION = 2;

enum class NetMsgType : uint8_t {
    Discovery    = 0x01,
    AcceptGame   = 0x02,
    GameStart    = 0x03,
    MoveMsg      = 0x10,
    MoveAck      = 0x11,
    Heartbeat    = 0x20,
    Resign       = 0x30,
};

// ── Discovery & Pairing ──────────────────────────────────────────────

#pragma pack(push, 1)

struct DiscoveryMsg {
    NetMsgType type = NetMsgType::Discovery;
    uint8_t    version = NET_PROTOCOL_VERSION;
    uint16_t   gameId = 0;
    uint8_t    variant = 0;        // ChessVariant as uint8_t
    uint16_t   positionIndex = 518; // Chess960 position (518 = standard)
    uint8_t    timeControl = 0;    // TimeControl as uint8_t
};

struct AcceptGameMsg {
    NetMsgType type = NetMsgType::AcceptGame;
    uint8_t    version = NET_PROTOCOL_VERSION;
    uint16_t   gameId = 0;
};

struct GameStartMsg {
    NetMsgType type = NetMsgType::GameStart;
    uint8_t    yourColor = 0;      // 0 = White, 1 = Black
    uint8_t    variant = 0;        // ChessVariant as uint8_t
    uint16_t   positionIndex = 518;
    uint8_t    timeControl = 0;    // TimeControl as uint8_t
};

// ── Gameplay ─────────────────────────────────────────────────────────

struct MoveNetMsg {
    NetMsgType type = NetMsgType::MoveMsg;
    uint8_t    seq = 0;       // Sequence number for ack/dedup
    uint8_t    fromCol = 0;
    uint8_t    fromRow = 0;
    uint8_t    toCol = 0;
    uint8_t    toRow = 0;
    uint8_t    promotion = 0; // PieceType as uint8_t (0 = None)
    uint8_t    flags = 0;     // bit 0 = isCastle, bit 1 = isEnPassant
};

struct MoveAckMsg {
    NetMsgType type = NetMsgType::MoveAck;
    uint8_t    seq = 0;
};

struct HeartbeatMsg {
    NetMsgType type = NetMsgType::Heartbeat;
};

struct ResignMsg {
    NetMsgType type = NetMsgType::Resign;
};

#pragma pack(pop)

// ── Conversion Helpers ───────────────────────────────────────────────

inline Move netMsgToMove(const MoveNetMsg& msg) {
    Move m;
    m.from = Square(msg.fromCol, msg.fromRow);
    m.to = Square(msg.toCol, msg.toRow);
    m.promotion = static_cast<PieceType>(msg.promotion);
    m.isCastle = (msg.flags & 0x01) != 0;
    m.isEnPassant = (msg.flags & 0x02) != 0;
    return m;
}

inline MoveNetMsg moveToNetMsg(const Move& move, uint8_t seq) {
    MoveNetMsg msg;
    msg.seq = seq;
    msg.fromCol = move.from.col;
    msg.fromRow = move.from.row;
    msg.toCol = move.to.col;
    msg.toRow = move.to.row;
    msg.promotion = static_cast<uint8_t>(move.promotion);
    msg.flags = (move.isCastle ? 0x01 : 0x00) |
                (move.isEnPassant ? 0x02 : 0x00);
    return msg;
}

#endif // CHESS_NET_PROTOCOL_H
