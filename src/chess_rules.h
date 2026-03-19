#ifndef CHESS_RULES_H
#define CHESS_RULES_H

#include "chess_board.h"

// =====================================================================
// Chess rules: move generation, attack detection, game-end conditions.
// Operates on ChessBoard — no UI dependencies.
// =====================================================================

namespace ChessRules {

    // Is the given square attacked by pieces of the specified color?
    // Uses reverse-lookup (no move generation needed).
    bool isAttacked(const ChessBoard& board, uint8_t col, uint8_t row, PieceColor byColor);

    // Is the given color's king in check?
    bool isInCheck(const ChessBoard& board, PieceColor c);

    // Generate all fully legal moves for the side to move.
    // Requires a mutable board reference (uses makeMove/unmakeMove internally).
    void generateLegal(ChessBoard& board, MoveList& out);

    // Generate legal moves originating from a specific square.
    void getLegalMovesFrom(ChessBoard& board, uint8_t col, uint8_t row, MoveList& out);

    // No legal moves AND in check
    bool isCheckmate(ChessBoard& board);

    // No legal moves AND NOT in check
    bool isStalemate(ChessBoard& board);

    // 50-move rule (100 half-moves without pawn move or capture)
    bool isDraw50Move(const ChessBoard& board);

    // Insufficient material (K vs K, K+B vs K, K+N vs K)
    // Always returns false for Atomic variant.
    bool isInsufficientMaterial(const ChessBoard& board);

    // Atomic: Is the given color's king missing (exploded)?
    bool isKingExploded(const ChessBoard& board, PieceColor c);

} // namespace ChessRules

#endif // CHESS_RULES_H
