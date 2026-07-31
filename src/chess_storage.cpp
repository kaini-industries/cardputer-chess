#include "chess_storage.h"

#include "chess_storage_codec.h"
#include "nvs_blob_store.h"

#include <new>

namespace {

static constexpr const char* NVS_NAMESPACE = "chess";
static constexpr const char* NVS_KEY = "game";

ChessStorage::LoadStatus mapReadStatus(NvsBlobStore::ReadStatus status) {
    switch (status) {
        case NvsBlobStore::ReadStatus::Missing:
            return ChessStorage::LoadStatus::Missing;
        case NvsBlobStore::ReadStatus::WrongType:
            return ChessStorage::LoadStatus::Corrupt;
        case NvsBlobStore::ReadStatus::TooLarge:
            return ChessStorage::LoadStatus::UnsupportedVersion;
        case NvsBlobStore::ReadStatus::IoError:
            return ChessStorage::LoadStatus::IoError;
        case NvsBlobStore::ReadStatus::Ok:
            break;
    }
    return ChessStorage::LoadStatus::IoError;
}

ChessStorage::LoadStatus mapDecodeStatus(
        ChessStorageCodec::DecodeStatus status) {
    switch (status) {
        case ChessStorageCodec::DecodeStatus::Ok:
            return ChessStorage::LoadStatus::Loaded;
        case ChessStorageCodec::DecodeStatus::Corrupt:
            return ChessStorage::LoadStatus::Corrupt;
        case ChessStorageCodec::DecodeStatus::UnsupportedVersion:
            return ChessStorage::LoadStatus::UnsupportedVersion;
        case ChessStorageCodec::DecodeStatus::NoMemory:
            return ChessStorage::LoadStatus::IoError;
    }
    return ChessStorage::LoadStatus::Corrupt;
}

ChessStorage::LoadResult decodeBlob(
        const NvsBlobStore::Blob& blob,
        ChessBoard& board, MoveRecord* history, uint8_t& historyCount,
        ChessStorageCodec::SaveMetadata& metadata) {
    ChessStorage::LoadResult result;
    if (blob.size != 0 && blob.data) result.sourceVersion = blob.data[0];
    const ChessStorageCodec::DecodeStatus decodeStatus =
        ChessStorageCodec::decode(
            blob.data, blob.size, board, history,
            ChessStorageCodec::MAX_SAVED_HISTORY, historyCount, metadata);
    result.status = mapDecodeStatus(decodeStatus);
    if (result.status == ChessStorage::LoadStatus::Loaded) {
        result.sourceVersion = metadata.sourceVersion;
        result.participantsEmbedded = metadata.participantsEmbedded;
        result.gameId = metadata.participantsEmbedded
            ? metadata.participants.gameId : 0;
    }
    return result;
}

} // namespace

namespace ChessStorage {

bool saveGame(const ChessBoard& board,
              const MoveRecord* history, uint8_t historyCount,
              bool historyOverflow,
              AIDifficulty aiDifficulty, PieceColor aiColor,
              PieceColor localColor, bool boardFlipped,
              ChessVariant variant, uint16_t positionIndex,
              TimeControl timeControl, uint32_t timeWhiteMs,
              uint32_t timeBlackMs, bool timerRunning,
              const ActiveGameParticipants& participants) {
    const size_t capacity = ChessStorageCodec::encodedSize(historyCount);
    if (capacity == 0) return false;
    uint8_t* buffer = new (std::nothrow) uint8_t[capacity];
    if (!buffer) return false;

    ChessStorageCodec::SaveMetadata metadata;
    metadata.historyOverflow = historyOverflow;
    metadata.aiDifficulty = aiDifficulty;
    metadata.aiColor = aiColor;
    metadata.localColor = localColor;
    metadata.boardFlipped = boardFlipped;
    metadata.variant = variant;
    metadata.positionIndex = positionIndex;
    metadata.timeControl = timeControl;
    metadata.timeWhiteMs = timeWhiteMs;
    metadata.timeBlackMs = timeBlackMs;
    metadata.timerRunning = timerRunning;
    metadata.participants = participants;

    size_t bytesWritten = 0;
    const bool encoded = ChessStorageCodec::encode(
        board, history, historyCount, metadata,
        buffer, capacity, bytesWritten);
    const bool saved = encoded && bytesWritten == capacity &&
        NvsBlobStore::write(NVS_NAMESPACE, NVS_KEY, buffer, bytesWritten);
    delete[] buffer;
    return saved;
}

LoadResult loadGame(ChessBoard& board,
                    MoveRecord* history, uint8_t& historyCount,
                    bool& historyOverflow,
                    AIDifficulty& aiDifficulty, PieceColor& aiColor,
                    PieceColor& localColor, bool& boardFlipped,
                    ChessVariant& variant, uint16_t& positionIndex,
                    TimeControl& timeControl, uint32_t& timeWhiteMs,
                    uint32_t& timeBlackMs, bool& timerRunning,
                    ActiveGameParticipants& participants) {
    NvsBlobStore::Blob blob;
    const NvsBlobStore::ReadStatus readStatus = NvsBlobStore::read(
        NVS_NAMESPACE, NVS_KEY,
        ChessStorageCodec::MAX_SUPPORTED_READ_BYTES, blob);
    if (readStatus != NvsBlobStore::ReadStatus::Ok) {
        LoadResult result;
        result.status = mapReadStatus(readStatus);
        return result;
    }

    ChessStorageCodec::SaveMetadata metadata;
    uint8_t decodedHistoryCount = 0;
    LoadResult result = decodeBlob(blob, board, history,
                                   decodedHistoryCount, metadata);
    if (result.status != LoadStatus::Loaded) return result;

    historyCount = decodedHistoryCount;
    historyOverflow = metadata.historyOverflow;
    aiDifficulty = metadata.aiDifficulty;
    aiColor = metadata.aiColor;
    localColor = metadata.localColor;
    boardFlipped = metadata.boardFlipped;
    variant = metadata.variant;
    positionIndex = metadata.positionIndex;
    timeControl = metadata.timeControl;
    timeWhiteMs = metadata.timeWhiteMs;
    timeBlackMs = metadata.timeBlackMs;
    timerRunning = metadata.timerRunning;
    participants = metadata.participants;
    return result;
}

LoadResult probe() {
    NvsBlobStore::Blob blob;
    const NvsBlobStore::ReadStatus readStatus = NvsBlobStore::read(
        NVS_NAMESPACE, NVS_KEY,
        ChessStorageCodec::MAX_SUPPORTED_READ_BYTES, blob);
    if (readStatus != NvsBlobStore::ReadStatus::Ok) {
        LoadResult result;
        result.status = mapReadStatus(readStatus);
        return result;
    }

    MoveRecord* history = new (std::nothrow)
        MoveRecord[ChessStorageCodec::MAX_SAVED_HISTORY];
    if (!history) {
        LoadResult result;
        result.status = LoadStatus::IoError;
        return result;
    }
    ChessBoard board;
    uint8_t historyCount = 0;
    ChessStorageCodec::SaveMetadata metadata;
    LoadResult result = decodeBlob(blob, board, history,
                                   historyCount, metadata);
    delete[] history;
    return result;
}

bool hasSave() {
    return probe().status == LoadStatus::Loaded;
}

bool clearSave() {
    return NvsBlobStore::erase(NVS_NAMESPACE, NVS_KEY);
}

bool clearIfGameId(uint32_t expectedGameId) {
    if (expectedGameId == 0) return false;
    const LoadResult result = probe();
    if (result.status == LoadStatus::Missing) return true;
    if (result.status != LoadStatus::Loaded ||
        !result.participantsEmbedded || result.gameId != expectedGameId) {
        return false;
    }
    return clearSave();
}

} // namespace ChessStorage
