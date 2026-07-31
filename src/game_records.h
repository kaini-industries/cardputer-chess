#ifndef GAME_RECORDS_H
#define GAME_RECORDS_H

#include "chess_types.h"
#include <cstdint>

static constexpr uint8_t MAX_PLAYER_PROFILES = 4;
static constexpr uint8_t MAX_GAME_SUMMARIES = 16;
static constexpr uint8_t PLAYER_NAME_MAX = 12;

enum class GameMode : uint8_t {
    Local = 0,
    AI = 1,
    Online = 2,
};

enum class GameOutcome : uint8_t {
    None = 0,
    WhiteWin = 1,
    BlackWin = 2,
    Draw = 3,
    Incomplete = 4,
};

enum class TerminationReason : uint8_t {
    None = 0,
    Checkmate = 1,
    Timeout = 2,
    Resignation = 3,
    Agreement = 4,
    Stalemate = 5,
    Repetition = 6,
    FiftyMove = 7,
    InsufficientMaterial = 8,
    Disconnection = 9,
};

struct PlayerProfile {
    uint32_t id = 0;
    char name[PLAYER_NAME_MAX + 1] = {};
    uint32_t games = 0;
    uint32_t wins = 0;
    uint32_t losses = 0;
    uint32_t draws = 0;
};

struct GameSummary {
    uint32_t gameId = 0;
    uint32_t whiteProfileId = 0;
    uint32_t blackProfileId = 0;
    char whiteName[PLAYER_NAME_MAX + 1] = {};
    char blackName[PLAYER_NAME_MAX + 1] = {};
    GameMode mode = GameMode::Local;
    ChessVariant variant = ChessVariant::Standard;
    uint16_t positionIndex = 518;
    TimeControl timeControl = TimeControl::None;
    uint8_t aiDifficulty = 0;
    PieceColor localColor = PieceColor::White;
    GameOutcome outcome = GameOutcome::None;
    TerminationReason termination = TerminationReason::None;
    uint16_t plyCount = 0;
    uint32_t whiteRemainingMs = 0;
    uint32_t blackRemainingMs = 0;
};

struct ProfileData {
    uint8_t profileCount = 0;
    uint8_t activeIndex = 0;
    PlayerProfile profiles[MAX_PLAYER_PROFILES] = {};
    uint8_t historyCount = 0;
    GameSummary history[MAX_GAME_SUMMARIES] = {}; // Newest first.
};

struct ActiveGameParticipants {
    bool valid = false;
    uint32_t gameId = 0;
    GameMode mode = GameMode::Local;
    uint32_t whiteProfileId = 0;
    uint32_t blackProfileId = 0;
    char whiteName[PLAYER_NAME_MAX + 1] = {};
    char blackName[PLAYER_NAME_MAX + 1] = {};
};

namespace GameRecords {

// Copies a display-safe printable ASCII name, trims surrounding spaces, and
// returns false when no usable characters remain.
bool sanitizeName(const char* input, char output[PLAYER_NAME_MAX + 1]);

void initialize(ProfileData& data, uint32_t firstProfileId,
                const char* firstName = "Player");

const PlayerProfile* activeProfile(const ProfileData& data);
PlayerProfile* activeProfile(ProfileData& data);
const PlayerProfile* findProfile(const ProfileData& data, uint32_t profileId);
PlayerProfile* findProfile(ProfileData& data, uint32_t profileId);

bool setActiveProfile(ProfileData& data, uint8_t index);
bool addProfile(ProfileData& data, uint32_t profileId, const char* name);
bool renameProfile(ProfileData& data, uint8_t index, const char* name);
bool deleteProfile(ProfileData& data, uint8_t index);

// Adds newest-first history and updates statistics for any local profile IDs
// referenced by the completed game. Duplicate game IDs are ignored.
bool recordCompletedGame(ProfileData& data, const GameSummary& summary);
bool hasRecordedGame(const ProfileData& data, uint32_t gameId);

const char* outcomeShortName(GameOutcome outcome);
const char* terminationShortName(TerminationReason reason);

} // namespace GameRecords

#endif // GAME_RECORDS_H
