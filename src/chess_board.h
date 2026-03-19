#ifndef CHESS_BOARD_H
#define CHESS_BOARD_H

#include "chess_types.h"

// =====================================================================
// ChessBoard: 8x8 board state with make/unmake move support.
// Pure chess logic — no UI dependencies.
// =====================================================================

class ChessBoard {
public:
    ChessBoard();

    // Reset to standard starting position
    void reset();

    // Board access
    Piece at(uint8_t col, uint8_t row) const;
    void  set(uint8_t col, uint8_t row, Piece p);

    // Game state
    PieceColor sideToMove() const { return m_sideToMove; }
    uint8_t    castleRights() const { return m_castleRights; }
    Square     enPassantTarget() const { return m_enPassantTarget; }
    uint16_t   fullmoveNumber() const { return m_fullmoveNumber; }
    uint8_t    halfmoveClock() const { return m_halfmoveClock; }

    bool canCastleKS(PieceColor c) const;
    bool canCastleQS(PieceColor c) const;

    // Execute a move. Returns a record for undoing it.
    MoveRecord makeMove(const Move& move);

    // Undo a move using its record.
    void unmakeMove(const MoveRecord& record);

    // Find the king's position for a given color
    Square findKing(PieceColor c) const;

    // Setters for loading saved state
    void setSideToMove(PieceColor c) { m_sideToMove = c; }
    void setCastleRights(uint8_t r) { m_castleRights = r; }
    void setEnPassantTarget(Square s) { m_enPassantTarget = s; }
    void setHalfmoveClock(uint8_t c) { m_halfmoveClock = c; }
    void setFullmoveNumber(uint16_t n) { m_fullmoveNumber = n; }

private:
    Piece      m_board[8][8];       // [row][col]
    PieceColor m_sideToMove;
    uint8_t    m_castleRights;      // 4-bit mask
    Square     m_enPassantTarget;   // NO_SQUARE if none
    uint8_t    m_halfmoveClock;
    uint16_t   m_fullmoveNumber;
};

#endif // CHESS_BOARD_H
