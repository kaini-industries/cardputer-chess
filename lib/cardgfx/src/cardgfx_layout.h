#ifndef CARDGFX_LAYOUT_H
#define CARDGFX_LAYOUT_H

#include "cardgfx_canvas.h"
#include <cstdint>

namespace CardGFX {

// ── Anchor ───────────────────────────────────────────────────────

enum class Anchor : uint8_t {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

// ── Bounds ───────────────────────────────────────────────────────

/**
 * A widget's position and size, with optional anchoring.
 */
struct Bounds {
    int16_t  x = 0;
    int16_t  y = 0;
    uint16_t w = 0;
    uint16_t h = 0;

    Rect toRect() const { return {x, y, w, h}; }
};

// ── Layout Helpers ───────────────────────────────────────────────

namespace Layout {

    /**
     * Compute the (x, y) position for a child rect of size (childW, childH)
     * anchored within a parent rect, with optional margin.
     */
    Bounds anchor(uint16_t childW, uint16_t childH,
                  const Bounds& parent, Anchor anchor,
                  int16_t marginX = 0, int16_t marginY = 0);

    /**
     * Arrange an array of bounds horizontally, starting at (startX, startY).
     * Each element is placed side by side with `spacing` pixels between.
     *
     * @param bounds     Array of bounds to arrange. X values are overwritten.
     * @param count      Number of elements.
     * @param startX     Starting X.
     * @param startY     Y position for all elements.
     * @param spacing    Pixels between elements.
     */
    void row(Bounds* bounds, uint8_t count,
             int16_t startX, int16_t startY, uint8_t spacing);

    /**
     * Arrange an array of bounds vertically.
     */
    void column(Bounds* bounds, uint8_t count,
                int16_t startX, int16_t startY, uint8_t spacing);

    /**
     * Arrange bounds in a grid pattern.
     *
     * @param bounds   Array of bounds (count = cols * rows).
     * @param cols     Number of columns.
     * @param rows     Number of rows.
     * @param cellW    Width of each cell.
     * @param cellH    Height of each cell.
     * @param startX   Grid origin X.
     * @param startY   Grid origin Y.
     * @param padX     Horizontal padding within cell.
     * @param padY     Vertical padding within cell.
     */
    void grid(Bounds* bounds, uint8_t cols, uint8_t rows,
              uint16_t cellW, uint16_t cellH,
              int16_t startX, int16_t startY,
              uint8_t padX = 0, uint8_t padY = 0);

    /**
     * Center a child within a parent bounds.
     */
    inline Bounds center(uint16_t childW, uint16_t childH,
                         const Bounds& parent) {
        return {
            (int16_t)(parent.x + (parent.w - childW) / 2),
            (int16_t)(parent.y + (parent.h - childH) / 2),
            childW, childH
        };
    }

    /**
     * Split a bounds into left and right portions.
     * @param split  Width of the left portion.
     */
    inline void splitH(const Bounds& parent, uint16_t split,
                       Bounds& left, Bounds& right) {
        left  = {parent.x, parent.y, split, parent.h};
        right = {(int16_t)(parent.x + split), parent.y,
                 (uint16_t)(parent.w - split), parent.h};
    }

    /**
     * Split a bounds into top and bottom portions.
     * @param split  Height of the top portion.
     */
    inline void splitV(const Bounds& parent, uint16_t split,
                       Bounds& top, Bounds& bottom) {
        top    = {parent.x, parent.y, parent.w, split};
        bottom = {parent.x, (int16_t)(parent.y + split),
                  parent.w, (uint16_t)(parent.h - split)};
    }

} // namespace Layout
} // namespace CardGFX

#endif // CARDGFX_LAYOUT_H
