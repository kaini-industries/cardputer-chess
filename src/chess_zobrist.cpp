#include "chess_zobrist.h"
#include <pgmspace.h>

// Deterministic 32-bit random numbers for Zobrist hashing.
// Generated from LFSR with seed 0x12345678.
// Index: piece_index * 64 + row * 8 + col
// piece_index: 0-11 (6 types x 2 colors: WP=0, BP=1, WN=2, BN=3, ...)

static uint32_t lfsr32(uint32_t& state) {
    // Galois LFSR with taps at 32, 22, 2, 1
    uint32_t bit = ((state >> 0) ^ (state >> 1) ^ (state >> 21) ^ (state >> 31)) & 1;
    state = (state >> 1) | (bit << 31);
    return state;
}

// Generate table on first use (saves PROGMEM vs storing 3KB+ of constants)
static uint32_t s_pieceTable[12 * 64]; // 12 piece types x 64 squares
static uint32_t s_sideToMove;
static uint32_t s_castleTable[16];
static uint32_t s_epFileTable[8]; // en passant file (a-h)
static bool s_initialized = false;

static void initTable() {
    if (s_initialized) return;
    s_initialized = true;

    uint32_t state = 0x12345678;
    for (int i = 0; i < 12 * 64; i++) {
        s_pieceTable[i] = lfsr32(state);
    }
    s_sideToMove = lfsr32(state);
    for (int i = 0; i < 16; i++) {
        s_castleTable[i] = lfsr32(state);
    }
    for (int i = 0; i < 8; i++) {
        s_epFileTable[i] = lfsr32(state);
    }
}

static int pieceIndex(PieceType type, PieceColor color) {
    // type: Pawn=1..King=6, color: White=0, Black=1
    // Map to 0-11: (type-1)*2 + color
    return (static_cast<int>(type) - 1) * 2 + static_cast<int>(color);
}

namespace ChessZobrist {

uint32_t hash(const ChessBoard& board) {
    initTable();

    uint32_t h = 0;

    for (uint8_t r = 0; r < 8; r++) {
        for (uint8_t c = 0; c < 8; c++) {
            Piece p = board.at(c, r);
            if (!p.empty()) {
                int idx = pieceIndex(p.type, p.color);
                h ^= s_pieceTable[idx * 64 + r * 8 + c];
            }
        }
    }

    if (board.sideToMove() == PieceColor::Black) {
        h ^= s_sideToMove;
    }

    h ^= s_castleTable[board.castleRights() & 0x0F];

    Square ep = board.enPassantTarget();
    if (!isNoSquare(ep)) {
        h ^= s_epFileTable[ep.col];
    }

    return h;
}

} // namespace ChessZobrist
