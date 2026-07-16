#include <unity.h>

#include "chess_board.h"
#include "chess_rules.h"
#include "puzzle_data.h"

#include <cstdint>
#include <cstdio>

namespace {

struct LoadedPuzzle {
    ChessBoard board;
    Move solution[MAX_PUZZLE_SOLUTION_MOVES];
    uint8_t solutionLen = 0;
    PuzzleType type = PuzzleType::MateIn1;
    uint8_t rating = 0;
    bool loaded = false;
};

char s_message[160];

const char* puzzleMessage(uint16_t index, const char* detail) {
    std::snprintf(s_message, sizeof(s_message), "Puzzle #%u: %s",
                  static_cast<unsigned>(index + 1), detail);
    return s_message;
}

LoadedPuzzle loadPuzzle(uint16_t index) {
    LoadedPuzzle puzzle;
    puzzle.loaded = loadPuzzleIntoBoard(index, puzzle.board, puzzle.solution,
                                        puzzle.solutionLen, puzzle.type,
                                        puzzle.rating);
    return puzzle;
}

bool isKnownType(PuzzleType type) {
    return type == PuzzleType::MateIn1 ||
           type == PuzzleType::MateIn2 ||
           type == PuzzleType::Tactic;
}

bool sameMoveKey(const Move& legal, const Move& encoded) {
    return legal.from == encoded.from &&
           legal.to == encoded.to &&
           legal.promotion == encoded.promotion;
}

uint16_t resolveMove(const MoveList& legal, const Move& encoded, Move& resolved) {
    uint16_t matches = 0;
    for (uint16_t i = 0; i < legal.count; ++i) {
        if (sameMoveKey(legal.moves[i], encoded)) {
            resolved = legal.moves[i];
            ++matches;
        }
    }
    return matches;
}

uint16_t countMateInOne(ChessBoard board) {
    MoveList legal;
    ChessRules::generateLegal(board, legal);

    uint16_t mates = 0;
    for (uint16_t i = 0; i < legal.count; ++i) {
        ChessBoard after = board;
        after.makeMove(legal.moves[i]);
        if (ChessRules::isCheckmate(after)) {
            ++mates;
        }
    }
    return mates;
}

bool forcesMateInTwo(ChessBoard board, const Move& key) {
    board.makeMove(key);
    if (ChessRules::isCheckmate(board) || ChessRules::isStalemate(board)) return false;

    MoveList defenses;
    ChessRules::generateLegal(board, defenses);
    if (defenses.count == 0) return false;

    for (uint16_t i = 0; i < defenses.count; ++i) {
        ChessBoard afterDefense = board;
        afterDefense.makeMove(defenses.moves[i]);
        if (countMateInOne(afterDefense) == 0) return false;
    }
    return true;
}

uint16_t countMateInTwoKeys(ChessBoard board) {
    MoveList legal;
    ChessRules::generateLegal(board, legal);

    uint16_t keys = 0;
    for (uint16_t i = 0; i < legal.count; ++i) {
        if (forcesMateInTwo(board, legal.moves[i])) ++keys;
    }
    return keys;
}

bool sideToMoveCanCapture(ChessBoard board, Square target) {
    MoveList legal;
    ChessRules::generateLegal(board, legal);
    for (uint16_t i = 0; i < legal.count; ++i) {
        if (legal.moves[i].to == target) return true;
    }
    return false;
}

void assertPiece(const ChessBoard& board, uint8_t col, uint8_t row,
                 PieceType type, PieceColor color, uint16_t puzzleIndex,
                 const char* detail) {
    const Piece piece = board.at(col, row);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(type),
                                    static_cast<uint8_t>(piece.type),
                                    puzzleMessage(puzzleIndex, detail));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(color),
                                    static_cast<uint8_t>(piece.color),
                                    puzzleMessage(puzzleIndex, detail));
}

uint16_t playScriptedMove(LoadedPuzzle& puzzle, uint8_t step,
                          Move* played = nullptr) {
    MoveList legal;
    ChessRules::generateLegal(puzzle.board, legal);

    Move resolved;
    const uint16_t matches = resolveMove(legal, puzzle.solution[step], resolved);
    if (matches == 1) {
        puzzle.board.makeMove(resolved);
        if (played != nullptr) *played = resolved;
    }
    return matches;
}

void test_database_shape_and_category_mapping() {
    const uint16_t total = puzzleCount();
    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(0, total, "Puzzle database is empty");
    TEST_ASSERT_LESS_OR_EQUAL_UINT16_MESSAGE(
        256, total, "Puzzle database exceeds uint8 progress/index capacity");

    const LoadedPuzzle invalid = loadPuzzle(total);
    TEST_ASSERT_FALSE_MESSAGE(
        invalid.loaded, "Out-of-range puzzle load must report failure");

    ChessBoard unchanged;
    unchanged.set(0, 0, Piece{});
    unchanged.setSideToMove(PieceColor::Black);
    unchanged.setCastleRights(0);
    Move scratch[MAX_PUZZLE_SOLUTION_MOVES];
    uint8_t scratchLen = 99;
    PuzzleType scratchType = PuzzleType::Tactic;
    uint8_t scratchRating = 99;
    TEST_ASSERT_FALSE_MESSAGE(
        loadPuzzleIntoBoard(total, unchanged, scratch, scratchLen,
                            scratchType, scratchRating),
        "Out-of-range puzzle load must fail");
    TEST_ASSERT_TRUE_MESSAGE(
        unchanged.at(0, 0).empty(), "Failed load changed board pieces");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        static_cast<uint8_t>(PieceColor::Black),
        static_cast<uint8_t>(unchanged.sideToMove()),
        "Failed load changed the side to move");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, unchanged.castleRights(), "Failed load changed castling rights");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, scratchLen, "Failed load did not clear the solution length");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, scratchRating, "Failed load did not clear the rating");

    const PuzzleType types[] = {
        PuzzleType::MateIn1,
        PuzzleType::MateIn2,
        PuzzleType::Tactic,
    };

    uint16_t categoryTotal = 0;
    for (PuzzleType type : types) {
        uint16_t nth = 0;
        for (uint16_t index = 0; index < total; ++index) {
            const LoadedPuzzle puzzle = loadPuzzle(index);
            if (puzzle.type != type) continue;

            TEST_ASSERT_EQUAL_UINT16_MESSAGE(
                index, puzzleIndexByType(type, nth),
                puzzleMessage(index, "category index mapping is inconsistent"));
            ++nth;
        }

        TEST_ASSERT_EQUAL_UINT16_MESSAGE(
            nth, puzzleCountByType(type), "Category count is inconsistent");
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(
            0xFFFF, puzzleIndexByType(type, nth),
            "One-past-last category lookup must fail");
        categoryTotal += nth;
    }

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        total, categoryTotal, "Known category counts do not cover every puzzle");
}

void test_starting_positions_are_well_formed_and_legal() {
    const uint16_t total = puzzleCount();
    for (uint16_t index = 0; index < total; ++index) {
        LoadedPuzzle puzzle = loadPuzzle(index);

        TEST_ASSERT_TRUE_MESSAGE(
            puzzle.loaded, puzzleMessage(index, "production loader rejected puzzle"));
        TEST_ASSERT_TRUE_MESSAGE(
            isKnownType(puzzle.type), puzzleMessage(index, "unknown puzzle type"));
        TEST_ASSERT_GREATER_THAN_UINT8_MESSAGE(
            0, puzzle.rating, puzzleMessage(index, "rating must be nonzero"));
        TEST_ASSERT_GREATER_THAN_UINT8_MESSAGE(
            0, puzzle.solutionLen, puzzleMessage(index, "solution is empty"));
        TEST_ASSERT_LESS_OR_EQUAL_UINT8_MESSAGE(
            MAX_PUZZLE_SOLUTION_MOVES, puzzle.solutionLen,
            puzzleMessage(index, "solution exceeds storage capacity"));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(
            1, puzzle.solutionLen % 2,
            puzzleMessage(index, "solution must end with a solver move"));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(
            static_cast<uint8_t>(ChessVariant::Standard),
            static_cast<uint8_t>(puzzle.board.variant()),
            puzzleMessage(index, "puzzle must use standard chess"));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(
            0, puzzle.board.castleRights(),
            puzzleMessage(index, "puzzle must not retain castling rights"));
        TEST_ASSERT_TRUE_MESSAGE(
            isNoSquare(puzzle.board.enPassantTarget()),
            puzzleMessage(index, "puzzle must not retain an en-passant target"));

        uint8_t whiteKings = 0;
        uint8_t blackKings = 0;
        for (uint8_t row = 0; row < 8; ++row) {
            for (uint8_t col = 0; col < 8; ++col) {
                const Piece piece = puzzle.board.at(col, row);
                if (piece.type == PieceType::King) {
                    if (piece.color == PieceColor::White) ++whiteKings;
                    else ++blackKings;
                }
                if ((row == 0 || row == 7) && piece.type == PieceType::Pawn) {
                    TEST_FAIL_MESSAGE(puzzleMessage(
                        index, "unpromoted pawn is on the first or eighth rank"));
                }
            }
        }

        TEST_ASSERT_EQUAL_UINT8_MESSAGE(
            1, whiteKings, puzzleMessage(index, "must contain exactly one white king"));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(
            1, blackKings, puzzleMessage(index, "must contain exactly one black king"));

        const PieceColor nonmoving = opponent(puzzle.board.sideToMove());
        TEST_ASSERT_FALSE_MESSAGE(
            ChessRules::isInCheck(puzzle.board, nonmoving),
            puzzleMessage(index, "nonmoving side is already in check"));

        MoveList legal;
        ChessRules::generateLegal(puzzle.board, legal);
        TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
            0, legal.count, puzzleMessage(index, "starting side has no legal move"));
    }
}

void test_scripts_are_legal_and_match_type_semantics() {
    const uint16_t total = puzzleCount();
    for (uint16_t index = 0; index < total; ++index) {
        LoadedPuzzle puzzle = loadPuzzle(index);
        const ChessBoard initial = puzzle.board;
        const PieceColor solver = puzzle.board.sideToMove();

        for (uint8_t step = 0; step < puzzle.solutionLen; ++step) {
            const PieceColor expectedSide = (step % 2 == 0) ? solver : opponent(solver);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(
                static_cast<uint8_t>(expectedSide),
                static_cast<uint8_t>(puzzle.board.sideToMove()),
                puzzleMessage(index, "script turn order is incorrect"));

            const uint16_t matches = playScriptedMove(puzzle, step);
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(
                1, matches,
                puzzleMessage(index, "scripted move is not uniquely legal"));

            if (step + 1 < puzzle.solutionLen) {
                TEST_ASSERT_FALSE_MESSAGE(
                    ChessRules::isCheckmate(puzzle.board),
                    puzzleMessage(index, "script continues after checkmate"));
                TEST_ASSERT_FALSE_MESSAGE(
                    ChessRules::isStalemate(puzzle.board),
                    puzzleMessage(index, "script continues after stalemate"));
            }
        }

        switch (puzzle.type) {
        case PuzzleType::MateIn1:
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(
                1, puzzle.solutionLen,
                puzzleMessage(index, "mate-in-1 must contain exactly one ply"));
            TEST_ASSERT_TRUE_MESSAGE(
                ChessRules::isCheckmate(puzzle.board),
                puzzleMessage(index, "mate-in-1 line does not end in checkmate"));
            break;

        case PuzzleType::MateIn2: {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(
                3, puzzle.solutionLen,
                puzzleMessage(index, "mate-in-2 must contain exactly three plies"));
            TEST_ASSERT_TRUE_MESSAGE(
                ChessRules::isCheckmate(puzzle.board),
                puzzleMessage(index, "mate-in-2 line does not end in checkmate"));
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(
                0, countMateInOne(initial),
                puzzleMessage(index, "mate-in-2 position already has a mate-in-1"));
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(
                1, countMateInTwoKeys(initial),
                puzzleMessage(index, "mate-in-2 must have exactly one winning key"));

            LoadedPuzzle afterKey = loadPuzzle(index);
            const uint16_t keyMatches = playScriptedMove(afterKey, 0);
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(
                1, keyMatches,
                puzzleMessage(index, "mate-in-2 key is not uniquely legal"));
            TEST_ASSERT_FALSE_MESSAGE(
                ChessRules::isCheckmate(afterKey.board),
                puzzleMessage(index, "mate-in-2 key mates one move too early"));

            MoveList defenses;
            ChessRules::generateLegal(afterKey.board, defenses);
            TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
                0, defenses.count,
                puzzleMessage(index, "mate-in-2 key leaves no defensive reply"));

            for (uint16_t reply = 0; reply < defenses.count; ++reply) {
                ChessBoard afterDefense = afterKey.board;
                afterDefense.makeMove(defenses.moves[reply]);
                TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
                    0, countMateInOne(afterDefense),
                    puzzleMessage(index, "a legal defense escapes mate on the next move"));
            }
            break;
        }

        case PuzzleType::Tactic:
            // The current format records a line, but not a machine-checkable
            // evaluation goal. Individual tactic contracts are checked below.
            break;

        default:
            TEST_FAIL_MESSAGE(puzzleMessage(index, "unknown puzzle type"));
        }
    }
}

void test_tactic_contracts() {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16_MESSAGE(
        3, puzzleCountByType(PuzzleType::Tactic),
        "Expected the three documented tactic puzzles");

    // Tactic 1: Nd4-e6+ forks the king on g7 and queen on c5.
    const uint16_t forkOneIndex = puzzleIndexByType(PuzzleType::Tactic, 0);
    LoadedPuzzle forkOne = loadPuzzle(forkOneIndex);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        1, playScriptedMove(forkOne, 0),
        puzzleMessage(forkOneIndex, "scripted fork move is not uniquely legal"));
    assertPiece(forkOne.board, 4, 5, PieceType::Knight, PieceColor::White,
                forkOneIndex, "forking knight must finish on e6");
    assertPiece(forkOne.board, 2, 4, PieceType::Queen, PieceColor::Black,
                forkOneIndex, "fork target queen must be on c5");
    TEST_ASSERT_TRUE_MESSAGE(
        ChessRules::isInCheck(forkOne.board, PieceColor::Black),
        puzzleMessage(forkOneIndex, "fork must check the black king"));
    TEST_ASSERT_TRUE_MESSAGE(
        ChessRules::isAttacked(forkOne.board, 2, 4, PieceColor::White),
        puzzleMessage(forkOneIndex, "fork must attack the queen on c5"));
    TEST_ASSERT_FALSE_MESSAGE(
        sideToMoveCanCapture(forkOne.board, makeSquare(4, 5)),
        puzzleMessage(forkOneIndex, "defender can capture the forking knight"));

    // Tactic 2: Bg5xf6 removes the knight and discovers an attack on Rd8.
    const uint16_t pinIndex = puzzleIndexByType(PuzzleType::Tactic, 1);
    LoadedPuzzle pin = loadPuzzle(pinIndex);
    assertPiece(pin.board, 5, 5, PieceType::Knight, PieceColor::Black,
                pinIndex, "captured tactic target must begin on f6");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        1, playScriptedMove(pin, 0),
        puzzleMessage(pinIndex, "scripted bishop capture is not uniquely legal"));
    assertPiece(pin.board, 5, 5, PieceType::Bishop, PieceColor::White,
                pinIndex, "capturing bishop must finish on f6");
    assertPiece(pin.board, 3, 7, PieceType::Rook, PieceColor::Black,
                pinIndex, "relative-pin target rook must be on d8");
    TEST_ASSERT_TRUE_MESSAGE(
        ChessRules::isAttacked(pin.board, 3, 7, PieceColor::White),
        puzzleMessage(pinIndex, "bishop capture must attack the rook on d8"));
    TEST_ASSERT_FALSE_MESSAGE(
        sideToMoveCanCapture(pin.board, makeSquare(5, 5)),
        puzzleMessage(pinIndex, "defender can immediately recapture the bishop"));

    // Tactic 3: Ne4-f6+ forks the king on g8 and queen on d5.
    const uint16_t forkTwoIndex = puzzleIndexByType(PuzzleType::Tactic, 2);
    LoadedPuzzle forkTwo = loadPuzzle(forkTwoIndex);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        1, playScriptedMove(forkTwo, 0),
        puzzleMessage(forkTwoIndex, "scripted fork move is not uniquely legal"));
    assertPiece(forkTwo.board, 5, 5, PieceType::Knight, PieceColor::White,
                forkTwoIndex, "forking knight must finish on f6");
    assertPiece(forkTwo.board, 3, 4, PieceType::Queen, PieceColor::Black,
                forkTwoIndex, "fork target queen must be on d5");
    assertPiece(forkTwo.board, 5, 6, PieceType::Pawn, PieceColor::Black,
                forkTwoIndex, "pawn on f7 must prevent Rf8xf6");
    TEST_ASSERT_TRUE_MESSAGE(
        ChessRules::isInCheck(forkTwo.board, PieceColor::Black),
        puzzleMessage(forkTwoIndex, "fork must check the black king"));
    TEST_ASSERT_TRUE_MESSAGE(
        ChessRules::isAttacked(forkTwo.board, 3, 4, PieceColor::White),
        puzzleMessage(forkTwoIndex, "fork must attack the queen on d5"));
    TEST_ASSERT_FALSE_MESSAGE(
        sideToMoveCanCapture(forkTwo.board, makeSquare(5, 5)),
        puzzleMessage(forkTwoIndex, "defender can capture the forking knight"));
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_database_shape_and_category_mapping);
    RUN_TEST(test_starting_positions_are_well_formed_and_legal);
    RUN_TEST(test_scripts_are_legal_and_match_type_semantics);
    RUN_TEST(test_tactic_contracts);
    return UNITY_END();
}
