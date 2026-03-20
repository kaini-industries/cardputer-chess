#ifndef PUZZLE_DATA_H
#define PUZZLE_DATA_H

#include "chess_types.h"
#include <cstdint>

// =====================================================================
// Puzzle data format and embedded puzzle database.
// Each puzzle is a 64-char position string + solution moves.
// =====================================================================

// Puzzle types
enum class PuzzleType : uint8_t {
    MateIn1  = 0,
    MateIn2  = 1,
    Tactic   = 2
};

class ChessBoard;

// Get the total number of puzzles
uint16_t puzzleCount();

// Get puzzle category counts
uint16_t puzzleCountByType(PuzzleType type);

// Get the nth puzzle of a given type (returns index into the global array)
// Returns 0xFFFF if not found.
uint16_t puzzleIndexByType(PuzzleType type, uint16_t nth);

// Load a puzzle into a ChessBoard, returning solution moves and metadata
void loadPuzzleIntoBoard(uint16_t index, ChessBoard& board, Move* solution,
                         uint8_t& solutionLen, PuzzleType& type, uint8_t& rating);

#endif // PUZZLE_DATA_H
