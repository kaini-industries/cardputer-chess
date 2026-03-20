#ifndef CHESS_ZOBRIST_H
#define CHESS_ZOBRIST_H

#include "chess_board.h"
#include <cstdint>

// =====================================================================
// Zobrist hashing for position identification.
// Uses 32-bit hashes with deterministic PROGMEM random table.
// =====================================================================

namespace ChessZobrist {
    // Compute full hash of a board position
    uint32_t hash(const ChessBoard& board);
}

#endif // CHESS_ZOBRIST_H
