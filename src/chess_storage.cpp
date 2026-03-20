#include "chess_storage.h"
#include <Preferences.h>
#include <cstring>

// ─── Binary Save Format ──────────────────────────────────────────────
//
// Version 3 layout (extends v2 with 960 position and timer):
//   [0]       version (0x03)
//   [1..64]   board squares — 1 byte each: (type << 4) | (color << 0)
//   [65]      sideToMove
//   [66]      castleRights
//   [67..68]  enPassantTarget (col, row)
//   [69]      halfmoveClock
//   [70..71]  fullmoveNumber (little-endian uint16)
//   [72]      historyCount
//   [73]      flags: bit0 = historyOverflow
//   [74]      aiDifficulty
//   [75]      aiColor
//   [76]      localColor
//   [77]      boardFlipped
//   [78]      variant (ChessVariant)
//   [79..80]  positionIndex (little-endian uint16)
//   [81]      timeControl (TimeControl)
//   [82..85]  timeWhiteMs (little-endian uint32)
//   [86..89]  timeBlackMs (little-endian uint32)
//   [90]      timerRunning
//   [91..]    history records (33 bytes each)
//
// Each MoveRecord packed as 33 bytes:
//   [0]  from.col, [1]  from.row
//   [2]  to.col,   [3]  to.row
//   [4]  promotion (PieceType)
//   [5]  flags: bit0=isCastle, bit1=isEnPassant
//   [6]  captured type, [7] captured color
//   [8]  capturedSquare.col, [9] capturedSquare.row
//   [10] prevCastleRights
//   [11] prevHalfmoveClock
//   [12] prevEnPassantTarget.col, [13] prevEnPassantTarget.row
//   [14] movedPiece type, [15] movedPiece color
//   [16..31] exploded[0..7] — 2 bytes each (type, color)
//   [32] explodedMask

static constexpr uint8_t SAVE_VERSION_V1 = 0x01;
static constexpr uint8_t SAVE_VERSION_V2 = 0x02;
static constexpr uint8_t SAVE_VERSION_V3 = 0x03;
static constexpr uint8_t SAVE_VERSION = SAVE_VERSION_V3;

static constexpr size_t HEADER_SIZE_V1 = 78;
static constexpr size_t RECORD_SIZE_V1 = 14;

static constexpr size_t HEADER_SIZE_V2 = 79;
static constexpr size_t RECORD_SIZE_V2 = 33;

static constexpr size_t HEADER_SIZE = 91;
static constexpr size_t RECORD_SIZE = 33;

static constexpr const char* NVS_NAMESPACE = "chess";
static constexpr const char* NVS_KEY = "game";

namespace ChessStorage {

static void packRecord(uint8_t* dst, const MoveRecord& rec) {
    dst[0] = rec.move.from.col;
    dst[1] = rec.move.from.row;
    dst[2] = rec.move.to.col;
    dst[3] = rec.move.to.row;
    dst[4] = static_cast<uint8_t>(rec.move.promotion);
    dst[5] = (rec.move.isCastle ? 0x01 : 0) | (rec.move.isEnPassant ? 0x02 : 0);
    dst[6] = static_cast<uint8_t>(rec.captured.type);
    dst[7] = static_cast<uint8_t>(rec.captured.color);
    dst[8] = rec.capturedSquare.col;
    dst[9] = rec.capturedSquare.row;
    dst[10] = rec.prevCastleRights;
    dst[11] = rec.prevHalfmoveClock;
    dst[12] = rec.prevEnPassantTarget.col;
    dst[13] = rec.prevEnPassantTarget.row;
    dst[14] = static_cast<uint8_t>(rec.movedPiece.type);
    dst[15] = static_cast<uint8_t>(rec.movedPiece.color);
    for (int i = 0; i < 8; i++) {
        dst[16 + i * 2]     = static_cast<uint8_t>(rec.exploded[i].type);
        dst[16 + i * 2 + 1] = static_cast<uint8_t>(rec.exploded[i].color);
    }
    dst[32] = rec.explodedMask;
}

static void unpackRecord(const uint8_t* src, MoveRecord& rec) {
    rec.move.from = makeSquare(src[0], src[1]);
    rec.move.to = makeSquare(src[2], src[3]);
    rec.move.promotion = static_cast<PieceType>(src[4]);
    rec.move.isCastle = (src[5] & 0x01) != 0;
    rec.move.isEnPassant = (src[5] & 0x02) != 0;
    rec.captured = Piece(static_cast<PieceType>(src[6]),
                         static_cast<PieceColor>(src[7]));
    rec.capturedSquare = makeSquare(src[8], src[9]);
    rec.prevCastleRights = src[10];
    rec.prevHalfmoveClock = src[11];
    rec.prevEnPassantTarget = makeSquare(src[12], src[13]);
    rec.movedPiece = Piece(static_cast<PieceType>(src[14]),
                           static_cast<PieceColor>(src[15]));
    for (int i = 0; i < 8; i++) {
        rec.exploded[i] = Piece(static_cast<PieceType>(src[16 + i * 2]),
                                static_cast<PieceColor>(src[16 + i * 2 + 1]));
    }
    rec.explodedMask = src[32];
}

// V1 unpacker for backward compatibility
static void unpackRecordV1(const uint8_t* src, MoveRecord& rec) {
    rec.move.from = makeSquare(src[0], src[1]);
    rec.move.to = makeSquare(src[2], src[3]);
    rec.move.promotion = static_cast<PieceType>(src[4]);
    rec.move.isCastle = (src[5] & 0x01) != 0;
    rec.move.isEnPassant = (src[5] & 0x02) != 0;
    rec.captured = Piece(static_cast<PieceType>(src[6]),
                         static_cast<PieceColor>(src[7]));
    rec.capturedSquare = makeSquare(src[8], src[9]);
    rec.prevCastleRights = src[10];
    rec.prevHalfmoveClock = src[11];
    rec.prevEnPassantTarget = makeSquare(src[12], src[13]);
    rec.movedPiece = Piece{}; // Not stored in v1
    rec.explodedMask = 0;    // No atomic data in v1
}

static inline void writeU16LE(uint8_t* dst, uint16_t val) {
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)(val >> 8);
}

static inline void writeU32LE(uint8_t* dst, uint32_t val) {
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
    dst[2] = (uint8_t)((val >> 16) & 0xFF);
    dst[3] = (uint8_t)((val >> 24) & 0xFF);
}

static inline uint16_t readU16LE(const uint8_t* src) {
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static inline uint32_t readU32LE(const uint8_t* src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

void saveGame(const ChessBoard& board,
              const MoveRecord* history, uint8_t historyCount,
              bool historyOverflow,
              AIDifficulty aiDifficulty, PieceColor aiColor,
              PieceColor localColor, bool boardFlipped,
              ChessVariant variant, uint16_t positionIndex,
              TimeControl timeControl, uint32_t timeWhiteMs,
              uint32_t timeBlackMs, bool timerRunning) {

    size_t totalSize = HEADER_SIZE + (size_t)historyCount * RECORD_SIZE;

    // Use stack buffer for small saves, heap for large ones
    uint8_t stackBuf[512];
    uint8_t* buf = (totalSize <= sizeof(stackBuf)) ? stackBuf : new uint8_t[totalSize];

    // Header
    buf[0] = SAVE_VERSION;

    // Board squares
    for (uint8_t r = 0; r < 8; r++) {
        for (uint8_t c = 0; c < 8; c++) {
            Piece p = board.at(c, r);
            buf[1 + r * 8 + c] = (static_cast<uint8_t>(p.type) << 4) |
                                   static_cast<uint8_t>(p.color);
        }
    }

    buf[65] = static_cast<uint8_t>(board.sideToMove());
    buf[66] = board.castleRights();
    buf[67] = board.enPassantTarget().col;
    buf[68] = board.enPassantTarget().row;
    buf[69] = board.halfmoveClock();
    writeU16LE(buf + 70, board.fullmoveNumber());
    buf[72] = historyCount;
    buf[73] = historyOverflow ? 0x01 : 0x00;
    buf[74] = static_cast<uint8_t>(aiDifficulty);
    buf[75] = static_cast<uint8_t>(aiColor);
    buf[76] = static_cast<uint8_t>(localColor);
    buf[77] = boardFlipped ? 1 : 0;
    buf[78] = static_cast<uint8_t>(variant);
    writeU16LE(buf + 79, positionIndex);
    buf[81] = static_cast<uint8_t>(timeControl);
    writeU32LE(buf + 82, timeWhiteMs);
    writeU32LE(buf + 86, timeBlackMs);
    buf[90] = timerRunning ? 1 : 0;

    // History records
    for (uint8_t i = 0; i < historyCount; i++) {
        packRecord(buf + HEADER_SIZE + (size_t)i * RECORD_SIZE, history[i]);
    }

    // Write to NVS
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBytes(NVS_KEY, buf, totalSize);
    prefs.end();

    if (buf != stackBuf) delete[] buf;
}

bool loadGame(ChessBoard& board,
              MoveRecord* history, uint8_t& historyCount,
              bool& historyOverflow,
              AIDifficulty& aiDifficulty, PieceColor& aiColor,
              PieceColor& localColor, bool& boardFlipped,
              ChessVariant& variant, uint16_t& positionIndex,
              TimeControl& timeControl, uint32_t& timeWhiteMs,
              uint32_t& timeBlackMs, bool& timerRunning) {

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true); // read-only
    size_t len = prefs.getBytesLength(NVS_KEY);

    // Need at least the v1 header to proceed
    if (len < HEADER_SIZE_V1) {
        prefs.end();
        return false;
    }

    // Allocate buffer large enough
    uint8_t* buf = new uint8_t[len];
    prefs.getBytes(NVS_KEY, buf, len);
    prefs.end();

    uint8_t version = buf[0];
    if (version != SAVE_VERSION_V1 && version != SAVE_VERSION_V2 &&
        version != SAVE_VERSION_V3) {
        delete[] buf;
        return false;
    }

    // Board squares
    board.reset(); // Start clean, then overwrite
    for (uint8_t r = 0; r < 8; r++) {
        for (uint8_t c = 0; c < 8; c++) {
            uint8_t packed = buf[1 + r * 8 + c];
            PieceType type = static_cast<PieceType>(packed >> 4);
            PieceColor color = static_cast<PieceColor>(packed & 0x0F);
            board.set(c, r, (type == PieceType::None) ? Piece{} : Piece(type, color));
        }
    }

    board.setSideToMove(static_cast<PieceColor>(buf[65]));
    board.setCastleRights(buf[66]);
    board.setEnPassantTarget(makeSquare(buf[67], buf[68]));
    board.setHalfmoveClock(buf[69]);
    board.setFullmoveNumber(readU16LE(buf + 70));

    historyCount = buf[72];
    historyOverflow = (buf[73] & 0x01) != 0;
    aiDifficulty = static_cast<AIDifficulty>(buf[74]);
    aiColor = static_cast<PieceColor>(buf[75]);
    localColor = static_cast<PieceColor>(buf[76]);
    boardFlipped = buf[77] != 0;

    // Variant (v2+)
    if (version >= SAVE_VERSION_V2) {
        variant = static_cast<ChessVariant>(buf[78]);
    } else {
        variant = ChessVariant::Standard;
    }

    // Position index and timer (v3+)
    if (version >= SAVE_VERSION_V3 && len >= HEADER_SIZE) {
        positionIndex = readU16LE(buf + 79);
        timeControl = static_cast<TimeControl>(buf[81]);
        timeWhiteMs = readU32LE(buf + 82);
        timeBlackMs = readU32LE(buf + 86);
        timerRunning = buf[90] != 0;
    } else {
        positionIndex = 518;
        timeControl = TimeControl::None;
        timeWhiteMs = 0;
        timeBlackMs = 0;
        timerRunning = false;
    }

    // Determine record layout based on version
    size_t headerSize, recordSize;
    if (version >= SAVE_VERSION_V3) {
        headerSize = HEADER_SIZE;
        recordSize = RECORD_SIZE;
    } else if (version == SAVE_VERSION_V2) {
        headerSize = HEADER_SIZE_V2;
        recordSize = RECORD_SIZE_V2;
    } else {
        headerSize = HEADER_SIZE_V1;
        recordSize = RECORD_SIZE_V1;
    }

    // Validate history fits in buffer
    size_t expectedSize = headerSize + (size_t)historyCount * recordSize;
    if (len < expectedSize) {
        historyCount = 0;
    }

    // Unpack history records
    for (uint8_t i = 0; i < historyCount; i++) {
        if (version >= SAVE_VERSION_V2) {
            unpackRecord(buf + headerSize + (size_t)i * recordSize, history[i]);
        } else {
            unpackRecordV1(buf + headerSize + (size_t)i * recordSize, history[i]);
        }
    }

    delete[] buf;
    return true;
}

bool hasSave() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    size_t len = prefs.getBytesLength(NVS_KEY);
    prefs.end();
    return len >= HEADER_SIZE_V1; // Accept v1, v2, and v3
}

void clearSave() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.remove(NVS_KEY);
    prefs.end();
}

} // namespace ChessStorage
