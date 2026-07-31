#ifndef CURSOR_NAVIGATION_H
#define CURSOR_NAVIGATION_H

#include "chess_types.h"

#include <cstdint>

// Directions are expressed in displayed grid coordinates: row zero is the
// top of the screen and column zero is the left edge. Callers must transform
// board squares into display coordinates before invoking the chooser.
enum class CursorDirection : uint8_t {
    Up,
    Down,
    Left,
    Right,
};

// Chooses a legal destination in direction from current.
//
// Directional candidates are ranked by distance from the requested axis,
// then forward distance, then row and column. If there is no candidate in the
// requested direction, navigation wraps to the opposite edge. Invalid cells
// are ignored, duplicate cells do not affect the result, and current is
// excluded whenever another valid destination exists. A sole current-cell
// destination is retained for Chess960's zero-displacement castling case.
//
// Returns false without modifying next when the input has no usable
// destination or direction/current is invalid.
bool chooseCursorDestination(const Square* cells, uint8_t count,
                             Square current, CursorDirection direction,
                             Square& next);

// Chooses the next unique legal destination in display order (top-to-bottom,
// then left-to-right), wrapping after the final cell. The input order is not
// significant and duplicate cells (such as four promotion variants) count as
// one destination. If current is not itself a destination, the first display-
// ordered destination is returned. A sole same-square destination is retained
// for Chess960's zero-displacement castling case.
//
// Returns false without modifying next when the input has no usable
// destination or current is invalid.
bool cycleCursorDestination(const Square* cells, uint8_t count,
                            Square current, Square& next);

#endif // CURSOR_NAVIGATION_H
