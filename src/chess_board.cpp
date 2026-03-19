#include "chess_board.h"

ChessBoard::ChessBoard() {
    reset();
}

void ChessBoard::reset() {
    // Clear the board
    memset(m_board, 0, sizeof(m_board));

    // White pieces (row 0 = rank 1)
    m_board[0][0] = makePiece(PieceType::Rook,   PieceColor::White);
    m_board[0][1] = makePiece(PieceType::Knight, PieceColor::White);
    m_board[0][2] = makePiece(PieceType::Bishop, PieceColor::White);
    m_board[0][3] = makePiece(PieceType::Queen,  PieceColor::White);
    m_board[0][4] = makePiece(PieceType::King,   PieceColor::White);
    m_board[0][5] = makePiece(PieceType::Bishop, PieceColor::White);
    m_board[0][6] = makePiece(PieceType::Knight, PieceColor::White);
    m_board[0][7] = makePiece(PieceType::Rook,   PieceColor::White);
    for (uint8_t c = 0; c < 8; c++) {
        m_board[1][c] = makePiece(PieceType::Pawn, PieceColor::White);
    }

    // Black pieces (row 7 = rank 8)
    m_board[7][0] = makePiece(PieceType::Rook,   PieceColor::Black);
    m_board[7][1] = makePiece(PieceType::Knight, PieceColor::Black);
    m_board[7][2] = makePiece(PieceType::Bishop, PieceColor::Black);
    m_board[7][3] = makePiece(PieceType::Queen,  PieceColor::Black);
    m_board[7][4] = makePiece(PieceType::King,   PieceColor::Black);
    m_board[7][5] = makePiece(PieceType::Bishop, PieceColor::Black);
    m_board[7][6] = makePiece(PieceType::Knight, PieceColor::Black);
    m_board[7][7] = makePiece(PieceType::Rook,   PieceColor::Black);
    for (uint8_t c = 0; c < 8; c++) {
        m_board[6][c] = makePiece(PieceType::Pawn, PieceColor::Black);
    }

    m_sideToMove     = PieceColor::White;
    m_castleRights   = CastleRights::All;
    m_enPassantTarget = NO_SQUARE;
    m_halfmoveClock  = 0;
    m_fullmoveNumber = 1;
}

Piece ChessBoard::at(uint8_t col, uint8_t row) const {
    if (col >= 8 || row >= 8) return Piece{};
    return m_board[row][col];
}

void ChessBoard::set(uint8_t col, uint8_t row, Piece p) {
    if (col < 8 && row < 8) {
        m_board[row][col] = p;
    }
}

bool ChessBoard::canCastleKS(PieceColor c) const {
    return (c == PieceColor::White)
        ? (m_castleRights & CastleRights::WhiteKing)
        : (m_castleRights & CastleRights::BlackKing);
}

bool ChessBoard::canCastleQS(PieceColor c) const {
    return (c == PieceColor::White)
        ? (m_castleRights & CastleRights::WhiteQueen)
        : (m_castleRights & CastleRights::BlackQueen);
}

Square ChessBoard::findKing(PieceColor c) const {
    for (uint8_t r = 0; r < 8; r++) {
        for (uint8_t col = 0; col < 8; col++) {
            if (m_board[r][col].type == PieceType::King &&
                m_board[r][col].color == c) {
                return makeSquare(col, r);
            }
        }
    }
    return NO_SQUARE; // Should never happen in a valid game
}

// Neighbor offsets for atomic explosion (matches KING_OFFS in chess_rules.cpp)
static const int8_t ATOMIC_OFFS[8][2] = {
    {-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}
};

MoveRecord ChessBoard::makeMove(const Move& move) {
    MoveRecord record;
    record.move = move;
    record.prevCastleRights   = m_castleRights;
    record.prevEnPassantTarget = m_enPassantTarget;
    record.prevHalfmoveClock  = m_halfmoveClock;
    record.explodedMask = 0;

    Piece movingPiece = at(move.from.col, move.from.row);
    record.movedPiece = movingPiece;

    // Determine captured piece and its square
    if (move.isEnPassant) {
        // Captured pawn is on the same row as the moving pawn, same col as destination
        uint8_t capturedRow = move.from.row; // The pawn being captured is on the same rank we came from
        record.captured = at(move.to.col, capturedRow);
        record.capturedSquare = makeSquare(move.to.col, capturedRow);
        // Remove the captured pawn
        set(move.to.col, capturedRow, Piece{});
    } else {
        record.captured = at(move.to.col, move.to.row);
        record.capturedSquare = move.to;
    }

    // Move the piece
    set(move.to.col, move.to.row, movingPiece);
    set(move.from.col, move.from.row, Piece{});

    // Handle promotion
    if (move.promotion != PieceType::None) {
        set(move.to.col, move.to.row, makePiece(move.promotion, movingPiece.color));
    }

    // Handle castling — move the rook
    if (move.isCastle) {
        uint8_t row = move.from.row;
        if (move.to.col > move.from.col) {
            // Kingside: rook from h-file to f-file
            Piece rook = at(7, row);
            set(5, row, rook);
            set(7, row, Piece{});
        } else {
            // Queenside: rook from a-file to d-file
            Piece rook = at(0, row);
            set(3, row, rook);
            set(0, row, Piece{});
        }
    }

    // Atomic explosion: destroy all pieces in 3x3 area around capture square
    if (m_variant == ChessVariant::Atomic && !record.captured.empty()) {
        // Record and destroy all 8 neighbors of the capture square
        for (int i = 0; i < 8; i++) {
            int8_t nc = (int8_t)move.to.col + ATOMIC_OFFS[i][0];
            int8_t nr = (int8_t)move.to.row + ATOMIC_OFFS[i][1];
            if (nc >= 0 && nc < 8 && nr >= 0 && nr < 8) {
                Piece neighbor = at(nc, nr);
                if (!neighbor.empty()) {
                    record.exploded[i] = neighbor;
                    record.explodedMask |= (1 << i);
                    set(nc, nr, Piece{});
                }
            }
        }
        // Destroy the capturing piece at move.to (it exploded)
        set(move.to.col, move.to.row, Piece{});
    }

    // Update en passant target
    m_enPassantTarget = NO_SQUARE;
    if (movingPiece.type == PieceType::Pawn) {
        int8_t rowDiff = (int8_t)move.to.row - (int8_t)move.from.row;
        if (rowDiff == 2 || rowDiff == -2) {
            // Pawn moved two squares — set en passant target to the square it passed through
            uint8_t epRow = (move.from.row + move.to.row) / 2;
            m_enPassantTarget = makeSquare(move.from.col, epRow);
        }
    }

    // Update castle rights
    // King moves
    if (movingPiece.type == PieceType::King) {
        if (movingPiece.color == PieceColor::White) {
            m_castleRights &= ~CastleRights::WhiteAll;
        } else {
            m_castleRights &= ~CastleRights::BlackAll;
        }
    }
    // Rook moves or is captured
    auto clearRookRights = [&](uint8_t col, uint8_t row) {
        if (row == 0 && col == 0) m_castleRights &= ~CastleRights::WhiteQueen;
        if (row == 0 && col == 7) m_castleRights &= ~CastleRights::WhiteKing;
        if (row == 7 && col == 0) m_castleRights &= ~CastleRights::BlackQueen;
        if (row == 7 && col == 7) m_castleRights &= ~CastleRights::BlackKing;
    };
    clearRookRights(move.from.col, move.from.row);
    clearRookRights(move.to.col, move.to.row);
    // Atomic: clear castle rights for rooks destroyed in explosion
    if (record.explodedMask != 0) {
        for (int i = 0; i < 8; i++) {
            if (record.explodedMask & (1 << i)) {
                int8_t nc = (int8_t)move.to.col + ATOMIC_OFFS[i][0];
                int8_t nr = (int8_t)move.to.row + ATOMIC_OFFS[i][1];
                clearRookRights((uint8_t)nc, (uint8_t)nr);
            }
        }
    }

    // Update halfmove clock
    if (movingPiece.type == PieceType::Pawn || !record.captured.empty()) {
        m_halfmoveClock = 0;
    } else {
        m_halfmoveClock++;
    }

    // Update fullmove number
    if (m_sideToMove == PieceColor::Black) {
        m_fullmoveNumber++;
    }

    // Switch side
    m_sideToMove = opponent(m_sideToMove);

    return record;
}

void ChessBoard::unmakeMove(const MoveRecord& record) {
    const Move& move = record.move;

    // Switch side back
    m_sideToMove = opponent(m_sideToMove);

    // Restore state
    m_castleRights    = record.prevCastleRights;
    m_enPassantTarget = record.prevEnPassantTarget;
    m_halfmoveClock   = record.prevHalfmoveClock;
    if (m_sideToMove == PieceColor::Black) {
        m_fullmoveNumber--;
    }

    // Atomic capture undo: restore exploded neighbors first, then the moving piece
    if (record.explodedMask != 0) {
        // Restore all exploded neighbors
        for (int i = 0; i < 8; i++) {
            if (record.explodedMask & (1 << i)) {
                int8_t nc = (int8_t)move.to.col + ATOMIC_OFFS[i][0];
                int8_t nr = (int8_t)move.to.row + ATOMIC_OFFS[i][1];
                set(nc, nr, record.exploded[i]);
            }
        }
        // Restore moving piece to FROM (it was destroyed in explosion)
        set(move.from.col, move.from.row, record.movedPiece);
        // Clear TO (was already empty from explosion, but be explicit)
        set(move.to.col, move.to.row, Piece{});
        // Restore captured piece
        if (!record.captured.empty()) {
            set(record.capturedSquare.col, record.capturedSquare.row, record.captured);
        }
    } else {
        // Standard undo path
        // Get the piece that was moved (may have been promoted)
        Piece movedPiece = at(move.to.col, move.to.row);

        // If it was a promotion, restore to pawn
        if (move.promotion != PieceType::None) {
            movedPiece = makePiece(PieceType::Pawn, m_sideToMove);
        }

        // Move the piece back
        set(move.from.col, move.from.row, movedPiece);
        set(move.to.col, move.to.row, Piece{});

        // Restore captured piece
        if (!record.captured.empty()) {
            set(record.capturedSquare.col, record.capturedSquare.row, record.captured);
        }
    }

    // Undo castling — move the rook back
    if (move.isCastle) {
        uint8_t row = move.from.row;
        if (move.to.col > move.from.col) {
            // Kingside: rook back from f to h
            Piece rook = at(5, row);
            set(7, row, rook);
            set(5, row, Piece{});
        } else {
            // Queenside: rook back from d to a
            Piece rook = at(3, row);
            set(0, row, rook);
            set(3, row, Piece{});
        }
    }
}
