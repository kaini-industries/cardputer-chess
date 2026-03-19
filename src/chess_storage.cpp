#include "chess_storage.h"
#include <Preferences.h>
#include <cstring>

// ─── Binary Save Format ──────────────────────────────────────────────
//
// Version 1 layout:
//   [0]       version (0x01)
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
//   [78..]    history records (12 bytes each)
//
// Each MoveRecord packed as 12 bytes:
//   [0] from.col, [1] from.row
//   [2] to.col,   [3] to.row
//   [4] promotion (PieceType)
//   [5] flags: bit0=isCastle, bit1=isEnPassant
//   [6] captured type, [7] captured color
//   [8] capturedSquare.col, [9] capturedSquare.row
//   [10] prevCastleRights
//   [11] prevHalfmoveClock
//   [12] prevEnPassantTarget.col, [13] prevEnPassantTarget.row
//   (total 14 bytes per record)

static constexpr uint8_t SAVE_VERSION = 0x01;
static constexpr size_t HEADER_SIZE = 78;
static constexpr size_t RECORD_SIZE = 14;
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
}

void saveGame(const ChessBoard& board,
              const MoveRecord* history, uint8_t historyCount,
              bool historyOverflow,
              AIDifficulty aiDifficulty, PieceColor aiColor,
              PieceColor localColor, bool boardFlipped) {

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
    buf[70] = (uint8_t)(board.fullmoveNumber() & 0xFF);
    buf[71] = (uint8_t)(board.fullmoveNumber() >> 8);
    buf[72] = historyCount;
    buf[73] = historyOverflow ? 0x01 : 0x00;
    buf[74] = static_cast<uint8_t>(aiDifficulty);
    buf[75] = static_cast<uint8_t>(aiColor);
    buf[76] = static_cast<uint8_t>(localColor);
    buf[77] = boardFlipped ? 1 : 0;

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
              PieceColor& localColor, bool& boardFlipped) {

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true); // read-only
    size_t len = prefs.getBytesLength(NVS_KEY);

    if (len < HEADER_SIZE) {
        prefs.end();
        return false;
    }

    uint8_t stackBuf[512];
    uint8_t* buf = (len <= sizeof(stackBuf)) ? stackBuf : new uint8_t[len];
    prefs.getBytes(NVS_KEY, buf, len);
    prefs.end();

    // Version check
    if (buf[0] != SAVE_VERSION) {
        if (buf != stackBuf) delete[] buf;
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
    board.setFullmoveNumber((uint16_t)buf[70] | ((uint16_t)buf[71] << 8));

    historyCount = buf[72];
    historyOverflow = (buf[73] & 0x01) != 0;
    aiDifficulty = static_cast<AIDifficulty>(buf[74]);
    aiColor = static_cast<PieceColor>(buf[75]);
    localColor = static_cast<PieceColor>(buf[76]);
    boardFlipped = buf[77] != 0;

    // Validate history fits in buffer
    size_t expectedSize = HEADER_SIZE + (size_t)historyCount * RECORD_SIZE;
    if (len < expectedSize) {
        historyCount = 0;
    }

    // Unpack history records
    for (uint8_t i = 0; i < historyCount; i++) {
        unpackRecord(buf + HEADER_SIZE + (size_t)i * RECORD_SIZE, history[i]);
    }

    if (buf != stackBuf) delete[] buf;
    return true;
}

bool hasSave() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    size_t len = prefs.getBytesLength(NVS_KEY);
    prefs.end();
    return len >= HEADER_SIZE;
}

void clearSave() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.remove(NVS_KEY);
    prefs.end();
}

} // namespace ChessStorage
