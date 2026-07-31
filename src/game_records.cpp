#include "game_records.h"

#include <cstring>
#include <limits>

namespace {

uint8_t boundedProfileCount(const ProfileData& data) {
    return data.profileCount <= MAX_PLAYER_PROFILES
         ? data.profileCount : MAX_PLAYER_PROFILES;
}

uint8_t boundedHistoryCount(const ProfileData& data) {
    return data.historyCount <= MAX_GAME_SUMMARIES
         ? data.historyCount : MAX_GAME_SUMMARIES;
}

void copyName(char dst[PLAYER_NAME_MAX + 1], const char* src) {
    if (!GameRecords::sanitizeName(src, dst)) {
        std::strncpy(dst, "Player", PLAYER_NAME_MAX);
        dst[PLAYER_NAME_MAX] = '\0';
    }
}

void incrementSaturated(uint32_t& value) {
    if (value != std::numeric_limits<uint32_t>::max()) value++;
}

void updateProfileResult(PlayerProfile& profile, PieceColor color,
                         GameOutcome outcome) {
    incrementSaturated(profile.games);
    if (outcome == GameOutcome::Draw) {
        incrementSaturated(profile.draws);
        return;
    }

    const bool won = (color == PieceColor::White &&
                      outcome == GameOutcome::WhiteWin) ||
                     (color == PieceColor::Black &&
                      outcome == GameOutcome::BlackWin);
    if (won) incrementSaturated(profile.wins);
    else incrementSaturated(profile.losses);
}

bool isCompletedOutcome(GameOutcome outcome) {
    return outcome == GameOutcome::WhiteWin ||
           outcome == GameOutcome::BlackWin ||
           outcome == GameOutcome::Draw;
}

void normalizeSummaryName(char name[PLAYER_NAME_MAX + 1]) {
    // Persisted/network-provided fixed fields are not guaranteed to contain a
    // terminator. Bound the input before passing it to the C-string sanitizer.
    char bounded[PLAYER_NAME_MAX + 1];
    std::memcpy(bounded, name, PLAYER_NAME_MAX);
    bounded[PLAYER_NAME_MAX] = '\0';
    char clean[PLAYER_NAME_MAX + 1];
    if (!GameRecords::sanitizeName(bounded, clean)) {
        std::strncpy(clean, "Player", PLAYER_NAME_MAX + 1);
    }
    std::memcpy(name, clean, sizeof(clean));
}

} // namespace

namespace GameRecords {

bool sanitizeName(const char* input, char output[PLAYER_NAME_MAX + 1]) {
    if (!output) return false;
    if (!input) {
        output[0] = '\0';
        return false;
    }

    // Build into a temporary buffer so input and output may safely alias.
    char clean[PLAYER_NAME_MAX + 1] = {};
    bool started = false;
    uint8_t length = 0;
    while (*input && length < PLAYER_NAME_MAX) {
        const unsigned char c = static_cast<unsigned char>(*input++);
        if (c < 32 || c > 126) continue;
        if (!started && c == ' ') continue;
        started = true;
        clean[length++] = static_cast<char>(c);
    }

    while (length > 0 && clean[length - 1] == ' ') length--;
    clean[length] = '\0';
    std::memcpy(output, clean, sizeof(clean));
    return length > 0;
}

void initialize(ProfileData& data, uint32_t firstProfileId,
                const char* firstName) {
    data = ProfileData{};
    data.profileCount = 1;
    data.activeIndex = 0;
    data.profiles[0].id = firstProfileId == 0 ? 1 : firstProfileId;
    copyName(data.profiles[0].name, firstName);
}

const PlayerProfile* activeProfile(const ProfileData& data) {
    if (data.profileCount == 0 || data.profileCount > MAX_PLAYER_PROFILES ||
        data.activeIndex >= data.profileCount) {
        return nullptr;
    }
    return &data.profiles[data.activeIndex];
}

PlayerProfile* activeProfile(ProfileData& data) {
    if (data.profileCount == 0 || data.profileCount > MAX_PLAYER_PROFILES ||
        data.activeIndex >= data.profileCount) {
        return nullptr;
    }
    return &data.profiles[data.activeIndex];
}

const PlayerProfile* findProfile(const ProfileData& data, uint32_t profileId) {
    if (profileId == 0) return nullptr;
    for (uint8_t i = 0; i < boundedProfileCount(data); i++) {
        if (data.profiles[i].id == profileId) return &data.profiles[i];
    }
    return nullptr;
}

PlayerProfile* findProfile(ProfileData& data, uint32_t profileId) {
    if (profileId == 0) return nullptr;
    for (uint8_t i = 0; i < boundedProfileCount(data); i++) {
        if (data.profiles[i].id == profileId) return &data.profiles[i];
    }
    return nullptr;
}

bool setActiveProfile(ProfileData& data, uint8_t index) {
    if (data.profileCount > MAX_PLAYER_PROFILES || index >= data.profileCount) {
        return false;
    }
    data.activeIndex = index;
    return true;
}

bool addProfile(ProfileData& data, uint32_t profileId, const char* name) {
    if (data.profileCount >= MAX_PLAYER_PROFILES || profileId == 0 ||
        findProfile(data, profileId)) {
        return false;
    }

    char clean[PLAYER_NAME_MAX + 1];
    if (!sanitizeName(name, clean)) return false;

    PlayerProfile& profile = data.profiles[data.profileCount];
    profile = PlayerProfile{};
    profile.id = profileId;
    std::strncpy(profile.name, clean, PLAYER_NAME_MAX + 1);
    data.activeIndex = data.profileCount;
    data.profileCount++;
    return true;
}

bool renameProfile(ProfileData& data, uint8_t index, const char* name) {
    if (data.profileCount > MAX_PLAYER_PROFILES || index >= data.profileCount) {
        return false;
    }
    char clean[PLAYER_NAME_MAX + 1];
    if (!sanitizeName(name, clean)) return false;
    std::strncpy(data.profiles[index].name, clean, PLAYER_NAME_MAX + 1);
    return true;
}

bool deleteProfile(ProfileData& data, uint8_t index) {
    if (data.profileCount <= 1 || data.profileCount > MAX_PLAYER_PROFILES ||
        data.activeIndex >= data.profileCount || index >= data.profileCount) {
        return false;
    }
    for (uint8_t i = index; i + 1 < data.profileCount; i++) {
        data.profiles[i] = data.profiles[i + 1];
    }
    data.profiles[data.profileCount - 1] = PlayerProfile{};
    data.profileCount--;
    if (data.activeIndex == index) {
        data.activeIndex = 0;
    } else if (data.activeIndex > index) {
        data.activeIndex--;
    }
    return true;
}

bool hasRecordedGame(const ProfileData& data, uint32_t gameId) {
    if (gameId == 0) return false;
    for (uint8_t i = 0; i < boundedHistoryCount(data); i++) {
        if (data.history[i].gameId == gameId) return true;
    }
    return false;
}

bool recordCompletedGame(ProfileData& data, const GameSummary& summary) {
    if (data.profileCount > MAX_PLAYER_PROFILES ||
        data.historyCount > MAX_GAME_SUMMARIES || summary.gameId == 0 ||
        !isCompletedOutcome(summary.outcome) ||
        summary.termination == TerminationReason::None ||
        static_cast<uint8_t>(summary.termination) >
            static_cast<uint8_t>(TerminationReason::Disconnection) ||
        hasRecordedGame(data, summary.gameId)) {
        return false;
    }

    const uint8_t copyCount = data.historyCount < MAX_GAME_SUMMARIES
                            ? data.historyCount
                            : static_cast<uint8_t>(MAX_GAME_SUMMARIES - 1);
    for (uint8_t i = copyCount; i > 0; i--) {
        data.history[i] = data.history[i - 1];
    }
    data.history[0] = summary;
    normalizeSummaryName(data.history[0].whiteName);
    normalizeSummaryName(data.history[0].blackName);
    if (data.historyCount < MAX_GAME_SUMMARIES) data.historyCount++;

    PlayerProfile* white = findProfile(data, summary.whiteProfileId);
    PlayerProfile* black = findProfile(data, summary.blackProfileId);
    if (white) updateProfileResult(*white, PieceColor::White, summary.outcome);
    if (black && black != white) {
        updateProfileResult(*black, PieceColor::Black, summary.outcome);
    }
    return true;
}

const char* outcomeShortName(GameOutcome outcome) {
    switch (outcome) {
        case GameOutcome::WhiteWin: return "1-0";
        case GameOutcome::BlackWin: return "0-1";
        case GameOutcome::Draw: return "1/2";
        case GameOutcome::Incomplete: return "Inc";
        default: return "--";
    }
}

const char* terminationShortName(TerminationReason reason) {
    switch (reason) {
        case TerminationReason::Checkmate: return "Mate";
        case TerminationReason::Timeout: return "Time";
        case TerminationReason::Resignation: return "Resign";
        case TerminationReason::Agreement: return "Agreement";
        case TerminationReason::Stalemate: return "Stalemate";
        case TerminationReason::Repetition: return "Repetition";
        case TerminationReason::FiftyMove: return "50-move";
        case TerminationReason::InsufficientMaterial: return "Material";
        case TerminationReason::Disconnection: return "Disconnect";
        default: return "Unknown";
    }
}

} // namespace GameRecords
