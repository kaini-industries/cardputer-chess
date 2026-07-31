#ifndef PROFILE_STORAGE_CODEC_H
#define PROFILE_STORAGE_CODEC_H

#include "game_records.h"

#include <cstddef>
#include <cstdint>

namespace ProfileStorageCodec {

enum class DecodeStatus : uint8_t {
    Ok,
    Corrupt,
    UnsupportedVersion,
};

static constexpr size_t PROFILE_ENCODED_BYTES =
    4 + 4 + (MAX_PLAYER_PROFILES * 33) +
    (MAX_GAME_SUMMARIES * 57) + 4;
static constexpr size_t ACTIVE_ENCODED_BYTES =
    4 + 2 + 4 + 1 + 4 + 4 + (PLAYER_NAME_MAX + 1) * 2 + 4;
static constexpr size_t PENDING_RESULT_ENCODED_BYTES = 4 + 1 + 57 + 4;

uint32_t crc32(const uint8_t* data, size_t length);

bool validCompletedSummary(const GameSummary& summary);
bool summariesEqual(const GameSummary& left, const GameSummary& right);

bool encodeProfiles(const ProfileData& data, uint8_t* buffer, size_t size);
DecodeStatus decodeProfiles(const uint8_t* buffer, size_t size,
                            ProfileData& data);

bool encodeActive(const ActiveGameParticipants& active,
                  uint8_t* buffer, size_t size);
DecodeStatus decodeActive(const uint8_t* buffer, size_t size,
                          ActiveGameParticipants& active);

bool encodePendingResult(const GameSummary& summary,
                         uint8_t* buffer, size_t size);
DecodeStatus decodePendingResult(const uint8_t* buffer, size_t size,
                                 GameSummary& summary);

} // namespace ProfileStorageCodec

#endif // PROFILE_STORAGE_CODEC_H
