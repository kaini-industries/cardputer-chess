#include "profile_storage_codec.h"

#include <cstring>

namespace ProfileStorageCodec {
namespace {

static constexpr uint8_t PROFILE_VERSION = 1;
static constexpr uint8_t ACTIVE_VERSION = 1;
static constexpr uint8_t PENDING_VERSION = 1;

class Writer {
public:
    Writer(uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    bool u8(uint8_t value) {
        if (m_pos + 1 > m_size) return false;
        m_data[m_pos++] = value;
        return true;
    }
    bool u16(uint16_t value) {
        return u8(static_cast<uint8_t>(value)) &&
               u8(static_cast<uint8_t>(value >> 8));
    }
    bool u32(uint32_t value) {
        return u16(static_cast<uint16_t>(value)) &&
               u16(static_cast<uint16_t>(value >> 16));
    }
    bool bytes(const void* source, size_t count) {
        if (!source || m_pos + count > m_size) return false;
        std::memcpy(m_data + m_pos, source, count);
        m_pos += count;
        return true;
    }
    size_t position() const { return m_pos; }

private:
    uint8_t* m_data;
    size_t m_size;
    size_t m_pos = 0;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    bool u8(uint8_t& value) {
        if (m_pos + 1 > m_size) return false;
        value = m_data[m_pos++];
        return true;
    }
    bool u16(uint16_t& value) {
        uint8_t low, high;
        if (!u8(low) || !u8(high)) return false;
        value = static_cast<uint16_t>(low) |
                (static_cast<uint16_t>(high) << 8);
        return true;
    }
    bool u32(uint32_t& value) {
        uint16_t low, high;
        if (!u16(low) || !u16(high)) return false;
        value = static_cast<uint32_t>(low) |
                (static_cast<uint32_t>(high) << 16);
        return true;
    }
    bool bytes(void* destination, size_t count) {
        if (!destination || m_pos + count > m_size) return false;
        std::memcpy(destination, m_data + m_pos, count);
        m_pos += count;
        return true;
    }
    size_t position() const { return m_pos; }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos = 0;
};

bool validName(const char name[PLAYER_NAME_MAX + 1]) {
    char clean[PLAYER_NAME_MAX + 1];
    return GameRecords::sanitizeName(name, clean) &&
           std::memcmp(name, clean, PLAYER_NAME_MAX + 1) == 0;
}

bool validProfilesForSave(const ProfileData& data) {
    if (data.profileCount == 0 || data.profileCount > MAX_PLAYER_PROFILES ||
        data.activeIndex >= data.profileCount ||
        data.historyCount > MAX_GAME_SUMMARIES) {
        return false;
    }
    for (uint8_t i = 0; i < data.profileCount; ++i) {
        const PlayerProfile& profile = data.profiles[i];
        if (profile.id == 0 || !validName(profile.name) ||
            profile.wins > profile.games ||
            profile.losses > profile.games ||
            profile.draws > profile.games) {
            return false;
        }
        for (uint8_t j = 0; j < i; ++j) {
            if (data.profiles[j].id == profile.id) return false;
        }
    }
    for (uint8_t i = 0; i < data.historyCount; ++i) {
        if (!validCompletedSummary(data.history[i])) return false;
        for (uint8_t j = 0; j < i; ++j) {
            if (data.history[j].gameId == data.history[i].gameId) return false;
        }
    }
    return true;
}

bool validActive(const ActiveGameParticipants& active) {
    if (!active.valid) return true;
    return active.gameId != 0 &&
           static_cast<uint8_t>(active.mode) <=
               static_cast<uint8_t>(GameMode::Online) &&
           validName(active.whiteName) && validName(active.blackName);
}

bool writeProfile(Writer& writer, const PlayerProfile& profile) {
    return writer.u32(profile.id) &&
           writer.bytes(profile.name, PLAYER_NAME_MAX + 1) &&
           writer.u32(profile.games) && writer.u32(profile.wins) &&
           writer.u32(profile.losses) && writer.u32(profile.draws);
}

bool readProfile(Reader& reader, PlayerProfile& profile) {
    return reader.u32(profile.id) &&
           reader.bytes(profile.name, PLAYER_NAME_MAX + 1) &&
           reader.u32(profile.games) && reader.u32(profile.wins) &&
           reader.u32(profile.losses) && reader.u32(profile.draws);
}

bool writeSummary(Writer& writer, const GameSummary& summary) {
    return writer.u32(summary.gameId) &&
           writer.u32(summary.whiteProfileId) &&
           writer.u32(summary.blackProfileId) &&
           writer.bytes(summary.whiteName, PLAYER_NAME_MAX + 1) &&
           writer.bytes(summary.blackName, PLAYER_NAME_MAX + 1) &&
           writer.u8(static_cast<uint8_t>(summary.mode)) &&
           writer.u8(static_cast<uint8_t>(summary.variant)) &&
           writer.u16(summary.positionIndex) &&
           writer.u8(static_cast<uint8_t>(summary.timeControl)) &&
           writer.u8(summary.aiDifficulty) &&
           writer.u8(static_cast<uint8_t>(summary.localColor)) &&
           writer.u8(static_cast<uint8_t>(summary.outcome)) &&
           writer.u8(static_cast<uint8_t>(summary.termination)) &&
           writer.u16(summary.plyCount) &&
           writer.u32(summary.whiteRemainingMs) &&
           writer.u32(summary.blackRemainingMs);
}

bool readSummary(Reader& reader, GameSummary& summary) {
    uint8_t mode, variant, timeControl, localColor, outcome, termination;
    if (!reader.u32(summary.gameId) ||
        !reader.u32(summary.whiteProfileId) ||
        !reader.u32(summary.blackProfileId) ||
        !reader.bytes(summary.whiteName, PLAYER_NAME_MAX + 1) ||
        !reader.bytes(summary.blackName, PLAYER_NAME_MAX + 1) ||
        !reader.u8(mode) || !reader.u8(variant) ||
        !reader.u16(summary.positionIndex) || !reader.u8(timeControl) ||
        !reader.u8(summary.aiDifficulty) || !reader.u8(localColor) ||
        !reader.u8(outcome) || !reader.u8(termination) ||
        !reader.u16(summary.plyCount) ||
        !reader.u32(summary.whiteRemainingMs) ||
        !reader.u32(summary.blackRemainingMs)) {
        return false;
    }
    if (mode > static_cast<uint8_t>(GameMode::Online) || variant > 1 ||
        timeControl > static_cast<uint8_t>(TimeControl::Rapid10) ||
        localColor > 1 ||
        outcome > static_cast<uint8_t>(GameOutcome::Incomplete) ||
        termination > static_cast<uint8_t>(TerminationReason::Disconnection)) {
        return false;
    }
    summary.mode = static_cast<GameMode>(mode);
    summary.variant = static_cast<ChessVariant>(variant);
    summary.timeControl = static_cast<TimeControl>(timeControl);
    summary.localColor = static_cast<PieceColor>(localColor);
    summary.outcome = static_cast<GameOutcome>(outcome);
    summary.termination = static_cast<TerminationReason>(termination);
    if (summary.whiteName[PLAYER_NAME_MAX] != '\0' ||
        summary.blackName[PLAYER_NAME_MAX] != '\0') {
        return false;
    }
    summary.whiteName[PLAYER_NAME_MAX] = '\0';
    summary.blackName[PLAYER_NAME_MAX] = '\0';
    return true;
}

bool writeChecksum(Writer& writer, const uint8_t* buffer) {
    return writer.u32(crc32(buffer, writer.position()));
}

bool hasMagicAndDifferentVersion(const uint8_t* buffer, size_t size,
                                 const char magic[4], uint8_t currentVersion) {
    return buffer && size >= 5 && std::memcmp(buffer, magic, 4) == 0 &&
           buffer[4] != currentVersion;
}

} // namespace

uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u &
                  static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u)));
        }
    }
    return ~crc;
}

bool validCompletedSummary(const GameSummary& summary) {
    const uint8_t mode = static_cast<uint8_t>(summary.mode);
    const uint8_t variant = static_cast<uint8_t>(summary.variant);
    const uint8_t timeControl = static_cast<uint8_t>(summary.timeControl);
    const uint8_t localColor = static_cast<uint8_t>(summary.localColor);
    const bool validDifficulty = summary.mode == GameMode::AI
        ? summary.aiDifficulty >= 1 && summary.aiDifficulty <= 3
        : summary.aiDifficulty == 0;
    const bool completed = summary.outcome == GameOutcome::WhiteWin ||
                           summary.outcome == GameOutcome::BlackWin ||
                           summary.outcome == GameOutcome::Draw;
    return summary.gameId != 0 &&
           mode <= static_cast<uint8_t>(GameMode::Online) &&
           variant <= static_cast<uint8_t>(ChessVariant::Chess960) &&
           (summary.variant == ChessVariant::Standard
                ? summary.positionIndex == 518
                : summary.positionIndex < 960) &&
           timeControl <= static_cast<uint8_t>(TimeControl::Rapid10) &&
           localColor <= static_cast<uint8_t>(PieceColor::Black) &&
           validDifficulty && completed &&
           summary.termination != TerminationReason::None &&
           static_cast<uint8_t>(summary.termination) <=
               static_cast<uint8_t>(TerminationReason::Disconnection) &&
           validName(summary.whiteName) && validName(summary.blackName);
}

bool summariesEqual(const GameSummary& left, const GameSummary& right) {
    return left.gameId == right.gameId &&
           left.whiteProfileId == right.whiteProfileId &&
           left.blackProfileId == right.blackProfileId &&
           std::memcmp(left.whiteName, right.whiteName,
                       PLAYER_NAME_MAX + 1) == 0 &&
           std::memcmp(left.blackName, right.blackName,
                       PLAYER_NAME_MAX + 1) == 0 &&
           left.mode == right.mode && left.variant == right.variant &&
           left.positionIndex == right.positionIndex &&
           left.timeControl == right.timeControl &&
           left.aiDifficulty == right.aiDifficulty &&
           left.localColor == right.localColor &&
           left.outcome == right.outcome &&
           left.termination == right.termination &&
           left.plyCount == right.plyCount &&
           left.whiteRemainingMs == right.whiteRemainingMs &&
           left.blackRemainingMs == right.blackRemainingMs;
}

bool encodeProfiles(const ProfileData& data, uint8_t* buffer, size_t size) {
    if (!buffer || size != PROFILE_ENCODED_BYTES ||
        !validProfilesForSave(data)) {
        return false;
    }

    std::memset(buffer, 0, size);
    Writer writer(buffer, size);
    const char magic[4] = {'C', 'P', 'F', '1'};
    if (!writer.bytes(magic, sizeof(magic)) ||
        !writer.u8(PROFILE_VERSION) || !writer.u8(data.profileCount) ||
        !writer.u8(data.activeIndex) || !writer.u8(data.historyCount)) {
        return false;
    }
    for (uint8_t i = 0; i < MAX_PLAYER_PROFILES; ++i) {
        if (!writeProfile(writer, data.profiles[i])) return false;
    }
    for (uint8_t i = 0; i < MAX_GAME_SUMMARIES; ++i) {
        if (!writeSummary(writer, data.history[i])) return false;
    }
    return writeChecksum(writer, buffer) && writer.position() == size;
}

DecodeStatus decodeProfiles(const uint8_t* buffer, size_t size,
                            ProfileData& data) {
    const char magic[4] = {'C', 'P', 'F', '1'};
    if (hasMagicAndDifferentVersion(buffer, size, magic, PROFILE_VERSION)) {
        return DecodeStatus::UnsupportedVersion;
    }
    if (!buffer || size != PROFILE_ENCODED_BYTES ||
        std::memcmp(buffer, magic, sizeof(magic)) != 0) {
        return DecodeStatus::Corrupt;
    }

    Reader checksumReader(buffer + size - 4, 4);
    uint32_t storedChecksum = 0;
    if (!checksumReader.u32(storedChecksum) ||
        storedChecksum != crc32(buffer, size - 4)) {
        return DecodeStatus::Corrupt;
    }

    Reader reader(buffer, size - 4);
    char decodedMagic[4];
    uint8_t version = 0;
    ProfileData decoded;
    if (!reader.bytes(decodedMagic, sizeof(decodedMagic)) ||
        std::memcmp(decodedMagic, magic, sizeof(magic)) != 0 ||
        !reader.u8(version) || version != PROFILE_VERSION ||
        !reader.u8(decoded.profileCount) ||
        !reader.u8(decoded.activeIndex) ||
        !reader.u8(decoded.historyCount) ||
        decoded.profileCount == 0 ||
        decoded.profileCount > MAX_PLAYER_PROFILES ||
        decoded.activeIndex >= decoded.profileCount ||
        decoded.historyCount > MAX_GAME_SUMMARIES) {
        return DecodeStatus::Corrupt;
    }
    for (uint8_t i = 0; i < MAX_PLAYER_PROFILES; ++i) {
        if (!readProfile(reader, decoded.profiles[i])) {
            return DecodeStatus::Corrupt;
        }
        if (decoded.profiles[i].name[PLAYER_NAME_MAX] != '\0') {
            return DecodeStatus::Corrupt;
        }
        decoded.profiles[i].name[PLAYER_NAME_MAX] = '\0';
    }
    for (uint8_t i = 0; i < MAX_GAME_SUMMARIES; ++i) {
        if (!readSummary(reader, decoded.history[i])) {
            return DecodeStatus::Corrupt;
        }
    }
    if (reader.position() != size - 4 || !validProfilesForSave(decoded)) {
        return DecodeStatus::Corrupt;
    }
    data = decoded;
    return DecodeStatus::Ok;
}

bool encodeActive(const ActiveGameParticipants& active,
                  uint8_t* buffer, size_t size) {
    if (!buffer || size != ACTIVE_ENCODED_BYTES || !validActive(active)) {
        return false;
    }
    std::memset(buffer, 0, size);
    Writer writer(buffer, size);
    const char magic[4] = {'C', 'P', 'A', '1'};
    if (!writer.bytes(magic, sizeof(magic)) || !writer.u8(ACTIVE_VERSION) ||
        !writer.u8(active.valid ? 1 : 0) || !writer.u32(active.gameId) ||
        !writer.u8(static_cast<uint8_t>(active.mode)) ||
        !writer.u32(active.whiteProfileId) ||
        !writer.u32(active.blackProfileId) ||
        !writer.bytes(active.whiteName, PLAYER_NAME_MAX + 1) ||
        !writer.bytes(active.blackName, PLAYER_NAME_MAX + 1)) {
        return false;
    }
    return writeChecksum(writer, buffer) && writer.position() == size;
}

DecodeStatus decodeActive(const uint8_t* buffer, size_t size,
                          ActiveGameParticipants& active) {
    const char magic[4] = {'C', 'P', 'A', '1'};
    if (hasMagicAndDifferentVersion(buffer, size, magic, ACTIVE_VERSION)) {
        return DecodeStatus::UnsupportedVersion;
    }
    if (!buffer || size != ACTIVE_ENCODED_BYTES ||
        std::memcmp(buffer, magic, sizeof(magic)) != 0) {
        return DecodeStatus::Corrupt;
    }
    Reader checksumReader(buffer + size - 4, 4);
    uint32_t storedChecksum = 0;
    if (!checksumReader.u32(storedChecksum) ||
        storedChecksum != crc32(buffer, size - 4)) {
        return DecodeStatus::Corrupt;
    }

    Reader reader(buffer, size - 4);
    char decodedMagic[4];
    uint8_t version = 0, valid = 0, mode = 0;
    ActiveGameParticipants decoded;
    if (!reader.bytes(decodedMagic, sizeof(decodedMagic)) ||
        std::memcmp(decodedMagic, magic, sizeof(magic)) != 0 ||
        !reader.u8(version) || version != ACTIVE_VERSION ||
        !reader.u8(valid) || valid > 1 ||
        !reader.u32(decoded.gameId) || !reader.u8(mode) ||
        mode > static_cast<uint8_t>(GameMode::Online) ||
        !reader.u32(decoded.whiteProfileId) ||
        !reader.u32(decoded.blackProfileId) ||
        !reader.bytes(decoded.whiteName, PLAYER_NAME_MAX + 1) ||
        !reader.bytes(decoded.blackName, PLAYER_NAME_MAX + 1) ||
        reader.position() != size - 4) {
        return DecodeStatus::Corrupt;
    }
    decoded.valid = valid != 0;
    decoded.mode = static_cast<GameMode>(mode);
    if (decoded.whiteName[PLAYER_NAME_MAX] != '\0' ||
        decoded.blackName[PLAYER_NAME_MAX] != '\0') {
        return DecodeStatus::Corrupt;
    }
    decoded.whiteName[PLAYER_NAME_MAX] = '\0';
    decoded.blackName[PLAYER_NAME_MAX] = '\0';
    if (!validActive(decoded)) return DecodeStatus::Corrupt;
    active = decoded;
    return DecodeStatus::Ok;
}

bool encodePendingResult(const GameSummary& summary,
                         uint8_t* buffer, size_t size) {
    if (!buffer || size != PENDING_RESULT_ENCODED_BYTES ||
        !validCompletedSummary(summary)) {
        return false;
    }
    std::memset(buffer, 0, size);
    Writer writer(buffer, size);
    const char magic[4] = {'C', 'P', 'R', 'J'};
    if (!writer.bytes(magic, sizeof(magic)) ||
        !writer.u8(PENDING_VERSION) || !writeSummary(writer, summary)) {
        return false;
    }
    return writeChecksum(writer, buffer) && writer.position() == size;
}

DecodeStatus decodePendingResult(const uint8_t* buffer, size_t size,
                                 GameSummary& summary) {
    const char magic[4] = {'C', 'P', 'R', 'J'};
    if (hasMagicAndDifferentVersion(buffer, size, magic, PENDING_VERSION)) {
        return DecodeStatus::UnsupportedVersion;
    }
    if (!buffer || size != PENDING_RESULT_ENCODED_BYTES ||
        std::memcmp(buffer, magic, sizeof(magic)) != 0) {
        return DecodeStatus::Corrupt;
    }
    Reader checksumReader(buffer + size - 4, 4);
    uint32_t storedChecksum = 0;
    if (!checksumReader.u32(storedChecksum) ||
        storedChecksum != crc32(buffer, size - 4)) {
        return DecodeStatus::Corrupt;
    }

    Reader reader(buffer, size - 4);
    char decodedMagic[4];
    uint8_t version = 0;
    GameSummary decoded;
    if (!reader.bytes(decodedMagic, sizeof(decodedMagic)) ||
        std::memcmp(decodedMagic, magic, sizeof(magic)) != 0 ||
        !reader.u8(version) || version != PENDING_VERSION ||
        !readSummary(reader, decoded) || reader.position() != size - 4 ||
        !validCompletedSummary(decoded)) {
        return DecodeStatus::Corrupt;
    }
    summary = decoded;
    return DecodeStatus::Ok;
}

} // namespace ProfileStorageCodec
