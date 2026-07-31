#include <unity.h>

#include "chess_clock.h"

#include <cstdint>
#include <limits>

namespace {

void test_conventional_first_move_starts_white_at_ready() {
    ChessClock clock(TimeControl::Bullet1);

    TEST_ASSERT_EQUAL_UINT32(60000, clock.remainingMs(PieceColor::White));
    TEST_ASSERT_EQUAL_UINT32(60000, clock.remainingMs(PieceColor::Black));
    TEST_ASSERT_TRUE(clock.start(1000));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PieceColor::White),
                            static_cast<uint8_t>(clock.activeSide()));
    TEST_ASSERT_EQUAL_UINT32(57500,
                             clock.remainingMsAt(PieceColor::White, 3500));
    TEST_ASSERT_EQUAL_UINT32(60000,
                             clock.remainingMsAt(PieceColor::Black, 3500));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessClockMoveResult::Applied),
        static_cast<uint8_t>(clock.commitMove(PieceColor::White, 3500)));
    TEST_ASSERT_EQUAL_UINT32(57500, clock.remainingMs(PieceColor::White));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PieceColor::Black),
                            static_cast<uint8_t>(clock.activeSide()));
    TEST_ASSERT_EQUAL_UINT32(59000,
                             clock.remainingMsAt(PieceColor::Black, 4500));
}

void test_increment_applies_to_every_move_including_whites_first() {
    ChessClock clock(TimeControl::Blitz3);
    TEST_ASSERT_TRUE(clock.start(100));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessClockMoveResult::Applied),
        static_cast<uint8_t>(clock.commitMove(PieceColor::White, 1100)));
    TEST_ASSERT_EQUAL_UINT32(181000, clock.remainingMs(PieceColor::White));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessClockMoveResult::Applied),
        static_cast<uint8_t>(clock.commitMove(PieceColor::Black, 3100)));
    TEST_ASSERT_EQUAL_UINT32(180000, clock.remainingMs(PieceColor::Black));
    TEST_ASSERT_EQUAL_UINT32(180000,
                             clock.remainingMsAt(PieceColor::White, 4100));
}

void test_terminal_pause_at_move_timestamp_does_not_charge_next_side() {
    ChessClock clock(TimeControl::Blitz3);
    TEST_ASSERT_TRUE(clock.start(100));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessClockMoveResult::Applied),
        static_cast<uint8_t>(clock.commitMove(PieceColor::White, 1100)));

    // A terminal board result is adjudicated at the move timestamp. The side
    // that no longer has a legal move must not lose time to SAN/UI processing.
    TEST_ASSERT_TRUE(clock.pause(1100));
    TEST_ASSERT_EQUAL_UINT32(180000,
                             clock.remainingMs(PieceColor::Black));
}

void test_elapsed_time_uses_exact_timestamps_not_frame_frequency() {
    ChessClock clock(TimeControl::Rapid10);
    TEST_ASSERT_TRUE(clock.start(500));

    // No intermediate updates are required, even across a long frame gap.
    clock.update(450500);
    TEST_ASSERT_EQUAL_UINT32(150000, clock.remainingMs(PieceColor::White));
    TEST_ASSERT_FALSE(clock.hasFlaggedSide());

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessClockMoveResult::Applied),
        static_cast<uint8_t>(clock.commitMove(PieceColor::White, 500500)));
    TEST_ASSERT_EQUAL_UINT32(100000, clock.remainingMs(PieceColor::White));
}

void test_pause_charges_to_pause_and_resume_excludes_paused_time() {
    ChessClock clock(TimeControl::Bullet1);
    TEST_ASSERT_TRUE(clock.start(1000));
    TEST_ASSERT_TRUE(clock.pause(6000));
    TEST_ASSERT_EQUAL_UINT32(55000, clock.remainingMs(PieceColor::White));
    TEST_ASSERT_EQUAL_UINT32(55000,
                             clock.remainingMsAt(PieceColor::White, 500000));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessClockMoveResult::Paused),
        static_cast<uint8_t>(clock.commitMove(PieceColor::White, 500000)));

    TEST_ASSERT_TRUE(clock.resume(8000));
    clock.update(10000);
    TEST_ASSERT_EQUAL_UINT32(53000, clock.remainingMs(PieceColor::White));
}

void test_timeout_at_exact_deadline_rejects_move_and_records_flag() {
    ChessClock clock(TimeControl::Bullet1);
    TEST_ASSERT_TRUE(clock.start(10));

    TEST_ASSERT_FALSE(clock.flaggedAt(PieceColor::White, 60009));
    TEST_ASSERT_TRUE(clock.flaggedAt(PieceColor::White, 60010));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessClockMoveResult::Flagged),
        static_cast<uint8_t>(clock.commitMove(PieceColor::White, 60010)));
    TEST_ASSERT_TRUE(clock.hasFlaggedSide());
    TEST_ASSERT_TRUE(clock.flagged(PieceColor::White));
    TEST_ASSERT_FALSE(clock.isRunning());
    TEST_ASSERT_EQUAL_UINT32(0, clock.remainingMs(PieceColor::White));
}

void test_authoritative_sync_and_exact_once_add_time() {
    ChessClock clock(TimeControl::Bullet1);
    TEST_ASSERT_TRUE(clock.start(1000));
    clock.update(6000);

    TEST_ASSERT_TRUE(clock.synchronizeRemainingMs(50000, 48000, 7000));
    TEST_ASSERT_EQUAL_UINT32(50000, clock.remainingMs(PieceColor::White));
    TEST_ASSERT_EQUAL_UINT32(48000, clock.remainingMs(PieceColor::Black));
    TEST_ASSERT_EQUAL_UINT32(49000,
                             clock.remainingMsAt(PieceColor::White, 8000));

    TEST_ASSERT_TRUE(
        clock.addTimeOnce(PieceColor::Black, 15000, 42, 8000));
    TEST_ASSERT_EQUAL_UINT32(63000, clock.remainingMs(PieceColor::Black));
    TEST_ASSERT_EQUAL_UINT32(49000, clock.remainingMs(PieceColor::White));

    TEST_ASSERT_FALSE(
        clock.addTimeOnce(PieceColor::Black, 15000, 42, 9000));
    TEST_ASSERT_FALSE(
        clock.addTimeOnce(PieceColor::Black, 15000, 41, 9000));
    TEST_ASSERT_EQUAL_UINT32(63000, clock.remainingMs(PieceColor::Black));

    TEST_ASSERT_TRUE(clock.addTimeOnce(PieceColor::Black,
                                       std::numeric_limits<uint32_t>::max(),
                                       43, 9000));
    TEST_ASSERT_EQUAL_UINT32(std::numeric_limits<uint32_t>::max(),
                             clock.remainingMs(PieceColor::Black));
    TEST_ASSERT_TRUE(clock.setRemainingMs(PieceColor::White, 47000, 10000));
    TEST_ASSERT_EQUAL_UINT32(47000, clock.remainingMs(PieceColor::White));
}

void test_no_timer_is_inert() {
    ChessClock clock(TimeControl::None);

    TEST_ASSERT_FALSE(clock.enabled());
    TEST_ASSERT_FALSE(clock.start(100));
    TEST_ASSERT_FALSE(clock.pause(200));
    TEST_ASSERT_FALSE(clock.resume(300));
    TEST_ASSERT_FALSE(clock.setRemainingMs(PieceColor::White, 1000, 400));
    TEST_ASSERT_FALSE(clock.addTimeOnce(PieceColor::Black, 15000, 1, 500));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessClockMoveResult::Disabled),
        static_cast<uint8_t>(clock.commitMove(PieceColor::White, 600)));
    TEST_ASSERT_EQUAL_UINT32(0,
                             clock.remainingMsAt(PieceColor::White, 1000000));
    TEST_ASSERT_FALSE(clock.hasFlaggedSide());
}

void test_uint32_timestamp_wrap_is_accounted_for() {
    ChessClock clock(TimeControl::Bullet1);
    const uint32_t ready = std::numeric_limits<uint32_t>::max() - 999;
    TEST_ASSERT_TRUE(clock.start(ready));

    // 1,500 ms elapsed: 1,000 ms to rollover and 500 ms after it.
    clock.update(500);
    TEST_ASSERT_EQUAL_UINT32(58500, clock.remainingMs(PieceColor::White));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessClockMoveResult::Applied),
        static_cast<uint8_t>(clock.commitMove(PieceColor::White, 1500)));
    TEST_ASSERT_EQUAL_UINT32(57500, clock.remainingMs(PieceColor::White));
}

void test_snapshot_settles_and_load_reanchors_running_clock() {
    ChessClock original(TimeControl::Blitz5);
    TEST_ASSERT_TRUE(original.start(1000));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessClockMoveResult::Applied),
        static_cast<uint8_t>(original.commitMove(PieceColor::White, 5000)));
    TEST_ASSERT_TRUE(original.addTimeOnce(PieceColor::White, 15000, 77, 7000));

    const ChessClockSnapshot saved = original.snapshot(9000);
    TEST_ASSERT_EQUAL_UINT32(314000, saved.whiteRemainingMs);
    TEST_ASSERT_EQUAL_UINT32(296000, saved.blackRemainingMs);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PieceColor::Black),
                            static_cast<uint8_t>(saved.activeSide));
    TEST_ASSERT_TRUE(saved.hasLastAddTimeEvent);

    ChessClock restored;
    TEST_ASSERT_TRUE(restored.loadSnapshot(saved, 100000));
    TEST_ASSERT_EQUAL_UINT32(295000,
                             restored.remainingMsAt(PieceColor::Black,
                                                    101000));
    TEST_ASSERT_FALSE(
        restored.addTimeOnce(PieceColor::White, 15000, 77, 101000));
    TEST_ASSERT_TRUE(
        restored.addTimeOnce(PieceColor::White, 15000, 78, 101000));
}

void test_snapshot_of_elapsed_timeout_restores_terminal_flag() {
    ChessClock original(TimeControl::Bullet1);
    TEST_ASSERT_TRUE(original.start(500));

    const ChessClockSnapshot saved = original.snapshot(60500);
    TEST_ASSERT_TRUE(saved.hasFlaggedSide);
    TEST_ASSERT_EQUAL_UINT32(0, saved.whiteRemainingMs);

    ChessClock restored;
    TEST_ASSERT_TRUE(restored.loadSnapshot(saved, 90000));
    TEST_ASSERT_TRUE(restored.flagged(PieceColor::White));
    TEST_ASSERT_FALSE(restored.isRunning());
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_conventional_first_move_starts_white_at_ready);
    RUN_TEST(test_increment_applies_to_every_move_including_whites_first);
    RUN_TEST(test_terminal_pause_at_move_timestamp_does_not_charge_next_side);
    RUN_TEST(test_elapsed_time_uses_exact_timestamps_not_frame_frequency);
    RUN_TEST(test_pause_charges_to_pause_and_resume_excludes_paused_time);
    RUN_TEST(test_timeout_at_exact_deadline_rejects_move_and_records_flag);
    RUN_TEST(test_authoritative_sync_and_exact_once_add_time);
    RUN_TEST(test_no_timer_is_inert);
    RUN_TEST(test_uint32_timestamp_wrap_is_accounted_for);
    RUN_TEST(test_snapshot_settles_and_load_reanchors_running_clock);
    RUN_TEST(test_snapshot_of_elapsed_timeout_restores_terminal_flag);
    return UNITY_END();
}
