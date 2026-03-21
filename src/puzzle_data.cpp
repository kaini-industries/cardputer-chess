#include "puzzle_data.h"
#include "chess_board.h"
#include "chess_rules.h"

// =====================================================================
// Puzzle Database — Runtime-decoded format
// Each puzzle is a 64-char position string + solution moves.
// Position: row 0 (rank 1) first, '.' = empty, uppercase = white, lowercase = black
// =====================================================================

struct SimplePuzzle {
    const char* position;
    uint8_t sideToMove;   // 0=White, 1=Black
    PuzzleType type;
    uint8_t rating;
    uint8_t solutionLen;
    struct { uint8_t fc, fr, tc, tr; PieceType promo; } solution[4];
};

static Piece charToPiece(char c) {
    switch (c) {
        case 'P': return Piece(PieceType::Pawn, PieceColor::White);
        case 'N': return Piece(PieceType::Knight, PieceColor::White);
        case 'B': return Piece(PieceType::Bishop, PieceColor::White);
        case 'R': return Piece(PieceType::Rook, PieceColor::White);
        case 'Q': return Piece(PieceType::Queen, PieceColor::White);
        case 'K': return Piece(PieceType::King, PieceColor::White);
        case 'p': return Piece(PieceType::Pawn, PieceColor::Black);
        case 'n': return Piece(PieceType::Knight, PieceColor::Black);
        case 'b': return Piece(PieceType::Bishop, PieceColor::Black);
        case 'r': return Piece(PieceType::Rook, PieceColor::Black);
        case 'q': return Piece(PieceType::Queen, PieceColor::Black);
        case 'k': return Piece(PieceType::King, PieceColor::Black);
        default:  return Piece();
    }
}

// Position strings: 64 chars. Index = row*8+col. Row 0 = rank 1 (white side).
// Coordinates: col 0=a..7=h, row 0=rank1..7=rank8

static const SimplePuzzle SIMPLE_PUZZLES[] = {
    // ── Mate in 1 (0-9) ─────────────────────────────────────

    // 0: Back rank mate with rook. Rd1-d8#
    // W: Rd1(3,0) Kg1(6,0). B: Kh8(7,7) pa7(0,6) pg7(6,6) ph7(7,6).
    {"...R..K." "........" "........" "........" "........" "........" "p.....pp" ".......k",
     0, PuzzleType::MateIn1, 50, 1, {{3,0, 3,7}}},

    // 1: Queen back rank mate. Qd1-d8#
    // W: Qd1(3,0) Rf1(5,0) Kg1(6,0). B: Kg8(6,7) pa7 pb7 pg7 ph7.
    // Rf1 covers f-file (f7,f8). Queen covers rank 8. Pawns box in king.
    {"...Q.RK." "........" "........" "........" "........" "........" "pp....pp" "......k.",
     0, PuzzleType::MateIn1, 45, 1, {{3,0, 3,7}}},

    // 2: Arabian mate. Rh7-h8# with Nf6 guarding g8/h7.
    // W: Kg1(6,0) Nf6(5,5) Rh7(7,6). B: Kg8(6,7) pf7(5,6) pg7(6,6).
    // Pawns on f7,g7 block escape. Nf6 covers g8 and h7. Rh8 covers rank 8.
    {"......K." "........" "........" "........" "........" ".....N.." ".....ppR" "......k.",
     0, PuzzleType::MateIn1, 60, 1, {{7,6, 7,7}}},

    // 3: Rook delivers Ra1-a8# with King covering escape.
    // W: Ra1(0,0) Kf6(5,5). B: Kh8(7,7) ph7(7,6).
    // Ra8 covers rank 8 (g8). Kf6 covers g7. ph7 blocks h7.
    {"R......." "........" "........" "........" "........" ".....K.." ".......p" ".......k",
     0, PuzzleType::MateIn1, 55, 1, {{0,0, 0,7}}},

    // 4: Queen captures mate. Qf6xg7#
    // W: Kh1(7,0) Qf6(5,5). B: Kh8(7,7) pg7(6,6) ph7(7,6).
    {".......K" "........" "........" "........" "........" ".....Q.." "......pp" ".......k",
     0, PuzzleType::MateIn1, 50, 1, {{5,5, 6,6}}},

    // 5: Queen mates on a8. Qa7-a8#
    // W: Kf6(5,5) Qa7(0,6). B: Kh8(7,7) ph7(7,6).
    {"........" "........" "........" "........" "........" ".....K.." "Q......p" ".......k",
     0, PuzzleType::MateIn1, 45, 1, {{0,6, 0,7}}},

    // 6: King+Queen mate. Qb6-a7#
    // W: Ka6(0,5) Qb6(1,5). B: Ka8(0,7).
    {"........" "........" "........" "........" "........" "KQ......" "........" "k.......",
     0, PuzzleType::MateIn1, 40, 1, {{1,5, 0,6}}},

    // 7: Ladder mate. Ra6-a8#. Rb7 covers b8,c8.
    // W: Kc1(2,0) Ra6(0,5) Rb7(1,6). B: Kd8(3,7).
    {"..K....." "........" "........" "........" "........" "R......." ".R......" "...k....",
     0, PuzzleType::MateIn1, 40, 1, {{0,5, 0,7}}},

    // 8: Pawn promotion mate. c7-c8=Q#. Kb6 covers escape.
    // W: Kb6(1,5) Pc7(2,6). B: Ka8(0,7).
    // c8=Q checks along rank 8. Kb6 covers a7 and b7.
    {"........" "........" "........" "........" "........" ".K......" "..P....." "k.......",
     0, PuzzleType::MateIn1, 70, 1, {{2,6, 2,7, PieceType::Queen}}},

    // 9: Queen mate on g1. Qg3-g1#.
    // W: Ka1(0,0) Qg3(6,2). B: Kh1(7,0) ph2(7,1).
    {"K......k" ".......p" "......Q." "........" "........" "........" "........" "........",
     0, PuzzleType::MateIn1, 55, 1, {{6,2, 6,0}}},

    // ── Mate in 1 (10-11) ────────────────────────────────────

    // 10: Knight smothered mate. Ne5-g6#. King boxed in by own pieces.
    // W: Kg1(6,0) Ne5(4,4). B: Kh8(7,7) Rg8(6,7) pg7(6,6) ph7(7,6).
    // Ng6 checks h8. Rg8 blocks g8, pg7 blocks g7, ph7 blocks h7. Rg8 can't capture (pg7 blocks).
    {"......K." "........" "........" "........" "....N..." "........" "......pp" "......rk",
     0, PuzzleType::MateIn1, 65, 1, {{4,4, 6,5}}},

    // 11: Queen captures pawn for mate. Qf6xg7#.
    // W: Kg1(6,0) Qf6(5,5). B: Kg8(6,7) Rf8(5,7) pd7(3,6) pg7(6,6) ph7(7,6).
    {"......K." "........" "........" "........" "........" ".....Q.." "...p..pp" ".....rk.",
     0, PuzzleType::MateIn1, 55, 1, {{5,5, 6,6}}},

    // ── Mate in 2 (12-14) ─────────────────────────────────────

    // 12: Rook sacrifice + sliding mate. Rd1xd8+ Kh7 Rd8-h8#.
    // W: Kg1(6,0) Pf5(5,4) Rd1(3,0). B: Kg8(6,7) Rd8(3,7) pf7(5,6) pg7(6,6).
    // After Rxd8+ Kh7 forced (f7,g7=pawns, f8/h8=rank 8). Rh8# with Pf5 covering g6.
    {"...R..K." "........" "........" "........" ".....P.." "........" ".....pp." "...r..k.",
     0, PuzzleType::MateIn2, 60, 3, {{3,0, 3,7}, {6,7, 7,6}, {3,7, 7,7}}},

    // 13: Rook sacrifice + queen mate. Rc1xc8+ Kh7 Qe2-h5#.
    // W: Kg1(6,0) Qe2(4,1) Rc1(2,0). B: Kg8(6,7) Rc8(2,7) pf7(5,6) pg7(6,6).
    // After Rxc8+ Kh7 forced. Qh5# with Rc8 covering rank 8, Qh5 covering g6/h6.
    {"..R...K." "....Q..." "........" "........" "........" "........" ".....pp." "..r...k.",
     0, PuzzleType::MateIn2, 65, 3, {{2,0, 2,7}, {6,7, 7,6}, {4,1, 7,4}}},

    // 14: Knight check + queen capture mate. Nf5-h6+ Kh8 Qa1xg7#.
    // W: Kg1(6,0) Qa1(0,0) Nf5(5,4). B: Kg8(6,7) Rf8(5,7) pf7(5,6) pg7(6,6) ph7(7,6).
    // Nh6+ forces Kh8 (f8=rook, h7=pawn, g7=pawn). Qxg7# via a1-g7 diagonal.
    {"Q.....K." "........" "........" "........" ".....N.." "........" ".....ppp" ".....rk.",
     0, PuzzleType::MateIn2, 70, 3, {{5,4, 7,5}, {6,7, 7,7}, {0,0, 6,6}}},

    // ── Tactics (15-17) ────────────────────────────────────────

    // 15: Knight fork. Nd4-e6 forks Kg7 and Qc5.
    // W: Kg1(6,0) Nd4(3,3). B: Kg7(6,6) Qc5(2,4).
    {"......K." "........" "........" "...N...." "..q....." "........" "......k." "........",
     0, PuzzleType::Tactic, 55, 1, {{3,3, 4,5}}},

    // 16: Pin wins piece. Bg5 pins Nf6 to Qd8. Bxf6 wins knight.
    // W: Kg1(6,0) Bg5(6,4). B: Ke8(4,7) Nf6(5,5) Qd8(3,7).
    {"......K." "........" "........" "........" "......B." ".....n.." "........" "...qk...",
     0, PuzzleType::Tactic, 60, 1, {{6,4, 5,5}}},

    // 17: Knight fork. Ne4-f6+ forks Kg8 and Qd5.
    // W: Kg1(6,0) Ne4(4,3). B: Kg8(6,7) Qd5(3,4) Rf8(5,7) ph7(7,6).
    {"......K." "........" "........" "....N..." "...q...." "........" ".......p" ".....rk.",
     0, PuzzleType::Tactic, 55, 1, {{4,3, 5,5}}},
};

static constexpr uint16_t NUM_PUZZLES = sizeof(SIMPLE_PUZZLES) / sizeof(SIMPLE_PUZZLES[0]);

// ── API Implementation ──────────────────────────────────────────────

uint16_t puzzleCount() {
    return NUM_PUZZLES;
}

uint16_t puzzleCountByType(PuzzleType type) {
    uint16_t count = 0;
    for (uint16_t i = 0; i < NUM_PUZZLES; i++) {
        if (SIMPLE_PUZZLES[i].type == type) count++;
    }
    return count;
}

uint16_t puzzleIndexByType(PuzzleType type, uint16_t nth) {
    uint16_t count = 0;
    for (uint16_t i = 0; i < NUM_PUZZLES; i++) {
        if (SIMPLE_PUZZLES[i].type == type) {
            if (count == nth) return i;
            count++;
        }
    }
    return 0xFFFF;
}

void loadPuzzleIntoBoard(uint16_t index, ChessBoard& board, Move* solution,
                         uint8_t& solutionLen, PuzzleType& type, uint8_t& rating) {
    if (index >= NUM_PUZZLES) return;
    const SimplePuzzle& p = SIMPLE_PUZZLES[index];

    board.setVariant(ChessVariant::Standard);
    board.reset();

    for (uint8_t r = 0; r < 8; r++) {
        for (uint8_t c = 0; c < 8; c++) {
            board.set(c, r, charToPiece(p.position[r * 8 + c]));
        }
    }
    board.setSideToMove(p.sideToMove == 0 ? PieceColor::White : PieceColor::Black);
    board.setCastleRights(0);
    board.setEnPassantTarget(NO_SQUARE);
    board.setHalfmoveClock(0);
    board.setFullmoveNumber(1);

    solutionLen = p.solutionLen;
    type = p.type;
    rating = p.rating;

    for (uint8_t i = 0; i < p.solutionLen && i < 4; i++) {
        solution[i].from = makeSquare(p.solution[i].fc, p.solution[i].fr);
        solution[i].to = makeSquare(p.solution[i].tc, p.solution[i].tr);
        solution[i].promotion = p.solution[i].promo;
        solution[i].isCastle = false;
        solution[i].isEnPassant = false;
    }
}
