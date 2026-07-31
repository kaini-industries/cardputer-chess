#include <unity.h>

#include "game_records.h"

#include <cstdint>
#include <cstring>
#include <limits>

namespace {

ProfileData makeProfiles() {
    ProfileData data;
    GameRecords::initialize(data, 101, "Alice");
    return data;
}

GameSummary makeSummary(uint32_t gameId, GameOutcome outcome,
                        uint32_t whiteId = 101, uint32_t blackId = 202) {
    GameSummary summary;
    summary.gameId = gameId;
    summary.whiteProfileId = whiteId;
    summary.blackProfileId = blackId;
    std::strncpy(summary.whiteName, "Alice", sizeof(summary.whiteName));
    std::strncpy(summary.blackName, "Bob", sizeof(summary.blackName));
    summary.outcome = outcome;
    summary.termination = outcome == GameOutcome::Draw
                        ? TerminationReason::Agreement
                        : TerminationReason::Checkmate;
    summary.plyCount = 42;
    return summary;
}

void test_sanitize_rejects_missing_or_blank_names() {
    char output[PLAYER_NAME_MAX + 1];
    std::memset(output, 'x', sizeof(output));

    TEST_ASSERT_FALSE(GameRecords::sanitizeName(nullptr, output));
    TEST_ASSERT_EQUAL_STRING("", output);
    TEST_ASSERT_FALSE(GameRecords::sanitizeName("   ", output));
    TEST_ASSERT_EQUAL_STRING("", output);
    TEST_ASSERT_FALSE(GameRecords::sanitizeName("\n\t\x7f", output));
    TEST_ASSERT_EQUAL_STRING("", output);
}

void test_sanitize_trims_filters_and_truncates() {
    char output[PLAYER_NAME_MAX + 1];

    TEST_ASSERT_TRUE(GameRecords::sanitizeName("  Alice Smith  ", output));
    TEST_ASSERT_EQUAL_STRING("Alice Smith", output);

    TEST_ASSERT_TRUE(GameRecords::sanitizeName("\t  A\nli\x7f" "ce  ", output));
    TEST_ASSERT_EQUAL_STRING("Alice", output);

    TEST_ASSERT_TRUE(GameRecords::sanitizeName("abcdefghijklmnop", output));
    TEST_ASSERT_EQUAL_STRING("abcdefghijkl", output);
    TEST_ASSERT_EQUAL_UINT8(PLAYER_NAME_MAX, std::strlen(output));
}

void test_sanitize_supports_in_place_input_output() {
    char name[PLAYER_NAME_MAX + 1] = "  Alice  ";
    TEST_ASSERT_TRUE(GameRecords::sanitizeName(name, name));
    TEST_ASSERT_EQUAL_STRING("Alice", name);
}

void test_initialize_creates_safe_default_profile() {
    ProfileData data;
    std::memset(&data, 0xff, sizeof(data));

    GameRecords::initialize(data, 0, "   ");

    TEST_ASSERT_EQUAL_UINT8(1, data.profileCount);
    TEST_ASSERT_EQUAL_UINT8(0, data.activeIndex);
    TEST_ASSERT_EQUAL_UINT32(1, data.profiles[0].id);
    TEST_ASSERT_EQUAL_STRING("Player", data.profiles[0].name);
    TEST_ASSERT_EQUAL_UINT8(0, data.historyCount);
    TEST_ASSERT_EQUAL_UINT32(0, data.profiles[0].games);
}

void test_profile_add_find_rename_and_activate() {
    ProfileData data = makeProfiles();

    TEST_ASSERT_TRUE(GameRecords::addProfile(data, 202, "  Bob  "));
    TEST_ASSERT_EQUAL_UINT8(2, data.profileCount);
    TEST_ASSERT_EQUAL_UINT8(1, data.activeIndex);
    TEST_ASSERT_EQUAL_STRING("Bob", GameRecords::activeProfile(data)->name);
    TEST_ASSERT_EQUAL_PTR(&data.profiles[0], GameRecords::findProfile(data, 101));
    TEST_ASSERT_EQUAL_PTR(&data.profiles[1], GameRecords::findProfile(data, 202));

    TEST_ASSERT_TRUE(GameRecords::renameProfile(data, 1, "Bobby Tables"));
    TEST_ASSERT_EQUAL_STRING("Bobby Tables", data.profiles[1].name);
    TEST_ASSERT_TRUE(GameRecords::setActiveProfile(data, 0));
    TEST_ASSERT_EQUAL_UINT32(101, GameRecords::activeProfile(data)->id);
}

void test_profile_operations_reject_invalid_data() {
    ProfileData data = makeProfiles();

    TEST_ASSERT_FALSE(GameRecords::addProfile(data, 0, "Bob"));
    TEST_ASSERT_FALSE(GameRecords::addProfile(data, 101, "Duplicate ID"));
    TEST_ASSERT_FALSE(GameRecords::addProfile(data, 202, " \n "));
    TEST_ASSERT_FALSE(GameRecords::renameProfile(data, 1, "Bob"));
    TEST_ASSERT_FALSE(GameRecords::renameProfile(data, 0, "   "));
    TEST_ASSERT_FALSE(GameRecords::setActiveProfile(data, 1));
    TEST_ASSERT_NULL(GameRecords::findProfile(data, 0));
    TEST_ASSERT_NULL(GameRecords::findProfile(data, 999));
}

void test_profiles_stop_at_capacity() {
    ProfileData data = makeProfiles();
    TEST_ASSERT_TRUE(GameRecords::addProfile(data, 202, "Bob"));
    TEST_ASSERT_TRUE(GameRecords::addProfile(data, 303, "Carol"));
    TEST_ASSERT_TRUE(GameRecords::addProfile(data, 404, "Dave"));
    TEST_ASSERT_EQUAL_UINT8(MAX_PLAYER_PROFILES, data.profileCount);
    TEST_ASSERT_FALSE(GameRecords::addProfile(data, 505, "Eve"));
    TEST_ASSERT_EQUAL_UINT8(MAX_PLAYER_PROFILES, data.profileCount);
}

void test_delete_active_profile_compacts_and_selects_first() {
    ProfileData data = makeProfiles();
    GameRecords::addProfile(data, 202, "Bob");
    GameRecords::addProfile(data, 303, "Carol");
    TEST_ASSERT_EQUAL_UINT8(2, data.activeIndex);

    TEST_ASSERT_TRUE(GameRecords::deleteProfile(data, 2));
    TEST_ASSERT_EQUAL_UINT8(2, data.profileCount);
    TEST_ASSERT_EQUAL_UINT8(0, data.activeIndex);
    TEST_ASSERT_EQUAL_STRING("Alice", GameRecords::activeProfile(data)->name);
    TEST_ASSERT_EQUAL_UINT32(0, data.profiles[2].id);
}

void test_delete_before_active_preserves_same_active_profile() {
    ProfileData data = makeProfiles();
    GameRecords::addProfile(data, 202, "Bob");
    GameRecords::addProfile(data, 303, "Carol");

    TEST_ASSERT_TRUE(GameRecords::deleteProfile(data, 0));
    TEST_ASSERT_EQUAL_UINT8(2, data.profileCount);
    TEST_ASSERT_EQUAL_UINT8(1, data.activeIndex);
    TEST_ASSERT_EQUAL_UINT32(303, GameRecords::activeProfile(data)->id);
    TEST_ASSERT_EQUAL_UINT32(202, data.profiles[0].id);
}

void test_cannot_delete_only_or_out_of_range_profile() {
    ProfileData data = makeProfiles();
    TEST_ASSERT_FALSE(GameRecords::deleteProfile(data, 0));
    TEST_ASSERT_FALSE(GameRecords::deleteProfile(data, 1));
    TEST_ASSERT_EQUAL_UINT8(1, data.profileCount);
}

void test_white_win_updates_both_profiles_once() {
    ProfileData data = makeProfiles();
    GameRecords::addProfile(data, 202, "Bob");
    const GameSummary summary = makeSummary(1, GameOutcome::WhiteWin);

    TEST_ASSERT_TRUE(GameRecords::recordCompletedGame(data, summary));
    TEST_ASSERT_EQUAL_UINT32(1, data.profiles[0].games);
    TEST_ASSERT_EQUAL_UINT32(1, data.profiles[0].wins);
    TEST_ASSERT_EQUAL_UINT32(0, data.profiles[0].losses);
    TEST_ASSERT_EQUAL_UINT32(1, data.profiles[1].games);
    TEST_ASSERT_EQUAL_UINT32(0, data.profiles[1].wins);
    TEST_ASSERT_EQUAL_UINT32(1, data.profiles[1].losses);
}

void test_black_win_and_draw_update_correct_statistics() {
    ProfileData data = makeProfiles();
    GameRecords::addProfile(data, 202, "Bob");

    TEST_ASSERT_TRUE(GameRecords::recordCompletedGame(
        data, makeSummary(1, GameOutcome::BlackWin)));
    TEST_ASSERT_TRUE(GameRecords::recordCompletedGame(
        data, makeSummary(2, GameOutcome::Draw)));

    TEST_ASSERT_EQUAL_UINT32(2, data.profiles[0].games);
    TEST_ASSERT_EQUAL_UINT32(0, data.profiles[0].wins);
    TEST_ASSERT_EQUAL_UINT32(1, data.profiles[0].losses);
    TEST_ASSERT_EQUAL_UINT32(1, data.profiles[0].draws);
    TEST_ASSERT_EQUAL_UINT32(2, data.profiles[1].games);
    TEST_ASSERT_EQUAL_UINT32(1, data.profiles[1].wins);
    TEST_ASSERT_EQUAL_UINT32(0, data.profiles[1].losses);
    TEST_ASSERT_EQUAL_UINT32(1, data.profiles[1].draws);
}

void test_unrecognized_profile_ids_do_not_block_history() {
    ProfileData data = makeProfiles();
    const GameSummary summary = makeSummary(1, GameOutcome::Draw, 999, 998);

    TEST_ASSERT_TRUE(GameRecords::recordCompletedGame(data, summary));
    TEST_ASSERT_EQUAL_UINT8(1, data.historyCount);
    TEST_ASSERT_EQUAL_UINT32(0, data.profiles[0].games);
}

void test_same_profile_on_both_sides_is_counted_once() {
    ProfileData data = makeProfiles();
    const GameSummary summary = makeSummary(1, GameOutcome::Draw, 101, 101);

    TEST_ASSERT_TRUE(GameRecords::recordCompletedGame(data, summary));
    TEST_ASSERT_EQUAL_UINT32(1, data.profiles[0].games);
    TEST_ASSERT_EQUAL_UINT32(1, data.profiles[0].draws);
}

void test_invalid_and_incomplete_results_are_rejected() {
    ProfileData data = makeProfiles();
    GameSummary summary = makeSummary(1, GameOutcome::None);
    TEST_ASSERT_FALSE(GameRecords::recordCompletedGame(data, summary));

    summary = makeSummary(2, GameOutcome::Draw);
    summary.termination = TerminationReason::None;
    TEST_ASSERT_FALSE(GameRecords::recordCompletedGame(data, summary));

    summary.outcome = GameOutcome::Incomplete;
    TEST_ASSERT_FALSE(GameRecords::recordCompletedGame(data, summary));
    summary.outcome = static_cast<GameOutcome>(99);
    TEST_ASSERT_FALSE(GameRecords::recordCompletedGame(data, summary));
    summary.gameId = 0;
    summary.outcome = GameOutcome::Draw;
    TEST_ASSERT_FALSE(GameRecords::recordCompletedGame(data, summary));

    TEST_ASSERT_EQUAL_UINT8(0, data.historyCount);
    TEST_ASSERT_EQUAL_UINT32(0, data.profiles[0].games);
}

void test_duplicate_game_is_ignored_without_double_counting() {
    ProfileData data = makeProfiles();
    const GameSummary summary = makeSummary(77, GameOutcome::WhiteWin, 101, 0);

    TEST_ASSERT_TRUE(GameRecords::recordCompletedGame(data, summary));
    TEST_ASSERT_TRUE(GameRecords::hasRecordedGame(data, 77));
    TEST_ASSERT_FALSE(GameRecords::recordCompletedGame(data, summary));
    TEST_ASSERT_EQUAL_UINT8(1, data.historyCount);
    TEST_ASSERT_EQUAL_UINT32(1, data.profiles[0].games);
    TEST_ASSERT_EQUAL_UINT32(1, data.profiles[0].wins);
}

void test_history_is_newest_first_and_stops_at_capacity() {
    ProfileData data = makeProfiles();

    const uint32_t total = MAX_GAME_SUMMARIES + 5;
    for (uint32_t id = 1; id <= total; id++) {
        TEST_ASSERT_TRUE(GameRecords::recordCompletedGame(
            data, makeSummary(id, GameOutcome::Draw, 0, 0)));
    }

    TEST_ASSERT_EQUAL_UINT8(MAX_GAME_SUMMARIES, data.historyCount);
    TEST_ASSERT_EQUAL_UINT32(total, data.history[0].gameId);
    TEST_ASSERT_EQUAL_UINT32(total - MAX_GAME_SUMMARIES + 1,
                             data.history[MAX_GAME_SUMMARIES - 1].gameId);
    for (uint8_t i = 1; i < data.historyCount; i++) {
        TEST_ASSERT_EQUAL_UINT32(data.history[i - 1].gameId - 1,
                                 data.history[i].gameId);
    }
}

void test_history_normalizes_names_for_safe_display() {
    ProfileData data = makeProfiles();
    GameSummary summary = makeSummary(1, GameOutcome::Draw, 0, 0);
    std::memset(summary.whiteName, ' ', sizeof(summary.whiteName));
    std::memset(summary.blackName, 'B', sizeof(summary.blackName));

    TEST_ASSERT_TRUE(GameRecords::recordCompletedGame(data, summary));
    TEST_ASSERT_EQUAL_STRING("Player", data.history[0].whiteName);
    TEST_ASSERT_EQUAL_STRING("BBBBBBBBBBBB", data.history[0].blackName);
    TEST_ASSERT_EQUAL_CHAR('\0', data.history[0].blackName[PLAYER_NAME_MAX]);
}

void test_counters_saturate_instead_of_wrapping() {
    ProfileData data = makeProfiles();
    const uint32_t max = std::numeric_limits<uint32_t>::max();
    data.profiles[0].games = max;
    data.profiles[0].wins = max;
    data.profiles[0].losses = max;
    data.profiles[0].draws = max;

    TEST_ASSERT_TRUE(GameRecords::recordCompletedGame(
        data, makeSummary(1, GameOutcome::WhiteWin, 101, 0)));
    TEST_ASSERT_TRUE(GameRecords::recordCompletedGame(
        data, makeSummary(2, GameOutcome::BlackWin, 101, 0)));
    TEST_ASSERT_TRUE(GameRecords::recordCompletedGame(
        data, makeSummary(3, GameOutcome::Draw, 101, 0)));

    TEST_ASSERT_EQUAL_UINT32(max, data.profiles[0].games);
    TEST_ASSERT_EQUAL_UINT32(max, data.profiles[0].wins);
    TEST_ASSERT_EQUAL_UINT32(max, data.profiles[0].losses);
    TEST_ASSERT_EQUAL_UINT32(max, data.profiles[0].draws);
}

void test_invalid_counts_are_rejected_without_out_of_bounds_access() {
    ProfileData data = makeProfiles();
    data.profileCount = MAX_PLAYER_PROFILES + 1;
    TEST_ASSERT_NULL(GameRecords::activeProfile(data));
    TEST_ASSERT_FALSE(GameRecords::setActiveProfile(data, 0));
    TEST_ASSERT_FALSE(GameRecords::renameProfile(data, 0, "Still Alice"));
    TEST_ASSERT_FALSE(GameRecords::deleteProfile(data, 0));
    TEST_ASSERT_FALSE(GameRecords::recordCompletedGame(
        data, makeSummary(1, GameOutcome::Draw)));

    data = makeProfiles();
    data.historyCount = MAX_GAME_SUMMARIES + 1;
    TEST_ASSERT_FALSE(GameRecords::recordCompletedGame(
        data, makeSummary(1, GameOutcome::Draw)));
}

void test_short_labels_cover_all_result_reasons() {
    TEST_ASSERT_EQUAL_STRING("1-0", GameRecords::outcomeShortName(GameOutcome::WhiteWin));
    TEST_ASSERT_EQUAL_STRING("0-1", GameRecords::outcomeShortName(GameOutcome::BlackWin));
    TEST_ASSERT_EQUAL_STRING("1/2", GameRecords::outcomeShortName(GameOutcome::Draw));
    TEST_ASSERT_EQUAL_STRING("Inc", GameRecords::outcomeShortName(GameOutcome::Incomplete));
    TEST_ASSERT_EQUAL_STRING("--", GameRecords::outcomeShortName(GameOutcome::None));

    TEST_ASSERT_EQUAL_STRING("Mate", GameRecords::terminationShortName(TerminationReason::Checkmate));
    TEST_ASSERT_EQUAL_STRING("Time", GameRecords::terminationShortName(TerminationReason::Timeout));
    TEST_ASSERT_EQUAL_STRING("Resign", GameRecords::terminationShortName(TerminationReason::Resignation));
    TEST_ASSERT_EQUAL_STRING("Agreement", GameRecords::terminationShortName(TerminationReason::Agreement));
    TEST_ASSERT_EQUAL_STRING("Stalemate", GameRecords::terminationShortName(TerminationReason::Stalemate));
    TEST_ASSERT_EQUAL_STRING("Repetition", GameRecords::terminationShortName(TerminationReason::Repetition));
    TEST_ASSERT_EQUAL_STRING("50-move", GameRecords::terminationShortName(TerminationReason::FiftyMove));
    TEST_ASSERT_EQUAL_STRING("Material", GameRecords::terminationShortName(TerminationReason::InsufficientMaterial));
    TEST_ASSERT_EQUAL_STRING("Disconnect", GameRecords::terminationShortName(TerminationReason::Disconnection));
    TEST_ASSERT_EQUAL_STRING("Unknown", GameRecords::terminationShortName(TerminationReason::None));
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sanitize_rejects_missing_or_blank_names);
    RUN_TEST(test_sanitize_trims_filters_and_truncates);
    RUN_TEST(test_sanitize_supports_in_place_input_output);
    RUN_TEST(test_initialize_creates_safe_default_profile);
    RUN_TEST(test_profile_add_find_rename_and_activate);
    RUN_TEST(test_profile_operations_reject_invalid_data);
    RUN_TEST(test_profiles_stop_at_capacity);
    RUN_TEST(test_delete_active_profile_compacts_and_selects_first);
    RUN_TEST(test_delete_before_active_preserves_same_active_profile);
    RUN_TEST(test_cannot_delete_only_or_out_of_range_profile);
    RUN_TEST(test_white_win_updates_both_profiles_once);
    RUN_TEST(test_black_win_and_draw_update_correct_statistics);
    RUN_TEST(test_unrecognized_profile_ids_do_not_block_history);
    RUN_TEST(test_same_profile_on_both_sides_is_counted_once);
    RUN_TEST(test_invalid_and_incomplete_results_are_rejected);
    RUN_TEST(test_duplicate_game_is_ignored_without_double_counting);
    RUN_TEST(test_history_is_newest_first_and_stops_at_capacity);
    RUN_TEST(test_history_normalizes_names_for_safe_display);
    RUN_TEST(test_counters_saturate_instead_of_wrapping);
    RUN_TEST(test_invalid_counts_are_rejected_without_out_of_bounds_access);
    RUN_TEST(test_short_labels_cover_all_result_reasons);
    return UNITY_END();
}
