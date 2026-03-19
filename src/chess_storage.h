#ifndef CHESS_STORAGE_H
#define CHESS_STORAGE_H

#include "chess_board.h"
#include "chess_ai.h"

// =====================================================================
// ChessStorage: persist/restore chess game state to ESP32 NVS.
// Saves board, move history, and mode configuration so the game
// survives power cycles.
// =====================================================================

namespace ChessStorage {

    // Save the current game state to NVS.
    void saveGame(const ChessBoard& board,
                  const MoveRecord* history, uint8_t historyCount,
                  bool historyOverflow,
                  AIDifficulty aiDifficulty, PieceColor aiColor,
                  PieceColor localColor, bool boardFlipped,
                  ChessVariant variant);

    // Load a previously saved game. Returns true if a valid save was found.
    bool loadGame(ChessBoard& board,
                  MoveRecord* history, uint8_t& historyCount,
                  bool& historyOverflow,
                  AIDifficulty& aiDifficulty, PieceColor& aiColor,
                  PieceColor& localColor, bool& boardFlipped,
                  ChessVariant& variant);

    // Check if a saved game exists.
    bool hasSave();

    // Clear the saved game.
    void clearSave();

} // namespace ChessStorage

#endif // CHESS_STORAGE_H
