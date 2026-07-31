#ifndef PROFILE_STORAGE_H
#define PROFILE_STORAGE_H

#include "game_records.h"

#include <cstdint>

namespace ProfileStorage {

// Loaded/Created are the only statuses that authorize profile writes.
// Missing is returned only by load(); loadOrCreate() converts a confirmed
// Missing record to Created after a durable write. All failure outcomes leave
// the output argument untouched and never replace the stored bytes.
enum class LoadStatus : uint8_t {
    Loaded,
    Created,
    Missing,
    Corrupt,
    UnsupportedVersion,
    IoError,
};

enum class PendingSaveStatus : uint8_t {
    Saved,
    AlreadyPresent,
    Conflict,
    Invalid,
    CorruptExisting,
    UnsupportedExisting,
    IoError,
};

enum class SummaryMatch : uint8_t {
    Missing,
    Exact,
    Conflict,
};

LoadStatus load(ProfileData& data);
LoadStatus loadOrCreate(ProfileData& data);

// Refuses to overwrite corrupt, unsupported, or unreadable profile data.
// A confirmed missing record may be created; a valid record may be updated.
bool save(const ProfileData& data);

uint32_t createUniqueProfileId(const ProfileData& data);

// Legacy v0.20 active metadata. New resumable saves embed participants in the
// v6 ChessStorage record; these remain available only for safe migration.
bool saveActiveGame(const ActiveGameParticipants& participants);
LoadStatus loadActiveGameStatus(ActiveGameParticipants& participants);
bool loadActiveGame(ActiveGameParticipants& participants);
// Missing is already clear. Erases only a supported record that exactly
// matches expected; corrupt, future, unreadable, and unrelated bytes remain.
bool clearActiveGame(const ActiveGameParticipants& expected);

// A single durable, CRC-protected result journal. savePendingResult never
// overwrites a different, corrupt, or future-version journal.
LoadStatus loadPendingResult(GameSummary& summary);
PendingSaveStatus savePendingResult(const GameSummary& summary);
bool clearPendingResult(const GameSummary& expected);

bool summariesEqual(const GameSummary& left, const GameSummary& right);
SummaryMatch findSummary(const ProfileData& profiles,
                         const GameSummary& expected);

} // namespace ProfileStorage

#endif // PROFILE_STORAGE_H
