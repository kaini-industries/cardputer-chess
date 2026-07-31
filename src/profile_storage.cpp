#include "profile_storage.h"

#include "nvs_blob_store.h"
#include "profile_storage_codec.h"

#include <esp_random.h>
#include <cstring>

namespace {

static constexpr const char* NVS_NAMESPACE = "chess";
static constexpr const char* PROFILE_KEY = "profiles";
static constexpr const char* ACTIVE_KEY = "active_meta";
static constexpr const char* PENDING_KEY = "result_journal";

// Current records are much smaller. This cap prevents corrupt NVS lengths from
// forcing arbitrary allocations while preserving an oversized record as a
// possible future version.
static constexpr size_t MAX_PROFILE_READ_BYTES = 4096;
static constexpr size_t MAX_ACTIVE_READ_BYTES = 512;
static constexpr size_t MAX_PENDING_READ_BYTES = 512;

ProfileStorage::LoadStatus mapReadFailure(NvsBlobStore::ReadStatus status) {
    switch (status) {
        case NvsBlobStore::ReadStatus::Missing:
            return ProfileStorage::LoadStatus::Missing;
        case NvsBlobStore::ReadStatus::WrongType:
            return ProfileStorage::LoadStatus::Corrupt;
        case NvsBlobStore::ReadStatus::TooLarge:
            return ProfileStorage::LoadStatus::UnsupportedVersion;
        case NvsBlobStore::ReadStatus::IoError:
            return ProfileStorage::LoadStatus::IoError;
        case NvsBlobStore::ReadStatus::Ok:
            break;
    }
    return ProfileStorage::LoadStatus::IoError;
}

ProfileStorage::LoadStatus mapDecodeStatus(
        ProfileStorageCodec::DecodeStatus status) {
    switch (status) {
        case ProfileStorageCodec::DecodeStatus::Ok:
            return ProfileStorage::LoadStatus::Loaded;
        case ProfileStorageCodec::DecodeStatus::UnsupportedVersion:
            return ProfileStorage::LoadStatus::UnsupportedVersion;
        case ProfileStorageCodec::DecodeStatus::Corrupt:
            return ProfileStorage::LoadStatus::Corrupt;
    }
    return ProfileStorage::LoadStatus::Corrupt;
}

template <typename T, typename Decoder>
ProfileStorage::LoadStatus loadRecord(const char* key, size_t maximumSize,
                                      T& output, Decoder decoder) {
    NvsBlobStore::Blob blob;
    const NvsBlobStore::ReadStatus readStatus =
        NvsBlobStore::read(NVS_NAMESPACE, key, maximumSize, blob);
    if (readStatus != NvsBlobStore::ReadStatus::Ok) {
        return mapReadFailure(readStatus);
    }

    T decoded;
    const ProfileStorageCodec::DecodeStatus decodeStatus =
        decoder(blob.data, blob.size, decoded);
    if (decodeStatus != ProfileStorageCodec::DecodeStatus::Ok) {
        return mapDecodeStatus(decodeStatus);
    }
    output = decoded;
    return ProfileStorage::LoadStatus::Loaded;
}

} // namespace

namespace ProfileStorage {

LoadStatus load(ProfileData& data) {
    return loadRecord(PROFILE_KEY, MAX_PROFILE_READ_BYTES, data,
        [](const uint8_t* buffer, size_t size, ProfileData& decoded) {
            return ProfileStorageCodec::decodeProfiles(
                buffer, size, decoded);
        });
}

LoadStatus loadOrCreate(ProfileData& data) {
    ProfileData loaded;
    const LoadStatus status = load(loaded);
    if (status == LoadStatus::Loaded) {
        data = loaded;
        return LoadStatus::Loaded;
    }
    if (status != LoadStatus::Missing) return status;

    uint32_t id = esp_random();
    if (id == 0) id = 1;
    ProfileData created;
    GameRecords::initialize(created, id);
    if (!save(created)) return LoadStatus::IoError;
    data = created;
    return LoadStatus::Created;
}

bool save(const ProfileData& data) {
    // Re-read before every update. This is intentionally conservative: an
    // unreadable/future record is never replaced by stale in-memory defaults.
    ProfileData existing;
    const LoadStatus existingStatus = load(existing);
    if (existingStatus != LoadStatus::Loaded &&
        existingStatus != LoadStatus::Missing) {
        return false;
    }

    uint8_t buffer[ProfileStorageCodec::PROFILE_ENCODED_BYTES];
    if (!ProfileStorageCodec::encodeProfiles(data, buffer, sizeof(buffer))) {
        return false;
    }
    return NvsBlobStore::write(NVS_NAMESPACE, PROFILE_KEY,
                               buffer, sizeof(buffer));
}

uint32_t createUniqueProfileId(const ProfileData& data) {
    for (uint8_t attempt = 0; attempt < 32; ++attempt) {
        const uint32_t id = esp_random();
        if (id != 0 && !GameRecords::findProfile(data, id)) return id;
    }
    uint32_t id = 1;
    while (GameRecords::findProfile(data, id)) ++id;
    return id;
}

bool saveActiveGame(const ActiveGameParticipants& participants) {
    uint8_t buffer[ProfileStorageCodec::ACTIVE_ENCODED_BYTES];
    if (!ProfileStorageCodec::encodeActive(
            participants, buffer, sizeof(buffer))) {
        return false;
    }
    return NvsBlobStore::write(NVS_NAMESPACE, ACTIVE_KEY,
                               buffer, sizeof(buffer));
}

LoadStatus loadActiveGameStatus(ActiveGameParticipants& participants) {
    return loadRecord(ACTIVE_KEY, MAX_ACTIVE_READ_BYTES, participants,
        [](const uint8_t* buffer, size_t size,
           ActiveGameParticipants& decoded) {
            return ProfileStorageCodec::decodeActive(buffer, size, decoded);
        });
}

bool loadActiveGame(ActiveGameParticipants& participants) {
    ActiveGameParticipants loaded;
    if (loadActiveGameStatus(loaded) != LoadStatus::Loaded || !loaded.valid) {
        return false;
    }
    participants = loaded;
    return true;
}

bool clearActiveGame(const ActiveGameParticipants& expected) {
    if (!expected.valid || expected.gameId == 0) return false;

    ActiveGameParticipants stored;
    const LoadStatus status = loadActiveGameStatus(stored);
    if (status == LoadStatus::Missing) return true;
    if (status != LoadStatus::Loaded ||
        stored.valid != expected.valid ||
        stored.gameId != expected.gameId ||
        stored.mode != expected.mode ||
        stored.whiteProfileId != expected.whiteProfileId ||
        stored.blackProfileId != expected.blackProfileId ||
        std::memcmp(stored.whiteName, expected.whiteName,
                    PLAYER_NAME_MAX + 1) != 0 ||
        std::memcmp(stored.blackName, expected.blackName,
                    PLAYER_NAME_MAX + 1) != 0) {
        return false;
    }
    return NvsBlobStore::erase(NVS_NAMESPACE, ACTIVE_KEY);
}

LoadStatus loadPendingResult(GameSummary& summary) {
    return loadRecord(PENDING_KEY, MAX_PENDING_READ_BYTES, summary,
        [](const uint8_t* buffer, size_t size, GameSummary& decoded) {
            return ProfileStorageCodec::decodePendingResult(
                buffer, size, decoded);
        });
}

PendingSaveStatus savePendingResult(const GameSummary& summary) {
    uint8_t buffer[ProfileStorageCodec::PENDING_RESULT_ENCODED_BYTES];
    if (!ProfileStorageCodec::encodePendingResult(
            summary, buffer, sizeof(buffer))) {
        return PendingSaveStatus::Invalid;
    }

    GameSummary existing;
    const LoadStatus existingStatus = loadPendingResult(existing);
    switch (existingStatus) {
        case LoadStatus::Loaded:
            return summariesEqual(existing, summary)
                ? PendingSaveStatus::AlreadyPresent
                : PendingSaveStatus::Conflict;
        case LoadStatus::Missing:
            break;
        case LoadStatus::Corrupt:
            return PendingSaveStatus::CorruptExisting;
        case LoadStatus::UnsupportedVersion:
            return PendingSaveStatus::UnsupportedExisting;
        case LoadStatus::Created:
        case LoadStatus::IoError:
            return PendingSaveStatus::IoError;
    }

    return NvsBlobStore::write(NVS_NAMESPACE, PENDING_KEY,
                               buffer, sizeof(buffer))
        ? PendingSaveStatus::Saved
        : PendingSaveStatus::IoError;
}

bool clearPendingResult(const GameSummary& expected) {
    GameSummary stored;
    const LoadStatus status = loadPendingResult(stored);
    if (status == LoadStatus::Missing) return true;
    if (status != LoadStatus::Loaded || !summariesEqual(stored, expected)) {
        return false;
    }
    return NvsBlobStore::erase(NVS_NAMESPACE, PENDING_KEY);
}

bool summariesEqual(const GameSummary& left, const GameSummary& right) {
    return ProfileStorageCodec::summariesEqual(left, right);
}

SummaryMatch findSummary(const ProfileData& profiles,
                         const GameSummary& expected) {
    const uint8_t count = profiles.historyCount <= MAX_GAME_SUMMARIES
        ? profiles.historyCount : MAX_GAME_SUMMARIES;
    for (uint8_t i = 0; i < count; ++i) {
        if (profiles.history[i].gameId != expected.gameId) continue;
        return summariesEqual(profiles.history[i], expected)
            ? SummaryMatch::Exact : SummaryMatch::Conflict;
    }
    return SummaryMatch::Missing;
}

} // namespace ProfileStorage
