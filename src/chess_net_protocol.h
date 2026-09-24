#ifndef CHESS_NET_PROTOCOL_H
#define CHESS_NET_PROTOCOL_H

#include "chess_types.h"
#include "net_crypto.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

// =====================================================================
// Chess Network Protocol: packed ESP-NOW messages.
//
// Protocol v7. Discovery and AcceptGame carry public keys. Session packets
// end with a truncated HMAC tag. ResignMsg stays 10 bytes and is ignored;
// resignation is an authenticated GameEnd. Any other version is rejected.
// NetGameHeader is common to every session-scoped packet so stale games can
// be rejected before any payload is acted upon.
// =====================================================================

static constexpr uint8_t NET_PROTOCOL_VERSION = 7;
static constexpr uint8_t NET_PACKET_MAX_SIZE = 64;
static constexpr uint8_t NET_DISPLAY_NAME_MAX = 12;
static constexpr uint8_t NET_DISPLAY_NAME_BYTES = NET_DISPLAY_NAME_MAX + 1;

enum class NetMsgType : uint8_t {
    Discovery    = 0x01,
    AcceptGame   = 0x02,
    GameStart    = 0x03,
    GameStartAck = 0x04,
    MoveMsg      = 0x10,
    MoveAck      = 0x11,
    Heartbeat    = 0x20,
    Resign       = 0x30, // Legacy one-way control; prefer ControlMsg.
    ControlMsg   = 0x31,
    ControlAck   = 0x32,
};

enum class NetControlType : uint8_t {
    DrawOffer   = 0x01,
    DrawAccept  = 0x02,
    DrawDecline = 0x03,
    TimeGift    = 0x04,
    GameEnd     = 0x05,
};

enum class NetAckStatus : uint8_t {
    Accepted  = 0,
    Duplicate = 1,
    Rejected  = 2,
};

enum class NetPositionRelation : uint8_t {
    Match,
    PeerAheadOne,
    PeerBehind,
    Diverged,
};

enum class NetGameResult : uint8_t {
    None     = 0,
    WhiteWin = 1,
    BlackWin = 2,
    Draw     = 3,
};

enum class NetTermination : uint8_t {
    Unknown      = 0,
    Checkmate    = 1,
    Timeout      = 2,
    Resignation  = 3,
    Agreement    = 4,
    Stalemate    = 5,
    Repetition   = 6,
    FiftyMove    = 7,
    Insufficient = 8,
    Disconnect   = 9,
};

#pragma pack(push, 1)

// Shared by Discovery and AcceptGame. A non-zero gameId identifies one
// hosting attempt and prevents an old AcceptGame from joining a later lobby.
struct NetPairingHeader {
    NetMsgType type;
    uint8_t    version;
    uint16_t   gameId;

    constexpr NetPairingHeader(NetMsgType messageType = NetMsgType::Discovery,
                               uint16_t id = 0)
        : type(messageType), version(NET_PROTOCOL_VERSION), gameId(id) {}
};

// Shared by all in-game and game-start packets.
struct NetGameHeader {
    NetMsgType type;
    uint8_t    version;
    uint16_t   gameId;
    uint32_t   sessionId;

    constexpr NetGameHeader(NetMsgType messageType = NetMsgType::GameStart,
                            uint16_t game = 0,
                            uint32_t session = 0)
        : type(messageType), version(NET_PROTOCOL_VERSION), gameId(game),
          sessionId(session) {}
};

// ── Discovery & Pairing ──────────────────────────────────────────────

struct DiscoveryMsg {
    NetPairingHeader header = {NetMsgType::Discovery};
    uint8_t  variant = 0;          // ChessVariant as uint8_t
    uint16_t positionIndex = 518;  // Chess960 position (518 = standard)
    uint8_t  timeControl = 0;      // TimeControl as uint8_t
    char     displayName[NET_DISPLAY_NAME_BYTES] = {};
    uint8_t  publicKey[32] = {};
};

struct AcceptGameMsg {
    NetPairingHeader header = {NetMsgType::AcceptGame};
    char displayName[NET_DISPLAY_NAME_BYTES] = {};
    uint8_t publicKey[32] = {};
};

struct GameStartMsg {
    NetGameHeader header = {NetMsgType::GameStart};
    uint8_t  yourColor = 0;       // 0 = White, 1 = Black
    uint8_t  variant = 0;         // ChessVariant as uint8_t
    uint16_t positionIndex = 518;
    uint8_t  timeControl = 0;     // TimeControl as uint8_t
    uint32_t tag = 0;
};

struct GameStartAckMsg {
    NetGameHeader header = {NetMsgType::GameStartAck};
    uint32_t tag = 0;
};

// ── Gameplay ─────────────────────────────────────────────────────────

struct MoveNetMsg {
    NetGameHeader header = {NetMsgType::MoveMsg};
    uint16_t sequence = 0;        // Independent move sequence for ack/dedup
    uint8_t  fromCol = 0;
    uint8_t  fromRow = 0;
    uint8_t  toCol = 0;
    uint8_t  toRow = 0;
    uint8_t  promotion = 0;       // PieceType as uint8_t (0 = None)
    uint8_t  flags = 0;           // bit 0 = castle, bit 1 = en passant
    uint32_t moverRemainingMs = 0;
    uint32_t preBoardHash = 0;
    uint32_t postBoardHash = 0;
    uint32_t tag = 0;
};

struct MoveAckMsg {
    NetGameHeader header = {NetMsgType::MoveAck};
    uint16_t sequence = 0;
    NetAckStatus status = NetAckStatus::Accepted;
    uint32_t boardHash = 0;
    uint32_t tag = 0;
};

struct HeartbeatMsg {
    NetGameHeader header = {NetMsgType::Heartbeat};
    uint8_t  activeColor = 0;
    uint32_t activeRemainingMs = 0;
    uint16_t positionEpoch = 0;
    uint16_t lastAppliedSequence = 0;
    uint32_t boardHash = 0;
    uint32_t tag = 0;
};

// Legacy one-way packet. Protocol v7 ignores Resign completely. Resignation
// is a MACed Control GameEnd. This struct stays 10 bytes and is not tagged.
struct ResignMsg {
    NetGameHeader header = {NetMsgType::Resign};
    uint16_t eventId = 0;
};

// Reliable, session-scoped control event. Payload interpretation:
//   DrawOffer:   eventId only
//   DrawAccept / DrawDecline: relatedEventId identifies the offer
//   TimeGift:    arg0 target color, value amount; the receiver applies the
//                amount to its locally authoritative clock
//   GameEnd:     arg0 NetGameResult, arg1 NetTermination; each receiver keeps
//                its local clock and imports only the sender's snapshot.
//                Agreement uses relatedEventId as a receipt for DrawAccept;
//                other termination types require it to be zero.
struct ControlNetMsg {
    NetGameHeader header = {NetMsgType::ControlMsg};
    uint16_t eventId = 0;
    NetControlType control = NetControlType::DrawOffer;
    uint8_t  arg0 = 0;
    uint8_t  arg1 = 0;
    uint16_t relatedEventId = 0;
    uint32_t valueMs = 0;
    uint32_t whiteRemainingMs = 0;
    uint32_t blackRemainingMs = 0;
    uint32_t boardHash = 0;
    uint32_t tag = 0;
};

struct ControlAckMsg {
    NetGameHeader header = {NetMsgType::ControlAck};
    uint16_t eventId = 0;
    NetControlType control = NetControlType::DrawOffer;
    NetAckStatus status = NetAckStatus::Accepted;
    uint32_t boardHash = 0;
    // TimeGift ACKs carry the receiver's authoritative post-gift clock.
    // Other control acknowledgements leave this field at zero.
    uint32_t clockRemainingMs = 0;
    uint32_t tag = 0;
};

#pragma pack(pop)

// ── Wire-format guards ───────────────────────────────────────────────

static_assert(sizeof(NetPairingHeader) == 4, "Unexpected pairing header size");
static_assert(sizeof(NetGameHeader) == 8, "Unexpected game header size");
static_assert(offsetof(DiscoveryMsg, header) == 0, "Header must be first");
static_assert(offsetof(AcceptGameMsg, header) == 0, "Header must be first");
static_assert(offsetof(GameStartMsg, header) == 0, "Header must be first");
static_assert(offsetof(GameStartAckMsg, header) == 0, "Header must be first");
static_assert(offsetof(MoveNetMsg, header) == 0, "Header must be first");
static_assert(offsetof(MoveAckMsg, header) == 0, "Header must be first");
static_assert(offsetof(HeartbeatMsg, header) == 0, "Header must be first");
static_assert(offsetof(ResignMsg, header) == 0, "Header must be first");
static_assert(offsetof(ControlNetMsg, header) == 0, "Header must be first");
static_assert(offsetof(ControlAckMsg, header) == 0, "Header must be first");
static_assert(sizeof(DiscoveryMsg) == 53, "Unexpected Discovery layout");
static_assert(sizeof(AcceptGameMsg) == 49, "Unexpected AcceptGame layout");
static_assert(sizeof(GameStartMsg) == 17, "Unexpected GameStart layout");
static_assert(sizeof(GameStartAckMsg) == 12, "Unexpected GameStartAck layout");
static_assert(sizeof(MoveNetMsg) == 32, "Unexpected Move layout");
static_assert(sizeof(MoveAckMsg) == 19, "Unexpected MoveAck layout");
static_assert(sizeof(HeartbeatMsg) == 25, "Unexpected Heartbeat layout");
static_assert(sizeof(ResignMsg) == 10, "Unexpected Resign layout");
static_assert(sizeof(ControlNetMsg) == 35, "Unexpected Control layout");
static_assert(sizeof(ControlAckMsg) == 24, "Unexpected ControlAck layout");
static_assert(offsetof(DiscoveryMsg, publicKey) == 21, "publicKey must trail");
static_assert(offsetof(AcceptGameMsg, publicKey) == 17, "publicKey must trail");
static_assert(offsetof(GameStartMsg, tag) == 13, "tag must trail");
static_assert(offsetof(GameStartAckMsg, tag) == 8, "tag must trail");
static_assert(offsetof(MoveNetMsg, tag) == 28, "tag must trail");
static_assert(offsetof(MoveAckMsg, tag) == 15, "tag must trail");
static_assert(offsetof(HeartbeatMsg, tag) == 21, "tag must trail");
static_assert(offsetof(ControlNetMsg, tag) == 31, "tag must trail");
static_assert(offsetof(ControlAckMsg, tag) == 20, "tag must trail");
static_assert(sizeof(DiscoveryMsg) <= NET_PACKET_MAX_SIZE,
              "Largest protocol packet exceeds transport capacity");
static_assert(sizeof(AcceptGameMsg) <= NET_PACKET_MAX_SIZE, "AcceptGame too large");
static_assert(sizeof(GameStartMsg) <= NET_PACKET_MAX_SIZE, "GameStart too large");
static_assert(sizeof(GameStartAckMsg) <= NET_PACKET_MAX_SIZE, "GameStartAck too large");
static_assert(sizeof(MoveNetMsg) <= NET_PACKET_MAX_SIZE, "Move too large");
static_assert(sizeof(MoveAckMsg) <= NET_PACKET_MAX_SIZE, "MoveAck too large");
static_assert(sizeof(HeartbeatMsg) <= NET_PACKET_MAX_SIZE, "Heartbeat too large");
static_assert(sizeof(ResignMsg) <= NET_PACKET_MAX_SIZE, "Resign too large");
static_assert(sizeof(ControlNetMsg) <= NET_PACKET_MAX_SIZE, "Control too large");
static_assert(sizeof(ControlAckMsg) <= NET_PACKET_MAX_SIZE, "ControlAck too large");

// Stamp or check the trailing host-endian tag. packetTag covers every byte
// before those four. Discovery, AcceptGame, and ResignMsg are not tagged.
inline void stampSessionPacket(void* packet, size_t size,
                               const uint8_t macKey[NetCrypto::KEY_SIZE]) {
    if (packet == nullptr || macKey == nullptr || size < sizeof(uint32_t)) return;
    auto* bytes = static_cast<uint8_t*>(packet);
    const size_t prefix = size - sizeof(uint32_t);
    const uint32_t tag = NetCrypto::packetTag(macKey, bytes, prefix);
    std::memcpy(bytes + prefix, &tag, sizeof(tag));
}

inline bool sessionPacketTagMatches(const void* packet, size_t size,
                                    const uint8_t macKey[NetCrypto::KEY_SIZE]) {
    if (packet == nullptr || macKey == nullptr || size < sizeof(uint32_t)) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(packet);
    const size_t prefix = size - sizeof(uint32_t);
    uint32_t tag = 0;
    std::memcpy(&tag, bytes + prefix, sizeof(tag));
    return NetCrypto::tagsEqual(tag, NetCrypto::packetTag(macKey, bytes, prefix));
}

// ── Validation / conversion helpers ──────────────────────────────────

inline bool isValidNetDisplayName(const char name[NET_DISPLAY_NAME_BYTES]) {
    for (uint8_t i = 0; i < NET_DISPLAY_NAME_BYTES; ++i) {
        const uint8_t ch = static_cast<uint8_t>(name[i]);
        if (ch == 0) return true;
        if (i == NET_DISPLAY_NAME_MAX || ch < 0x20 || ch > 0x7E) return false;
    }
    return false;
}

inline void copyNetDisplayName(char dest[NET_DISPLAY_NAME_BYTES],
                               const char* source) {
    uint8_t out = 0;
    if (source != nullptr) {
        while (out < NET_DISPLAY_NAME_MAX && source[out] != '\0') {
            const uint8_t ch = static_cast<uint8_t>(source[out]);
            dest[out] = (ch >= 0x20 && ch <= 0x7E) ? static_cast<char>(ch) : '?';
            ++out;
        }
    }
    dest[out] = '\0';
    for (uint8_t i = out + 1; i < NET_DISPLAY_NAME_BYTES; ++i) dest[i] = '\0';
}

inline bool isValidPairingHeader(const NetPairingHeader& header,
                                 NetMsgType expectedType,
                                 uint16_t expectedGameId = 0) {
    return header.type == expectedType &&
           header.version == NET_PROTOCOL_VERSION &&
           header.gameId != 0 &&
           (expectedGameId == 0 || header.gameId == expectedGameId);
}

inline bool isValidGameHeader(const NetGameHeader& header,
                              NetMsgType expectedType,
                              uint16_t expectedGameId,
                              uint32_t expectedSessionId = 0) {
    return header.type == expectedType &&
           header.version == NET_PROTOCOL_VERSION &&
           header.gameId != 0 &&
           header.gameId == expectedGameId &&
           header.sessionId != 0 &&
           (expectedSessionId == 0 || header.sessionId == expectedSessionId);
}

inline bool isValidChessVariantValue(uint8_t value) {
    return value <= static_cast<uint8_t>(ChessVariant::Chess960);
}

inline bool isValidTimeControlValue(uint8_t value) {
    return value <= static_cast<uint8_t>(TimeControl::Rapid10);
}

inline bool isValidColorValue(uint8_t value) {
    return value <= static_cast<uint8_t>(PieceColor::Black);
}

inline bool isValidPositionIndex(uint8_t variant, uint16_t positionIndex) {
    if (!isValidChessVariantValue(variant)) return false;
    return variant == static_cast<uint8_t>(ChessVariant::Standard)
        ? positionIndex == 518
        : positionIndex < 960;
}

// Classify a peer's live-position epoch using serial-number arithmetic. Equal
// epochs are comparable only when their board hashes also match. A peer may be
// exactly one position ahead while its Move packet is still in flight; older
// peer epochs are stale. Larger forward jumps and the ambiguous half-range are
// protocol divergence.
inline NetPositionRelation classifyNetPosition(uint16_t localEpoch,
                                               uint32_t localBoardHash,
                                               uint16_t peerEpoch,
                                               uint32_t peerBoardHash) {
    if (peerEpoch == localEpoch) {
        return peerBoardHash == localBoardHash
            ? NetPositionRelation::Match
            : NetPositionRelation::Diverged;
    }

    const uint16_t peerAhead = static_cast<uint16_t>(peerEpoch - localEpoch);
    if (peerAhead == 1) return NetPositionRelation::PeerAheadOne;
    if (peerAhead < 0x8000u) return NetPositionRelation::Diverged;

    const uint16_t localAhead = static_cast<uint16_t>(localEpoch - peerEpoch);
    return localAhead < 0x8000u
        ? NetPositionRelation::PeerBehind
        : NetPositionRelation::Diverged;
}

// A successful timed move can never report zero: reaching zero flags before
// the move is committed. Untimed games use zero as their canonical wire value.
inline bool isValidMoveClockValue(bool timed, uint32_t moverRemainingMs) {
    return timed ? moverRemainingMs > 0 : moverRemainingMs == 0;
}

inline Move netMsgToMove(const MoveNetMsg& msg) {
    Move move;
    move.from = Square(msg.fromCol, msg.fromRow);
    move.to = Square(msg.toCol, msg.toRow);
    move.promotion = static_cast<PieceType>(msg.promotion);
    move.isCastle = (msg.flags & 0x01) != 0;
    move.isEnPassant = (msg.flags & 0x02) != 0;
    return move;
}

inline MoveNetMsg moveToNetMsg(const Move& move,
                               uint16_t sequence,
                               uint16_t gameId,
                               uint32_t sessionId,
                               uint32_t moverRemainingMs,
                               uint32_t preBoardHash,
                               uint32_t postBoardHash) {
    MoveNetMsg msg;
    msg.header.gameId = gameId;
    msg.header.sessionId = sessionId;
    msg.sequence = sequence;
    msg.fromCol = move.from.col;
    msg.fromRow = move.from.row;
    msg.toCol = move.to.col;
    msg.toRow = move.to.row;
    msg.promotion = static_cast<uint8_t>(move.promotion);
    msg.flags = (move.isCastle ? 0x01 : 0x00) |
                (move.isEnPassant ? 0x02 : 0x00);
    msg.moverRemainingMs = moverRemainingMs;
    msg.preBoardHash = preBoardHash;
    msg.postBoardHash = postBoardHash;
    return msg;
}

#endif // CHESS_NET_PROTOCOL_H
