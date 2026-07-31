#include <unity.h>

#include "terminal_reliability.h"

#include <cstdint>
#include <cstring>

namespace {

using TerminalReliability::ImmutableQueuedControl;
using TerminalReliability::ResultCommitDecision;
using TerminalReliability::decideResultCommit;
using TerminalReliability::summariesExactlyEqual;

GameSummary makeSummary(uint32_t gameId = 0x12345678u) {
    GameSummary summary;
    summary.gameId = gameId;
    summary.whiteProfileId = 11;
    summary.blackProfileId = 22;
    std::strncpy(summary.whiteName, "Alice", sizeof(summary.whiteName));
    std::strncpy(summary.blackName, "Bob", sizeof(summary.blackName));
    summary.mode = GameMode::Online;
    summary.variant = ChessVariant::Chess960;
    summary.positionIndex = 959;
    summary.timeControl = TimeControl::Rapid10;
    summary.aiDifficulty = 0;
    summary.localColor = PieceColor::Black;
    summary.outcome = GameOutcome::WhiteWin;
    summary.termination = TerminationReason::Checkmate;
    summary.plyCount = 251;
    summary.whiteRemainingMs = 1234;
    summary.blackRemainingMs = 5678;
    return summary;
}

void assertSummaryDiffers(const GameSummary& expected,
                          const GameSummary& changed) {
    TEST_ASSERT_FALSE(summariesExactlyEqual(expected, changed));
    TEST_ASSERT_FALSE(summariesExactlyEqual(changed, expected));
}

void test_summary_equality_covers_every_persisted_field() {
    const GameSummary base = makeSummary();
    TEST_ASSERT_TRUE(summariesExactlyEqual(base, base));
    TEST_ASSERT_TRUE(summariesExactlyEqual(base, GameSummary(base)));

    GameSummary changed = base;
    changed.gameId++;
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.whiteProfileId++;
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.blackProfileId++;
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.whiteName[0] = 'E';
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.blackName[0] = 'R';
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.mode = GameMode::Local;
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.variant = ChessVariant::Standard;
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.positionIndex--;
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.timeControl = TimeControl::Blitz5;
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.aiDifficulty = 3;
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.localColor = PieceColor::White;
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.outcome = GameOutcome::BlackWin;
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.termination = TerminationReason::Timeout;
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.plyCount--;
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.whiteRemainingMs++;
    assertSummaryDiffers(base, changed);
    changed = base;
    changed.blackRemainingMs++;
    assertSummaryDiffers(base, changed);
}

void test_summary_equality_is_exact_for_fixed_name_storage() {
    const GameSummary base = makeSummary();
    GameSummary changed = base;

    // The visible name is still "Alice", but a serialized byte after its NUL
    // differs. Exact persisted-result equality must detect that difference.
    changed.whiteName[6] = 'x';
    assertSummaryDiffers(base, changed);
}

void test_commit_decision_distinguishes_absent_same_and_conflict() {
    GameSummary history[3] = {
        makeSummary(100), makeSummary(200), makeSummary(300)
    };

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ResultCommitDecision::RecordAbsent),
        static_cast<uint8_t>(decideResultCommit(
            history, 3, makeSummary(400))));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ResultCommitDecision::AlreadyCommitted),
        static_cast<uint8_t>(decideResultCommit(
            history, 3, makeSummary(200))));

    GameSummary conflicting = makeSummary(200);
    conflicting.plyCount++;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ResultCommitDecision::Conflict),
        static_cast<uint8_t>(decideResultCommit(
            history, 3, conflicting)));
}

void test_commit_decision_rejects_invalid_inputs_and_malformed_counts() {
    const GameSummary candidate = makeSummary();
    GameSummary invalid = candidate;
    invalid.gameId = 0;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ResultCommitDecision::InvalidInput),
        static_cast<uint8_t>(decideResultCommit(nullptr, 1, candidate)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ResultCommitDecision::InvalidInput),
        static_cast<uint8_t>(decideResultCommit(nullptr, 0, invalid)));

    ProfileData profiles;
    profiles.historyCount = MAX_GAME_SUMMARIES + 1;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ResultCommitDecision::InvalidInput),
        static_cast<uint8_t>(decideResultCommit(profiles, candidate)));
}

void test_commit_decision_does_not_hide_conflicting_duplicate_ids() {
    const GameSummary candidate = makeSummary(500);
    GameSummary history[2] = {candidate, candidate};
    history[1].blackRemainingMs++;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ResultCommitDecision::Conflict),
        static_cast<uint8_t>(decideResultCommit(history, 2, candidate)));
}

ControlNetMsg makeTerminalControl() {
    ControlNetMsg message;
    message.header.gameId = 0x4321;
    message.header.sessionId = 0x89abcdefu;
    message.eventId = 77;
    message.control = NetControlType::GameEnd;
    message.arg0 = static_cast<uint8_t>(NetGameResult::WhiteWin);
    message.arg1 = static_cast<uint8_t>(NetTermination::Checkmate);
    message.relatedEventId = 0;
    message.valueMs = 0;
    message.whiteRemainingMs = 4567;
    message.blackRemainingMs = 8901;
    message.boardHash = 0x10203040u;
    return message;
}

void test_queued_control_capture_is_independent_of_source_mutation() {
    ControlNetMsg source = makeTerminalControl();
    const ControlNetMsg expected = source;
    const ImmutableQueuedControl queued(source);

    source.arg0 = static_cast<uint8_t>(NetGameResult::Draw);
    source.whiteRemainingMs = 0;
    source.boardHash = 0;

    const ControlNetMsg promoted = queued.promote();
    TEST_ASSERT_EQUAL_MEMORY(&expected, &promoted, sizeof(expected));
    TEST_ASSERT_EQUAL_MEMORY(&expected, &queued.captured(), sizeof(expected));
}

void test_event_id_reuse_changes_only_event_id() {
    const ControlNetMsg captured = makeTerminalControl();
    const ImmutableQueuedControl queued(captured);

    ControlNetMsg promoted = queued.promoteReusingEventId(12);
    TEST_ASSERT_EQUAL_UINT16(12, promoted.eventId);
    promoted.eventId = captured.eventId;
    TEST_ASSERT_EQUAL_MEMORY(&captured, &promoted, sizeof(captured));

    // Reuse does not mutate the frozen packet or affect later promotions.
    TEST_ASSERT_EQUAL_MEMORY(&captured, &queued.captured(), sizeof(captured));
    const ControlNetMsg normal = queued.promote();
    TEST_ASSERT_EQUAL_MEMORY(&captured, &normal, sizeof(captured));
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_summary_equality_covers_every_persisted_field);
    RUN_TEST(test_summary_equality_is_exact_for_fixed_name_storage);
    RUN_TEST(test_commit_decision_distinguishes_absent_same_and_conflict);
    RUN_TEST(test_commit_decision_rejects_invalid_inputs_and_malformed_counts);
    RUN_TEST(test_commit_decision_does_not_hide_conflicting_duplicate_ids);
    RUN_TEST(test_queued_control_capture_is_independent_of_source_mutation);
    RUN_TEST(test_event_id_reuse_changes_only_event_id);
    return UNITY_END();
}
