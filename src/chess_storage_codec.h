#ifndef CHESS_STORAGE_CODEC_H
#define CHESS_STORAGE_CODEC_H

#include "chess_ai.h"
#include "game_records.h"

#include <cstddef>
#include <cstdint>

namespace ChessStorageCodec {

static constexpr uint8_t MAX_SAVED_HISTORY = 250;
static constexpr uint8_t CURRENT_VERSION = 7;
static constexpr size_t MAX_ENCODED_BYTES =
    131 + static_cast<size_t>(MAX_SAVED_HISTORY) * 16 + 4;
// v2/v3 used 33-byte atomic-era records and are the largest legacy blobs.
static constexpr size_t MAX_SUPPORTED_READ_BYTES =
    91 + static_cast<size_t>(MAX_SAVED_HISTORY) * 33 + 4;

enum class DecodeStatus : uint8_t {
    Ok,
    Corrupt,
    UnsupportedVersion,
    NoMemory,
};

struct SaveMetadata {
    bool historyOverflow = false;
    AIDifficulty aiDifficulty = AIDifficulty::None;
    PieceColor aiColor = PieceColor::Black;
    PieceColor localColor = PieceColor::White;
    bool boardFlipped = false;
    ChessVariant variant = ChessVariant::Standard;
    uint16_t positionIndex = 518;
    TimeControl timeControl = TimeControl::None;
    uint32_t timeWhiteMs = 0;
    uint32_t timeBlackMs = 0;
    bool timerRunning = false;
    ActiveGameParticipants participants = {};

    // Decode information; encode ignores these fields.
    uint8_t sourceVersion = 0;
    bool participantsEmbedded = false;
};

size_t encodedSize(uint8_t historyCount);

bool encode(const ChessBoard& board,
            const MoveRecord* history, uint8_t historyCount,
            const SaveMetadata& metadata,
            uint8_t* buffer, size_t capacity, size_t& bytesWritten);

// Decodes v1-v7. v7 overflow history is a recent suffix, not a prefix. All outputs remain untouched unless Ok is returned.
DecodeStatus decode(const uint8_t* buffer, size_t size,
                    ChessBoard& board,
                    MoveRecord* history, size_t historyCapacity,
                    uint8_t& historyCount,
                    SaveMetadata& metadata);

} // namespace ChessStorageCodec

#endif // CHESS_STORAGE_CODEC_H
