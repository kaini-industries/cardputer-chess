#ifndef CHESS960_H
#define CHESS960_H

#include "chess_types.h"

// =====================================================================
// Chess960 (Fischer Random) position generation.
// Uses the standard index-based algorithm (0–959).
// Position 518 = standard chess starting position.
// =====================================================================

struct Chess960Position {
    PieceType backRank[8]; // Piece types for columns 0-7
    uint8_t kingCol;
    uint8_t rookKS;        // Kingside rook column (right of king)
    uint8_t rookQS;        // Queenside rook column (left of king)
};

// Generate a Chess960 position from an index (0-959).
// Index 518 produces the standard starting position.
Chess960Position chess960Generate(uint16_t index);

// Generate a random Chess960 position index (0-959) using esp_random().
uint16_t chess960RandomIndex();

#endif // CHESS960_H
