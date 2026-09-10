#ifndef CHESS_STORAGE_H
#define CHESS_STORAGE_H

#include "chess_ai.h"
#include "game_records.h"

#include <cstdint>

namespace ChessStorage {

enum class LoadStatus : uint8_t {
    Loaded,
    Missing,
    Corrupt,
    UnsupportedVersion,
    IoError,
};

struct LoadResult {
    LoadStatus status = LoadStatus::Missing;
    uint8_t sourceVersion = 0;
    bool participantsEmbedded = false;
    uint32_t gameId = 0;
};

// Writes a CRC32-protected v7 record (recent history is retained after overflow). Participants (including the gameId) are
// committed in the same NVS blob as the board. Both profile IDs may be zero,
// but gameId and sanitized display names are required.
bool saveGame(const ChessBoard& board,
              const MoveRecord* history, uint8_t historyCount,
              bool historyOverflow,
              AIDifficulty aiDifficulty, PieceColor aiColor,
              PieceColor localColor, bool boardFlipped,
              ChessVariant variant, uint16_t positionIndex,
              TimeControl timeControl, uint32_t timeWhiteMs,
              uint32_t timeBlackMs, bool timerRunning,
              const ActiveGameParticipants& participants);

// Safely decodes v1-v7. All output arguments remain untouched on failure.
// participants.valid is false for v1-v5 because their separate active_meta
// record cannot be proven to belong to the board save.
LoadResult loadGame(ChessBoard& board,
                    MoveRecord* history, uint8_t& historyCount,
                    bool& historyOverflow,
                    AIDifficulty& aiDifficulty, PieceColor& aiColor,
                    PieceColor& localColor, bool& boardFlipped,
                    ChessVariant& variant, uint16_t& positionIndex,
                    TimeControl& timeControl, uint32_t& timeWhiteMs,
                    uint32_t& timeBlackMs, bool& timerRunning,
                    ActiveGameParticipants& participants);

// Fully validates the record and reports its version/embedded gameId without
// exposing partially decoded state.
LoadResult probe();

// Compatibility convenience. True only for a fully valid supported save.
bool hasSave();

// Idempotent erase. clearIfGameId refuses to delete a legacy, corrupt, future,
// or a different game.
bool clearSave();
bool clearIfGameId(uint32_t expectedGameId);

} // namespace ChessStorage

#endif // CHESS_STORAGE_H
