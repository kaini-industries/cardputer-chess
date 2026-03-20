#ifndef PUZZLE_STORAGE_H
#define PUZZLE_STORAGE_H

#include <cstdint>

// =====================================================================
// Puzzle progress tracking in NVS.
// =====================================================================

struct PuzzleProgress {
    uint8_t completed[32]; // Bitmask for up to 256 puzzles
    uint8_t lastPuzzle;    // Last attempted puzzle index
};

namespace PuzzleStorage {
    void loadProgress(PuzzleProgress& progress);
    void saveProgress(const PuzzleProgress& progress);
    bool isPuzzleCompleted(const PuzzleProgress& progress, uint8_t index);
    void markPuzzleCompleted(PuzzleProgress& progress, uint8_t index);
    uint16_t completedCount(const PuzzleProgress& progress, uint16_t total);
}

#endif // PUZZLE_STORAGE_H
