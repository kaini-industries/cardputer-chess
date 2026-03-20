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
    // W: Qd1(3,0) Rf1(5,0) Kg1(6,0). B: Rf8(5,7) Kg8(6,7) pa7 pb7 pg7 ph7.
    {"...Q.RK." "........" "........" "........" "........" "........" "pp....pp" ".....rk.",
     0, PuzzleType::MateIn1, 45, 1, {{3,0, 3,7}}},

    // 2: Arabian mate. Rh7-h8# with Nf6 guarding g8.
    // W: Kg1(6,0) Nf6(5,5) Rh7(7,6). B: Kg8(6,7).
    {"......K." "........" "........" "........" "........" ".....N.." ".......R" "......k.",
     0, PuzzleType::MateIn1, 60, 1, {{7,6, 7,7}}},

    // 3: Rook delivers Ra1-a8# with King covering escape.
    // W: Ra1(0,0) Kf6(5,5). B: Kh8(7,7).
    {"R......." "........" "........" "........" "........" ".....K.." "........" ".......k",
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

    // 8: Pawn promotion mate. e7-e8=Q#. Kf6 covers escape.
    // W: Kf6(5,5) Pe7(4,6). B: Ke8(4,7).
    {"........" "........" "........" "........" "........" ".....K.." "....P..." "....k...",
     0, PuzzleType::MateIn1, 70, 1, {{4,6, 4,7, PieceType::Queen}}},

    // 9: Queen mate on g1. Qg3-g1#.
    // W: Ka1(0,0) Qg3(6,2). B: Kh1(7,0) ph2(7,1).
    {"K......k" ".......p" "......Q." "........" "........" "........" "........" "........",
     0, PuzzleType::MateIn1, 55, 1, {{6,2, 6,0}}},

    // ── Tactics (10-11) ──────────────────────────────────────

    // 10: Knight fork. Nd4-e6 forks Kg7 and Qc5.
    // W: Kg1(6,0) Nd4(3,3). B: Kg7(6,6) Qc5(2,4).
    {"......K." "........" "........" "...N...." "..q....." "........" "......k." "........",
     0, PuzzleType::Tactic, 55, 1, {{3,3, 4,5}}},

    // 11: Pin wins piece. Bg5 pins Nf6 to Qd8. Capture Bxf6 wins knight.
    // W: Kg1(6,0) Bg5(6,4). B: Ke8(4,7) Nf6(5,5) Qd8(3,7).
    {"......K." "........" "........" "........" "......B." ".....n.." "........" "...qk...",
     // Wait: Bg5 is (6,4). Nf6 is (5,5). If B captures f6, Bxf6, Q recaptures? No — B pins N to Q.
     // The B on g5 attacks f6. The N on f6 shields the K on e8... wait, B goes diagonal.
     // g5-f6-e7-d8: yes! B on g5 pins N on f6 to Q on d8 (through e7 which is empty).
     // Actually Bg5-f6 is one square diagonal. Then f6-e7-d8. So if Bxf6, Qxf6? No, Q is on d8.
     // The pin means N can't move. So Bxf6 wins the knight for free.
     0, PuzzleType::Tactic, 60, 1, {{6,4, 5,5}}},
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
