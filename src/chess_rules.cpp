#include "chess_rules.h"

namespace ChessRules {

// Direction offsets for sliding pieces
static const int8_t BISHOP_DIRS[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
static const int8_t ROOK_DIRS[4][2]   = {{-1,0},{1,0},{0,-1},{0,1}};
static const int8_t KNIGHT_OFFS[8][2] = {
    {-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}
};
static const int8_t KING_OFFS[8][2] = {
    {-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}
};

// ─── Attack Detection ─────────────────────────────────────────────

bool isAttacked(const ChessBoard& board, uint8_t col, uint8_t row, PieceColor byColor) {
    // Knight attacks
    for (int i = 0; i < 8; i++) {
        int8_t nc = (int8_t)col + KNIGHT_OFFS[i][0];
        int8_t nr = (int8_t)row + KNIGHT_OFFS[i][1];
        if (nc >= 0 && nc < 8 && nr >= 0 && nr < 8) {
            Piece p = board.at(nc, nr);
            if (p.type == PieceType::Knight && p.color == byColor) return true;
        }
    }

    // King attacks (adjacent squares)
    for (int i = 0; i < 8; i++) {
        int8_t nc = (int8_t)col + KING_OFFS[i][0];
        int8_t nr = (int8_t)row + KING_OFFS[i][1];
        if (nc >= 0 && nc < 8 && nr >= 0 && nr < 8) {
            Piece p = board.at(nc, nr);
            if (p.type == PieceType::King && p.color == byColor) return true;
        }
    }

    // Pawn attacks
    int8_t pawnDir = (byColor == PieceColor::White) ? 1 : -1;
    // The attacking pawn would be on the row one step in its forward direction from target
    int8_t pr = (int8_t)row - pawnDir;
    if (pr >= 0 && pr < 8) {
        for (int8_t dc = -1; dc <= 1; dc += 2) {
            int8_t pc = (int8_t)col + dc;
            if (pc >= 0 && pc < 8) {
                Piece p = board.at(pc, pr);
                if (p.type == PieceType::Pawn && p.color == byColor) return true;
            }
        }
    }

    // Bishop/Queen attacks (diagonals)
    for (int d = 0; d < 4; d++) {
        for (int dist = 1; dist < 8; dist++) {
            int8_t nc = (int8_t)col + BISHOP_DIRS[d][0] * dist;
            int8_t nr = (int8_t)row + BISHOP_DIRS[d][1] * dist;
            if (nc < 0 || nc >= 8 || nr < 0 || nr >= 8) break;
            Piece p = board.at(nc, nr);
            if (!p.empty()) {
                if (p.color == byColor &&
                    (p.type == PieceType::Bishop || p.type == PieceType::Queen)) {
                    return true;
                }
                break; // Blocked
            }
        }
    }

    // Rook/Queen attacks (orthogonals)
    for (int d = 0; d < 4; d++) {
        for (int dist = 1; dist < 8; dist++) {
            int8_t nc = (int8_t)col + ROOK_DIRS[d][0] * dist;
            int8_t nr = (int8_t)row + ROOK_DIRS[d][1] * dist;
            if (nc < 0 || nc >= 8 || nr < 0 || nr >= 8) break;
            Piece p = board.at(nc, nr);
            if (!p.empty()) {
                if (p.color == byColor &&
                    (p.type == PieceType::Rook || p.type == PieceType::Queen)) {
                    return true;
                }
                break; // Blocked
            }
        }
    }

    return false;
}

bool isInCheck(const ChessBoard& board, PieceColor c) {
    Square king = board.findKing(c);
    if (isNoSquare(king)) return false;
    return isAttacked(board, king.col, king.row, opponent(c));
}

// ─── Pseudo-Legal Move Generation ─────────────────────────────────

static void addPawnMoves(const ChessBoard& board, uint8_t col, uint8_t row,
                         PieceColor color, MoveList& out) {
    int8_t dir = (color == PieceColor::White) ? 1 : -1;
    uint8_t startRow = (color == PieceColor::White) ? 1 : 6;
    uint8_t promoRow = (color == PieceColor::White) ? 7 : 0;

    bool isAtomic = board.variant() == ChessVariant::Atomic;

    auto addWithPromo = [&](Square from, Square to, bool isEP = false, bool isCapture = false) {
        // In atomic, capture-promotions are suppressed (pawn explodes before promoting)
        if (to.row == promoRow && !(isAtomic && isCapture)) {
            // Generate all 4 promotion options
            PieceType promos[] = {PieceType::Queen, PieceType::Rook,
                                  PieceType::Bishop, PieceType::Knight};
            for (auto pt : promos) {
                Move m;
                m.from = from;
                m.to = to;
                m.promotion = pt;
                out.add(m);
            }
        } else {
            Move m;
            m.from = from;
            m.to = to;
            m.isEnPassant = isEP;
            out.add(m);
        }
    };

    Square from = makeSquare(col, row);

    // Forward one
    int8_t newRow = (int8_t)row + dir;
    if (newRow >= 0 && newRow < 8 && board.at(col, newRow).empty()) {
        addWithPromo(from, makeSquare(col, newRow));

        // Forward two from start
        int8_t twoRow = newRow + dir;
        if (row == startRow && twoRow >= 0 && twoRow < 8 &&
            board.at(col, twoRow).empty()) {
            Move m;
            m.from = from;
            m.to = makeSquare(col, twoRow);
            out.add(m);
        }
    }

    // Captures (including en passant)
    for (int8_t dc = -1; dc <= 1; dc += 2) {
        int8_t nc = (int8_t)col + dc;
        if (nc < 0 || nc >= 8 || newRow < 0 || newRow >= 8) continue;

        Piece target = board.at(nc, newRow);
        Square toSq = makeSquare(nc, newRow);

        if (!target.empty() && target.color != color) {
            addWithPromo(from, toSq, false, true);
        }

        // En passant
        Square ep = board.enPassantTarget();
        if (!isNoSquare(ep) && ep.col == nc && ep.row == (uint8_t)newRow) {
            addWithPromo(from, toSq, true, true);
        }
    }
}

static void addSlidingMoves(const ChessBoard& board, uint8_t col, uint8_t row,
                             PieceColor color, const int8_t dirs[][2], int numDirs,
                             MoveList& out) {
    Square from = makeSquare(col, row);
    for (int d = 0; d < numDirs; d++) {
        for (int dist = 1; dist < 8; dist++) {
            int8_t nc = (int8_t)col + dirs[d][0] * dist;
            int8_t nr = (int8_t)row + dirs[d][1] * dist;
            if (nc < 0 || nc >= 8 || nr < 0 || nr >= 8) break;

            Piece target = board.at(nc, nr);
            if (target.empty()) {
                Move m;
                m.from = from;
                m.to = makeSquare(nc, nr);
                out.add(m);
            } else {
                if (target.color != color) {
                    Move m;
                    m.from = from;
                    m.to = makeSquare(nc, nr);
                    out.add(m);
                }
                break; // Blocked
            }
        }
    }
}

static void addKnightMoves(const ChessBoard& board, uint8_t col, uint8_t row,
                             PieceColor color, MoveList& out) {
    Square from = makeSquare(col, row);
    for (int i = 0; i < 8; i++) {
        int8_t nc = (int8_t)col + KNIGHT_OFFS[i][0];
        int8_t nr = (int8_t)row + KNIGHT_OFFS[i][1];
        if (nc < 0 || nc >= 8 || nr < 0 || nr >= 8) continue;

        Piece target = board.at(nc, nr);
        if (target.empty() || target.color != color) {
            Move m;
            m.from = from;
            m.to = makeSquare(nc, nr);
            out.add(m);
        }
    }
}

static void addKingMoves(const ChessBoard& board, uint8_t col, uint8_t row,
                          PieceColor color, MoveList& out) {
    Square from = makeSquare(col, row);

    // Normal king moves
    bool isAtomic = board.variant() == ChessVariant::Atomic;
    for (int i = 0; i < 8; i++) {
        int8_t nc = (int8_t)col + KING_OFFS[i][0];
        int8_t nr = (int8_t)row + KING_OFFS[i][1];
        if (nc < 0 || nc >= 8 || nr < 0 || nr >= 8) continue;

        Piece target = board.at(nc, nr);
        // In atomic chess, kings cannot capture (would self-destruct)
        if (isAtomic) {
            if (target.empty()) {
                Move m;
                m.from = from;
                m.to = makeSquare(nc, nr);
                out.add(m);
            }
        } else {
            if (target.empty() || target.color != color) {
                Move m;
                m.from = from;
                m.to = makeSquare(nc, nr);
                out.add(m);
            }
        }
    }

    // Castling
    PieceColor opp = opponent(color);
    uint8_t kingRow = (color == PieceColor::White) ? 0 : 7;

    // Only if king is on its starting square
    if (col != 4 || row != kingRow) return;

    // Don't castle out of check
    if (isAttacked(board, col, row, opp)) return;

    // Kingside
    if (board.canCastleKS(color)) {
        // Squares f and g must be empty, king must not pass through check
        if (board.at(5, kingRow).empty() && board.at(6, kingRow).empty() &&
            !isAttacked(board, 5, kingRow, opp) &&
            !isAttacked(board, 6, kingRow, opp)) {
            Move m;
            m.from = from;
            m.to = makeSquare(6, kingRow);
            m.isCastle = true;
            out.add(m);
        }
    }

    // Queenside
    if (board.canCastleQS(color)) {
        // Squares b, c, d must be empty, king must not pass through check on d and c
        if (board.at(1, kingRow).empty() && board.at(2, kingRow).empty() &&
            board.at(3, kingRow).empty() &&
            !isAttacked(board, 2, kingRow, opp) &&
            !isAttacked(board, 3, kingRow, opp)) {
            Move m;
            m.from = from;
            m.to = makeSquare(2, kingRow);
            m.isCastle = true;
            out.add(m);
        }
    }
}

static void generatePseudoLegal(const ChessBoard& board, MoveList& out) {
    PieceColor side = board.sideToMove();

    for (uint8_t r = 0; r < 8; r++) {
        for (uint8_t c = 0; c < 8; c++) {
            Piece p = board.at(c, r);
            if (p.empty() || p.color != side) continue;

            switch (p.type) {
            case PieceType::Pawn:
                addPawnMoves(board, c, r, side, out);
                break;
            case PieceType::Knight:
                addKnightMoves(board, c, r, side, out);
                break;
            case PieceType::Bishop:
                addSlidingMoves(board, c, r, side, BISHOP_DIRS, 4, out);
                break;
            case PieceType::Rook:
                addSlidingMoves(board, c, r, side, ROOK_DIRS, 4, out);
                break;
            case PieceType::Queen: {
                // Queen = bishop + rook directions
                addSlidingMoves(board, c, r, side, BISHOP_DIRS, 4, out);
                addSlidingMoves(board, c, r, side, ROOK_DIRS, 4, out);
                break;
            }
            case PieceType::King:
                addKingMoves(board, c, r, side, out);
                break;
            default:
                break;
            }
        }
    }
}

// ─── Legal Move Generation ────────────────────────────────────────

void generateLegal(ChessBoard& board, MoveList& out) {
    out.clear();
    MoveList pseudo;
    pseudo.clear();
    generatePseudoLegal(board, pseudo);

    PieceColor side = board.sideToMove();
    bool isAtomic = board.variant() == ChessVariant::Atomic;

    for (uint8_t i = 0; i < pseudo.count; i++) {
        MoveRecord rec = board.makeMove(pseudo.moves[i]);
        if (isAtomic) {
            bool enemyKingGone = isNoSquare(board.findKing(opponent(side)));
            bool ownKingGone = isNoSquare(board.findKing(side));
            if (enemyKingGone) {
                // Legal: wins by exploding enemy king (overrides self-check)
                out.add(pseudo.moves[i]);
            } else if (!ownKingGone && !isInCheck(board, side)) {
                // Legal: own king survives and is not in check
                out.add(pseudo.moves[i]);
            }
            // Else: blew up own king without blowing up theirs, or left in check
        } else {
            // Standard: own king must not be in check
            if (!isInCheck(board, side)) {
                out.add(pseudo.moves[i]);
            }
        }
        board.unmakeMove(rec);
    }
}

void getLegalMovesFrom(ChessBoard& board, uint8_t col, uint8_t row, MoveList& out) {
    out.clear();
    MoveList allLegal;
    allLegal.clear();
    generateLegal(board, allLegal);

    for (uint8_t i = 0; i < allLegal.count; i++) {
        if (allLegal.moves[i].from.col == col && allLegal.moves[i].from.row == row) {
            out.add(allLegal.moves[i]);
        }
    }
}

// ─── Game-End Detection ───────────────────────────────────────────

bool isCheckmate(ChessBoard& board) {
    MoveList legal;
    legal.clear();
    generateLegal(board, legal);
    return legal.count == 0 && isInCheck(board, board.sideToMove());
}

bool isStalemate(ChessBoard& board) {
    MoveList legal;
    legal.clear();
    generateLegal(board, legal);
    return legal.count == 0 && !isInCheck(board, board.sideToMove());
}

bool isDraw50Move(const ChessBoard& board) {
    return board.halfmoveClock() >= 100;
}

bool isInsufficientMaterial(const ChessBoard& board) {
    // Atomic: insufficient material doesn't apply (even a pawn can win)
    if (board.variant() == ChessVariant::Atomic) return false;

    uint8_t whiteKnights = 0, whiteBishops = 0;
    uint8_t blackKnights = 0, blackBishops = 0;
    uint8_t otherPieces = 0;

    for (uint8_t r = 0; r < 8; r++) {
        for (uint8_t c = 0; c < 8; c++) {
            Piece p = board.at(c, r);
            if (p.empty() || p.type == PieceType::King) continue;

            if (p.type == PieceType::Knight) {
                if (p.color == PieceColor::White) whiteKnights++;
                else blackKnights++;
            } else if (p.type == PieceType::Bishop) {
                if (p.color == PieceColor::White) whiteBishops++;
                else blackBishops++;
            } else {
                otherPieces++;
            }
        }
    }

    if (otherPieces > 0) return false;

    uint8_t totalMinor = whiteKnights + whiteBishops + blackKnights + blackBishops;

    // K vs K
    if (totalMinor == 0) return true;
    // K+B vs K or K+N vs K
    if (totalMinor == 1) return true;

    return false;
}

bool isKingExploded(const ChessBoard& board, PieceColor c) {
    return isNoSquare(board.findKing(c));
}

} // namespace ChessRules
