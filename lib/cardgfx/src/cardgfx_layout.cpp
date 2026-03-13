#include "cardgfx_layout.h"

namespace CardGFX {
namespace Layout {

Bounds anchor(uint16_t childW, uint16_t childH,
              const Bounds& parent, Anchor a,
              int16_t marginX, int16_t marginY) {
    Bounds result = {0, 0, childW, childH};

    switch (a) {
    case Anchor::TopLeft:
        result.x = parent.x + marginX;
        result.y = parent.y + marginY;
        break;
    case Anchor::TopCenter:
        result.x = parent.x + (parent.w - childW) / 2;
        result.y = parent.y + marginY;
        break;
    case Anchor::TopRight:
        result.x = parent.x + parent.w - childW - marginX;
        result.y = parent.y + marginY;
        break;
    case Anchor::CenterLeft:
        result.x = parent.x + marginX;
        result.y = parent.y + (parent.h - childH) / 2;
        break;
    case Anchor::Center:
        result.x = parent.x + (parent.w - childW) / 2;
        result.y = parent.y + (parent.h - childH) / 2;
        break;
    case Anchor::CenterRight:
        result.x = parent.x + parent.w - childW - marginX;
        result.y = parent.y + (parent.h - childH) / 2;
        break;
    case Anchor::BottomLeft:
        result.x = parent.x + marginX;
        result.y = parent.y + parent.h - childH - marginY;
        break;
    case Anchor::BottomCenter:
        result.x = parent.x + (parent.w - childW) / 2;
        result.y = parent.y + parent.h - childH - marginY;
        break;
    case Anchor::BottomRight:
        result.x = parent.x + parent.w - childW - marginX;
        result.y = parent.y + parent.h - childH - marginY;
        break;
    }
    return result;
}

void row(Bounds* bounds, uint8_t count,
         int16_t startX, int16_t startY, uint8_t spacing) {
    int16_t x = startX;
    for (uint8_t i = 0; i < count; i++) {
        bounds[i].x = x;
        bounds[i].y = startY;
        x += bounds[i].w + spacing;
    }
}

void column(Bounds* bounds, uint8_t count,
            int16_t startX, int16_t startY, uint8_t spacing) {
    int16_t y = startY;
    for (uint8_t i = 0; i < count; i++) {
        bounds[i].x = startX;
        bounds[i].y = y;
        y += bounds[i].h + spacing;
    }
}

void grid(Bounds* bounds, uint8_t cols, uint8_t rows,
          uint16_t cellW, uint16_t cellH,
          int16_t startX, int16_t startY,
          uint8_t padX, uint8_t padY) {
    for (uint8_t r = 0; r < rows; r++) {
        for (uint8_t c = 0; c < cols; c++) {
            uint8_t idx = r * cols + c;
            bounds[idx].x = startX + c * cellW + padX;
            bounds[idx].y = startY + r * cellH + padY;
            bounds[idx].w = cellW - 2 * padX;
            bounds[idx].h = cellH - 2 * padY;
        }
    }
}

} // namespace Layout
} // namespace CardGFX
