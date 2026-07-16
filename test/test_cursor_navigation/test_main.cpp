#include <unity.h>

#include "cursor_navigation.h"

#include <cstdint>

namespace {

void assertSquare(Square expected, Square actual, const char* message) {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected.col, actual.col, message);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected.row, actual.row, message);
}

bool containsSquare(const Square* cells, uint8_t count, Square target) {
    for (uint8_t i = 0; i < count; ++i) {
        if (cells[i] == target) return true;
    }
    return false;
}

Square toDisplay(Square boardSquare, bool flipped) {
    if (flipped) {
        return Square{static_cast<uint8_t>(7 - boardSquare.col),
                      boardSquare.row};
    }
    return Square{boardSquare.col,
                  static_cast<uint8_t>(7 - boardSquare.row)};
}

Square toBoard(Square displaySquare, bool flipped) {
    if (flipped) {
        return Square{static_cast<uint8_t>(7 - displaySquare.col),
                      displaySquare.row};
    }
    return Square{displaySquare.col,
                  static_cast<uint8_t>(7 - displaySquare.row)};
}

void test_rejects_invalid_input_without_changing_output() {
    const Square valid[] = {Square{1, 1}, Square{2, 2}};
    const Square invalid[] = {NO_SQUARE, Square{8, 0}, Square{0, 8}};
    Square next{6, 6};

    TEST_ASSERT_FALSE(chooseCursorDestination(
        nullptr, 2, Square{0, 0}, CursorDirection::Right, next));
    assertSquare(Square{6, 6}, next, "null input changed output");

    TEST_ASSERT_FALSE(chooseCursorDestination(
        valid, 0, Square{0, 0}, CursorDirection::Right, next));
    assertSquare(Square{6, 6}, next, "empty input changed output");

    TEST_ASSERT_FALSE(chooseCursorDestination(
        valid, 2, NO_SQUARE, CursorDirection::Right, next));
    assertSquare(Square{6, 6}, next, "invalid current changed output");

    TEST_ASSERT_FALSE(chooseCursorDestination(
        invalid, 3, Square{0, 0}, CursorDirection::Right, next));
    assertSquare(Square{6, 6}, next, "invalid candidates changed output");

    TEST_ASSERT_FALSE(chooseCursorDestination(
        valid, 2, Square{0, 0}, static_cast<CursorDirection>(0xFF), next));
    assertSquare(Square{6, 6}, next, "invalid direction changed output");
}

void test_rook_destinations_follow_cardinal_directions() {
    const Square destinations[] = {
        Square{3, 0}, Square{3, 7}, Square{0, 3}, Square{7, 3},
    };
    const Square current{3, 3};
    Square next;

    TEST_ASSERT_TRUE(chooseCursorDestination(
        destinations, 4, current, CursorDirection::Up, next));
    assertSquare(Square{3, 0}, next, "rook up");
    TEST_ASSERT_TRUE(chooseCursorDestination(
        destinations, 4, current, CursorDirection::Down, next));
    assertSquare(Square{3, 7}, next, "rook down");
    TEST_ASSERT_TRUE(chooseCursorDestination(
        destinations, 4, current, CursorDirection::Left, next));
    assertSquare(Square{0, 3}, next, "rook left");
    TEST_ASSERT_TRUE(chooseCursorDestination(
        destinations, 4, current, CursorDirection::Right, next));
    assertSquare(Square{7, 3}, next, "rook right");
}

void test_diagonal_knight_and_pawn_geometry_is_deterministic() {
    Square next;

    const Square bishopDestinations[] = {
        Square{1, 1}, Square{5, 1}, Square{1, 5}, Square{5, 5},
    };
    TEST_ASSERT_TRUE(chooseCursorDestination(
        bishopDestinations, 4, Square{3, 3}, CursorDirection::Up, next));
    assertSquare(Square{1, 1}, next, "bishop tie did not use row/column order");

    const Square knightDestinations[] = {
        Square{5, 2}, Square{5, 4}, Square{4, 1}, Square{4, 5},
        Square{1, 2}, Square{1, 4}, Square{2, 1}, Square{2, 5},
    };
    TEST_ASSERT_TRUE(chooseCursorDestination(
        knightDestinations, 8, Square{3, 3}, CursorDirection::Right, next));
    assertSquare(Square{5, 2}, next, "knight tie did not use row/column order");

    const Square pawnDestinations[] = {
        Square{4, 2}, Square{4, 3}, Square{3, 2}, Square{5, 2},
    };
    TEST_ASSERT_TRUE(chooseCursorDestination(
        pawnDestinations, 4, Square{4, 1}, CursorDirection::Down, next));
    assertSquare(Square{4, 2}, next, "pawn should prefer its forward axis");
}

void test_axis_distance_precedes_forward_distance() {
    const Square destinations[] = {
        Square{7, 3}, // On-axis but farther forward.
        Square{4, 4}, // One row off-axis but nearer.
        Square{6, 3}, // On-axis and nearer than the first candidate.
    };
    Square next;

    TEST_ASSERT_TRUE(chooseCursorDestination(
        destinations, 3, Square{3, 3}, CursorDirection::Right, next));
    assertSquare(Square{6, 3}, next,
                 "axis/forward ranking order is incorrect");
}

void test_wraps_to_opposite_edges_in_all_directions() {
    Square next;

    const Square wrapRight[] = {
        Square{0, 4}, Square{1, 3}, Square{0, 2},
    };
    TEST_ASSERT_TRUE(chooseCursorDestination(
        wrapRight, 3, Square{3, 3}, CursorDirection::Right, next));
    assertSquare(Square{0, 2}, next, "right wrap");

    const Square wrapLeft[] = {
        Square{7, 4}, Square{6, 3}, Square{7, 2},
    };
    TEST_ASSERT_TRUE(chooseCursorDestination(
        wrapLeft, 3, Square{3, 3}, CursorDirection::Left, next));
    assertSquare(Square{7, 2}, next, "left wrap");

    const Square wrapDown[] = {
        Square{4, 0}, Square{3, 1}, Square{2, 0},
    };
    TEST_ASSERT_TRUE(chooseCursorDestination(
        wrapDown, 3, Square{3, 3}, CursorDirection::Down, next));
    assertSquare(Square{2, 0}, next, "down wrap");

    const Square wrapUp[] = {
        Square{4, 7}, Square{3, 6}, Square{2, 7},
    };
    TEST_ASSERT_TRUE(chooseCursorDestination(
        wrapUp, 3, Square{3, 3}, CursorDirection::Up, next));
    assertSquare(Square{2, 7}, next, "up wrap");
}

void test_repeated_navigation_never_leaves_legal_destinations() {
    const Square destinations[] = {
        Square{0, 1}, Square{2, 0}, Square{5, 2}, Square{7, 6},
        Square{4, 7}, Square{1, 5},
    };
    const CursorDirection sequence[] = {
        CursorDirection::Right, CursorDirection::Down,
        CursorDirection::Left, CursorDirection::Up,
        CursorDirection::Up, CursorDirection::Right,
        CursorDirection::Right, CursorDirection::Down,
    };
    Square current{3, 3};

    for (uint8_t pass = 0; pass < 8; ++pass) {
        for (CursorDirection direction : sequence) {
            Square next = NO_SQUARE;
            TEST_ASSERT_TRUE(chooseCursorDestination(
                destinations, 6, current, direction, next));
            TEST_ASSERT_TRUE_MESSAGE(
                containsSquare(destinations, 6, next),
                "navigation selected a cell outside the legal set");
            current = next;
        }
    }
}

void test_flipped_board_directions_use_display_coordinates() {
    // From Black's perspective, board d6 is visually to the right of e7 even
    // though its board-file coordinate is smaller.
    const Square originBoard{4, 6}; // e7
    const Square legalBoard[] = {
        Square{4, 5}, // e6
        Square{3, 5}, // d6
        Square{5, 5}, // f6
    };
    Square legalDisplay[3];
    for (uint8_t i = 0; i < 3; ++i) {
        legalDisplay[i] = toDisplay(legalBoard[i], true);
    }

    const Square originDisplay = toDisplay(originBoard, true);
    Square nextDisplay;
    TEST_ASSERT_TRUE(chooseCursorDestination(
        legalDisplay, 3, originDisplay, CursorDirection::Right, nextDisplay));
    assertSquare(Square{3, 5}, toBoard(nextDisplay, true),
                 "flipped visual-right did not choose board d6");
}

void test_duplicate_promotion_destinations_are_effectively_deduplicated() {
    // A promotion produces four legal Moves with the same destination cell.
    const Square destinations[] = {
        Square{2, 0}, Square{2, 0}, Square{2, 0}, Square{2, 0},
        Square{3, 0},
    };
    Square next;

    TEST_ASSERT_TRUE(chooseCursorDestination(
        destinations, 5, Square{2, 1}, CursorDirection::Up, next));
    assertSquare(Square{2, 0}, next,
                 "promotion duplicates changed destination selection");

    TEST_ASSERT_TRUE(chooseCursorDestination(
        destinations, 5, next, CursorDirection::Right, next));
    assertSquare(Square{3, 0}, next,
                 "promotion duplicates prevented navigation to another cell");
}

void test_one_destination_is_reached_from_every_direction() {
    const Square destination[] = {Square{6, 1}};
    const CursorDirection directions[] = {
        CursorDirection::Up, CursorDirection::Down,
        CursorDirection::Left, CursorDirection::Right,
    };

    for (CursorDirection direction : directions) {
        Square next = NO_SQUARE;
        TEST_ASSERT_TRUE(chooseCursorDestination(
            destination, 1, Square{3, 3}, direction, next));
        assertSquare(destination[0], next,
                     "single destination was not selected by wrapping");
    }
}

void test_zero_displacement_destination_is_retained_only_when_necessary() {
    const Square current{6, 0};
    const Square onlyCurrent[] = {current, current};
    Square next = NO_SQUARE;

    TEST_ASSERT_TRUE(chooseCursorDestination(
        onlyCurrent, 2, current, CursorDirection::Right, next));
    assertSquare(current, next,
                 "sole Chess960 zero-displacement destination was discarded");

    const Square withAlternatives[] = {
        current, current, Square{1, 0}, Square{7, 0},
    };
    const CursorDirection directions[] = {
        CursorDirection::Up, CursorDirection::Down,
        CursorDirection::Left, CursorDirection::Right,
    };
    for (CursorDirection direction : directions) {
        next = current;
        TEST_ASSERT_TRUE(chooseCursorDestination(
            withAlternatives, 4, current, direction, next));
        TEST_ASSERT_FALSE_MESSAGE(
            next == current,
            "current cell was selected despite another legal destination");
    }
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rejects_invalid_input_without_changing_output);
    RUN_TEST(test_rook_destinations_follow_cardinal_directions);
    RUN_TEST(test_diagonal_knight_and_pawn_geometry_is_deterministic);
    RUN_TEST(test_axis_distance_precedes_forward_distance);
    RUN_TEST(test_wraps_to_opposite_edges_in_all_directions);
    RUN_TEST(test_repeated_navigation_never_leaves_legal_destinations);
    RUN_TEST(test_flipped_board_directions_use_display_coordinates);
    RUN_TEST(test_duplicate_promotion_destinations_are_effectively_deduplicated);
    RUN_TEST(test_one_destination_is_reached_from_every_direction);
    RUN_TEST(test_zero_displacement_destination_is_retained_only_when_necessary);
    return UNITY_END();
}
