#include "chess_board.h"
#include "chess960.h"

ChessBoard::ChessBoard() {
    reset();
}

void ChessBoard::reset() {
    // Clear the board
    memset(m_board, 0, sizeof(m_board));

    if (m_variant == ChessVariant::Chess960) {
        Chess960Position pos = chess960Generate(m_positionIndex);
        m_initKingCol = pos.kingCol;
        m_initRookKS = pos.rookKS;
        m_initRookQS = pos.rookQS;

        for (uint8_t c = 0; c < 8; c++) {
            m_board[0][c] = makePiece(pos.backRank[c], PieceColor::White);
            m_board[7][c] = makePiece(pos.backRank[c], PieceColor::Black);
        }
    } else {
        m_initKingCol = 4;
        m_initRookKS = 7;
        m_initRookQS = 0;

        // White pieces (row 0 = rank 1)
        m_board[0][0] = makePiece(PieceType::Rook,   PieceColor::White);
        m_board[0][1] = makePiece(PieceType::Knight, PieceColor::White);
        m_board[0][2] = makePiece(PieceType::Bishop, PieceColor::White);
        m_board[0][3] = makePiece(PieceType::Queen,  PieceColor::White);
        m_board[0][4] = makePiece(PieceType::King,   PieceColor::White);
        m_board[0][5] = makePiece(PieceType::Bishop, PieceColor::White);
        m_board[0][6] = makePiece(PieceType::Knight, PieceColor::White);
        m_board[0][7] = makePiece(PieceType::Rook,   PieceColor::White);

        // Black pieces (row 7 = rank 8)
        m_board[7][0] = makePiece(PieceType::Rook,   PieceColor::Black);
        m_board[7][1] = makePiece(PieceType::Knight, PieceColor::Black);
        m_board[7][2] = makePiece(PieceType::Bishop, PieceColor::Black);
        m_board[7][3] = makePiece(PieceType::Queen,  PieceColor::Black);
        m_board[7][4] = makePiece(PieceType::King,   PieceColor::Black);
        m_board[7][5] = makePiece(PieceType::Bishop, PieceColor::Black);
        m_board[7][6] = makePiece(PieceType::Knight, PieceColor::Black);
        m_board[7][7] = makePiece(PieceType::Rook,   PieceColor::Black);
    }

    for (uint8_t c = 0; c < 8; c++) {
        m_board[1][c] = makePiece(PieceType::Pawn, PieceColor::White);
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

MoveRecord ChessBoard::makeMove(const Move& move) {
    MoveRecord record;
    record.move = move;
    record.prevCastleRights   = m_castleRights;
    record.prevEnPassantTarget = m_enPassantTarget;
    record.prevHalfmoveClock  = m_halfmoveClock;

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

    // Handle castling — clear both pieces first, then place at destinations
    // (must be done before normal move to handle 960 overlapping positions)
    if (move.isCastle) {
        uint8_t row = move.from.row;
        uint8_t rookFromCol, rookToCol;
        if (move.to.col == 6) {
            // Kingside: king→g, rook→f
            rookFromCol = m_initRookKS;
            rookToCol = 5;
        } else {
            // Queenside: king→c, rook→d
            rookFromCol = m_initRookQS;
            rookToCol = 3;
        }
        Piece rook = makePiece(PieceType::Rook, movingPiece.color);
        // Clear both source squares first
        set(move.from.col, row, Piece{});
        set(rookFromCol, row, Piece{});
        // Place both at destinations
        set(move.to.col, row, movingPiece);
        set(rookToCol, row, rook);
    } else {
        // Normal move
        set(move.to.col, move.to.row, movingPiece);
        set(move.from.col, move.from.row, Piece{});

        // Handle promotion
        if (move.promotion != PieceType::None) {
            set(move.to.col, move.to.row, makePiece(move.promotion, movingPiece.color));
        }
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
    // Rook moves or is captured — use stored initial columns (supports 960)
    auto clearRookRights = [&](uint8_t col, uint8_t row) {
        if (row == 0) {
            if (col == m_initRookQS) m_castleRights &= ~CastleRights::WhiteQueen;
            if (col == m_initRookKS) m_castleRights &= ~CastleRights::WhiteKing;
        }
        if (row == 7) {
            if (col == m_initRookQS) m_castleRights &= ~CastleRights::BlackQueen;
            if (col == m_initRookKS) m_castleRights &= ~CastleRights::BlackKing;
        }
    };
    clearRookRights(move.from.col, move.from.row);
    clearRookRights(move.to.col, move.to.row);

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

    // Undo castling — restore king and rook to initial columns
    if (move.isCastle) {
        uint8_t row = move.from.row;
        uint8_t rookFromCol, rookToCol;
        if (move.to.col == 6) {
            rookFromCol = m_initRookKS;
            rookToCol = 5;
        } else {
            rookFromCol = m_initRookQS;
            rookToCol = 3;
        }
        Piece rook = makePiece(PieceType::Rook, m_sideToMove);
        // Clear destination squares
        set(move.to.col, row, Piece{});
        set(rookToCol, row, Piece{});
        // Restore to initial squares
        set(move.from.col, row, record.movedPiece);
        set(rookFromCol, row, rook);
    }
}
