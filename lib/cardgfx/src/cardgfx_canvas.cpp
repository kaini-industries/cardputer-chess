#include "cardgfx_canvas.h"
#include "cardgfx_hal.h"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>

// Built-in 5x7 font (ASCII 32-126, 95 characters)
// Each character is 5 bytes wide, each byte is a column (LSB = top row).
static const uint8_t FONT_5X7[] = {
    0x00,0x00,0x00,0x00,0x00, // 32 (space)
    0x00,0x00,0x5F,0x00,0x00, // 33 !
    0x00,0x07,0x00,0x07,0x00, // 34 "
    0x14,0x7F,0x14,0x7F,0x14, // 35 #
    0x24,0x2A,0x7F,0x2A,0x12, // 36 $
    0x23,0x13,0x08,0x64,0x62, // 37 %
    0x36,0x49,0x55,0x22,0x50, // 38 &
    0x00,0x05,0x03,0x00,0x00, // 39 '
    0x00,0x1C,0x22,0x41,0x00, // 40 (
    0x00,0x41,0x22,0x1C,0x00, // 41 )
    0x08,0x2A,0x1C,0x2A,0x08, // 42 *
    0x08,0x08,0x3E,0x08,0x08, // 43 +
    0x00,0x50,0x30,0x00,0x00, // 44 ,
    0x08,0x08,0x08,0x08,0x08, // 45 -
    0x00,0x60,0x60,0x00,0x00, // 46 .
    0x20,0x10,0x08,0x04,0x02, // 47 /
    0x3E,0x51,0x49,0x45,0x3E, // 48 0
    0x00,0x42,0x7F,0x40,0x00, // 49 1
    0x42,0x61,0x51,0x49,0x46, // 50 2
    0x21,0x41,0x45,0x4B,0x31, // 51 3
    0x18,0x14,0x12,0x7F,0x10, // 52 4
    0x27,0x45,0x45,0x45,0x39, // 53 5
    0x3C,0x4A,0x49,0x49,0x30, // 54 6
    0x01,0x71,0x09,0x05,0x03, // 55 7
    0x36,0x49,0x49,0x49,0x36, // 56 8
    0x06,0x49,0x49,0x29,0x1E, // 57 9
    0x00,0x36,0x36,0x00,0x00, // 58 :
    0x00,0x56,0x36,0x00,0x00, // 59 ;
    0x00,0x08,0x14,0x22,0x41, // 60 <
    0x14,0x14,0x14,0x14,0x14, // 61 =
    0x41,0x22,0x14,0x08,0x00, // 62 >
    0x02,0x01,0x51,0x09,0x06, // 63 ?
    0x32,0x49,0x79,0x41,0x3E, // 64 @
    0x7E,0x11,0x11,0x11,0x7E, // 65 A
    0x7F,0x49,0x49,0x49,0x36, // 66 B
    0x3E,0x41,0x41,0x41,0x22, // 67 C
    0x7F,0x41,0x41,0x22,0x1C, // 68 D
    0x7F,0x49,0x49,0x49,0x41, // 69 E
    0x7F,0x09,0x09,0x01,0x01, // 70 F
    0x3E,0x41,0x41,0x51,0x32, // 71 G
    0x7F,0x08,0x08,0x08,0x7F, // 72 H
    0x00,0x41,0x7F,0x41,0x00, // 73 I
    0x20,0x40,0x41,0x3F,0x01, // 74 J
    0x7F,0x08,0x14,0x22,0x41, // 75 K
    0x7F,0x40,0x40,0x40,0x40, // 76 L
    0x7F,0x02,0x04,0x02,0x7F, // 77 M
    0x7F,0x04,0x08,0x10,0x7F, // 78 N
    0x3E,0x41,0x41,0x41,0x3E, // 79 O
    0x7F,0x09,0x09,0x09,0x06, // 80 P
    0x3E,0x41,0x51,0x21,0x5E, // 81 Q
    0x7F,0x09,0x19,0x29,0x46, // 82 R
    0x46,0x49,0x49,0x49,0x31, // 83 S
    0x01,0x01,0x7F,0x01,0x01, // 84 T
    0x3F,0x40,0x40,0x40,0x3F, // 85 U
    0x1F,0x20,0x40,0x20,0x1F, // 86 V
    0x7F,0x20,0x18,0x20,0x7F, // 87 W
    0x63,0x14,0x08,0x14,0x63, // 88 X
    0x03,0x04,0x78,0x04,0x03, // 89 Y
    0x61,0x51,0x49,0x45,0x43, // 90 Z
    0x00,0x00,0x7F,0x41,0x41, // 91 [
    0x02,0x04,0x08,0x10,0x20, // 92 backslash
    0x41,0x41,0x7F,0x00,0x00, // 93 ]
    0x04,0x02,0x01,0x02,0x04, // 94 ^
    0x40,0x40,0x40,0x40,0x40, // 95 _
    0x00,0x01,0x02,0x04,0x00, // 96 `
    0x20,0x54,0x54,0x54,0x78, // 97 a
    0x7F,0x48,0x44,0x44,0x38, // 98 b
    0x38,0x44,0x44,0x44,0x20, // 99 c
    0x38,0x44,0x44,0x48,0x7F, // 100 d
    0x38,0x54,0x54,0x54,0x18, // 101 e
    0x08,0x7E,0x09,0x01,0x02, // 102 f
    0x08,0x54,0x54,0x54,0x3C, // 103 g
    0x7F,0x08,0x04,0x04,0x78, // 104 h
    0x00,0x44,0x7D,0x40,0x00, // 105 i
    0x20,0x40,0x44,0x3D,0x00, // 106 j
    0x00,0x7F,0x10,0x28,0x44, // 107 k
    0x00,0x41,0x7F,0x40,0x00, // 108 l
    0x7C,0x04,0x18,0x04,0x78, // 109 m
    0x7C,0x08,0x04,0x04,0x78, // 110 n
    0x38,0x44,0x44,0x44,0x38, // 111 o
    0x7C,0x14,0x14,0x14,0x08, // 112 p
    0x08,0x14,0x14,0x18,0x7C, // 113 q
    0x7C,0x08,0x04,0x04,0x08, // 114 r
    0x48,0x54,0x54,0x54,0x20, // 115 s
    0x04,0x3F,0x44,0x40,0x20, // 116 t
    0x3C,0x40,0x40,0x20,0x7C, // 117 u
    0x1C,0x20,0x40,0x20,0x1C, // 118 v
    0x3C,0x40,0x30,0x40,0x3C, // 119 w
    0x44,0x28,0x10,0x28,0x44, // 120 x
    0x0C,0x50,0x50,0x50,0x3C, // 121 y
    0x44,0x64,0x54,0x4C,0x44, // 122 z
    0x00,0x08,0x36,0x41,0x00, // 123 {
    0x00,0x00,0x7F,0x00,0x00, // 124 |
    0x00,0x41,0x36,0x08,0x00, // 125 }
    0x08,0x04,0x08,0x10,0x08, // 126 ~
};

namespace CardGFX {

// ── Canvas ───────────────────────────────────────────────────────

Canvas::Canvas() = default;

Canvas::~Canvas() {
    destroy();
}

Canvas::Canvas(Canvas&& other) noexcept
    : m_buffer(other.m_buffer)
    , m_width(other.m_width)
    , m_height(other.m_height)
    , m_psram(other.m_psram)
    , m_dirty(other.m_dirty)
    , m_dirtyRect(other.m_dirtyRect)
{
    other.m_buffer = nullptr;
    other.m_width = 0;
    other.m_height = 0;
}

Canvas& Canvas::operator=(Canvas&& other) noexcept {
    if (this != &other) {
        destroy();
        m_buffer    = other.m_buffer;
        m_width     = other.m_width;
        m_height    = other.m_height;
        m_psram     = other.m_psram;
        m_dirty     = other.m_dirty;
        m_dirtyRect = other.m_dirtyRect;
        other.m_buffer = nullptr;
        other.m_width = 0;
        other.m_height = 0;
    }
    return *this;
}

bool Canvas::create(uint16_t w, uint16_t h, bool psram) {
    destroy();  // Free any existing buffer

    uint32_t bytes = (uint32_t)w * h * sizeof(uint16_t);

#if CARDGFX_USE_PSRAM
    if (psram) {
        m_buffer = (uint16_t*)ps_malloc(bytes);
    } else {
        m_buffer = (uint16_t*)malloc(bytes);
    }
#else
    (void)psram;
    m_buffer = (uint16_t*)malloc(bytes);
#endif

    if (!m_buffer) return false;

    m_width  = w;
    m_height = h;
    m_psram  = psram;
    m_dirty  = false;
    m_dirtyRect = {};

    // Clear to black
    memset(m_buffer, 0, bytes);

    return true;
}

void Canvas::destroy() {
    if (m_buffer) {
        free(m_buffer);
        m_buffer = nullptr;
    }
    m_width  = 0;
    m_height = 0;
    m_dirty  = false;
    m_dirtyRect = {};
}

void Canvas::markDirty(const Rect& r) {
    if (r.empty()) return;
    if (!m_dirty) {
        m_dirty = true;
        m_dirtyRect = r;
    } else {
        m_dirtyRect.merge(r);
    }
}

void Canvas::markAllDirty() {
    m_dirty = true;
    m_dirtyRect = {0, 0, m_width, m_height};
}

void Canvas::clearDirty() {
    m_dirty = false;
    m_dirtyRect = {};
}

bool Canvas::clipRect(int16_t& x, int16_t& y, uint16_t& w, uint16_t& h) const {
    if (x >= m_width || y >= m_height) return false;
    if (x < 0) { if ((uint16_t)(-x) >= w) return false; w += x; x = 0; }
    if (y < 0) { if ((uint16_t)(-y) >= h) return false; h += y; y = 0; }
    if (x + w > m_width)  w = m_width - x;
    if (y + h > m_height) h = m_height - y;
    return w > 0 && h > 0;
}

// ── Drawing Primitives ───────────────────────────────────────────

void Canvas::fill(uint16_t color) {
    if (!m_buffer) return;
    uint32_t count = (uint32_t)m_width * m_height;
    // Fast fill: if color is 0x0000 or 0xFFFF, use memset
    if (color == 0x0000) {
        memset(m_buffer, 0, count * 2);
    } else if (color == 0xFFFF) {
        memset(m_buffer, 0xFF, count * 2);
    } else {
        for (uint32_t i = 0; i < count; i++) {
            m_buffer[i] = color;
        }
    }
    markAllDirty();
}

void Canvas::fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                       uint16_t color) {
    if (!m_buffer) return;
    if (!clipRect(x, y, w, h)) return;

    for (uint16_t row = 0; row < h; row++) {
        uint16_t* dst = m_buffer + (uint32_t)(y + row) * m_width + x;
        for (uint16_t col = 0; col < w; col++) {
            dst[col] = color;
        }
    }
    markDirty({x, y, w, h});
}

void Canvas::drawRect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                       uint16_t color) {
    drawHLine(x, y, w, color);
    drawHLine(x, y + h - 1, w, color);
    drawVLine(x, y, h, color);
    drawVLine(x + w - 1, y, h, color);
}

void Canvas::drawHLine(int16_t x, int16_t y, uint16_t len, uint16_t color) {
    if (!m_buffer || y < 0 || y >= m_height) return;
    if (x < 0) { if ((uint16_t)(-x) >= len) return; len += x; x = 0; }
    if (x + len > m_width) len = m_width - x;
    if (len == 0) return;

    uint16_t* dst = m_buffer + (uint32_t)y * m_width + x;
    for (uint16_t i = 0; i < len; i++) dst[i] = color;
    markDirty({x, y, len, 1});
}

void Canvas::drawVLine(int16_t x, int16_t y, uint16_t len, uint16_t color) {
    if (!m_buffer || x < 0 || x >= m_width) return;
    if (y < 0) { if ((uint16_t)(-y) >= len) return; len += y; y = 0; }
    if (y + len > m_height) len = m_height - y;
    if (len == 0) return;

    for (uint16_t i = 0; i < len; i++) {
        m_buffer[(uint32_t)(y + i) * m_width + x] = color;
    }
    markDirty({x, y, 1, len});
}

void Canvas::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                       uint16_t color) {
    // Bresenham's line algorithm
    int16_t dx = abs(x1 - x0);
    int16_t dy = -abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx + dy;

    int16_t minX = x0, maxX = x0, minY = y0, maxY = y0;

    while (true) {
        if (x0 >= 0 && x0 < m_width && y0 >= 0 && y0 < m_height) {
            m_buffer[(uint32_t)y0 * m_width + x0] = color;
        }
        if (x0 < minX) minX = x0;
        if (x0 > maxX) maxX = x0;
        if (y0 < minY) minY = y0;
        if (y0 > maxY) maxY = y0;

        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    markDirty({minX, minY,
               (uint16_t)(maxX - minX + 1),
               (uint16_t)(maxY - minY + 1)});
}

void Canvas::fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
    if (!m_buffer || r <= 0) return;
    for (int16_t dy = -r; dy <= r; dy++) {
        int16_t py = cy + dy;
        if (py < 0 || py >= m_height) continue;
        // Compute horizontal span from circle equation
        int16_t dx = (int16_t)sqrtf((float)((int32_t)r * r - (int32_t)dy * dy));
        int16_t x0 = cx - dx;
        int16_t x1 = cx + dx;
        if (x0 < 0) x0 = 0;
        if (x1 >= m_width) x1 = m_width - 1;
        uint16_t* row_ptr = m_buffer + (uint32_t)py * m_width;
        for (int16_t px = x0; px <= x1; px++) {
            row_ptr[px] = color;
        }
    }
    markDirty({(int16_t)(cx - r), (int16_t)(cy - r),
               (uint16_t)(2 * r + 1), (uint16_t)(2 * r + 1)});
}

void Canvas::drawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
    if (!m_buffer || r <= 0) return;
    // Midpoint circle algorithm
    int16_t x = r, y = 0;
    int16_t err = 1 - r;

    auto plot = [&](int16_t px, int16_t py) {
        if (px >= 0 && px < m_width && py >= 0 && py < m_height)
            m_buffer[(uint32_t)py * m_width + px] = color;
    };

    while (x >= y) {
        plot(cx + x, cy + y); plot(cx - x, cy + y);
        plot(cx + x, cy - y); plot(cx - x, cy - y);
        plot(cx + y, cy + x); plot(cx - y, cy + x);
        plot(cx + y, cy - x); plot(cx - y, cy - x);
        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
    markDirty({(int16_t)(cx - r), (int16_t)(cy - r),
               (uint16_t)(2 * r + 1), (uint16_t)(2 * r + 1)});
}

void Canvas::fillRoundRect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                            int16_t r, uint16_t color) {
    if (!m_buffer) return;
    // Center rectangle
    fillRect(x + r, y, w - 2 * r, h, color);
    // Left/right columns
    fillRect(x, y + r, r, h - 2 * r, color);
    fillRect(x + w - r, y + r, r, h - 2 * r, color);
    // Four corner circles
    fillCircle(x + r,     y + r,     r, color);
    fillCircle(x + w - r - 1, y + r,     r, color);
    fillCircle(x + r,     y + h - r - 1, r, color);
    fillCircle(x + w - r - 1, y + h - r - 1, r, color);
}

void Canvas::drawText(int16_t x, int16_t y, const char* text,
                       uint16_t color, uint8_t scale) {
    if (!m_buffer || !text) return;
    int16_t startX = x;
    int16_t startY = y;
    const uint8_t charW = FONT_CHAR_W;
    const uint8_t charH = FONT_CHAR_H;
    const uint8_t spacing = FONT_SPACING;

    while (*text) {
        char c = *text++;
        if (c == '\n') {
            x = startX;
            y += (charH + spacing) * scale;
            continue;
        }
        if (c < 32 || c > 126) c = '?';

        const uint8_t* glyph = &FONT_5X7[(c - 32) * charW];

        for (uint8_t col = 0; col < charW; col++) {
            uint8_t colData = glyph[col];
            for (uint8_t row = 0; row < charH; row++) {
                if (colData & (1 << row)) {
                    if (scale == 1) {
                        int16_t px = x + col;
                        int16_t py = y + row;
                        if (px >= 0 && px < m_width && py >= 0 && py < m_height)
                            m_buffer[(uint32_t)py * m_width + px] = color;
                    } else {
                        fillRect(x + col * scale, y + row * scale,
                                 scale, scale, color);
                    }
                }
            }
        }
        x += (charW + spacing) * scale;
    }
    // Dirty rect covering all rendered text
    markDirty({startX, startY,
               (uint16_t)(x - startX),
               (uint16_t)(y - startY + (charH + spacing) * scale)});
}

uint16_t Canvas::textWidth(const char* text, uint8_t scale) const {
    if (!text) return 0;
    uint16_t w = 0;
    uint16_t maxW = 0;
    while (*text) {
        if (*text == '\n') {
            if (w > maxW) maxW = w;
            w = 0;
        } else {
            w += 6 * scale;  // 5px char + 1px spacing
        }
        text++;
    }
    if (w > maxW) maxW = w;
    return maxW > 0 ? maxW - scale : 0;  // Remove trailing space
}

void Canvas::drawBitmap1Bit(int16_t x, int16_t y,
                             const uint8_t* bmp, uint16_t bw, uint16_t bh,
                             uint16_t fg, int32_t bg) {
    if (!m_buffer || !bmp) return;
    uint16_t bytesPerRow = (bw + 7) / 8;

    for (uint16_t row = 0; row < bh; row++) {
        int16_t py = y + row;
        if (py < 0 || py >= m_height) continue;
        for (uint16_t col = 0; col < bw; col++) {
            int16_t px = x + col;
            if (px < 0 || px >= m_width) continue;

            uint8_t byte = bmp[row * bytesPerRow + col / 8];
            bool set = byte & (0x80 >> (col % 8));

            if (set) {
                m_buffer[(uint32_t)py * m_width + px] = fg;
            } else if (bg >= 0) {
                m_buffer[(uint32_t)py * m_width + px] = (uint16_t)bg;
            }
        }
    }
    Rect dr = Rect{x, y, bw, bh}.clipped({0, 0, m_width, m_height});
    markDirty(dr);
}

void Canvas::drawBitmap565(int16_t x, int16_t y,
                            const uint16_t* bmp, uint16_t bw, uint16_t bh,
                            int32_t transparentColor) {
    if (!m_buffer || !bmp) return;

    for (uint16_t row = 0; row < bh; row++) {
        int16_t py = y + row;
        if (py < 0 || py >= m_height) continue;
        for (uint16_t col = 0; col < bw; col++) {
            int16_t px = x + col;
            if (px < 0 || px >= m_width) continue;

            uint16_t pixel = bmp[row * bw + col];
            if (transparentColor >= 0 && pixel == (uint16_t)transparentColor)
                continue;
            m_buffer[(uint32_t)py * m_width + px] = pixel;
        }
    }
    markDirty({x, y, bw, bh});
}

void Canvas::blit(const Canvas& src, int16_t dx, int16_t dy,
                   int32_t transparentColor) {
    if (!src.valid()) return;
    drawBitmap565(dx, dy, src.buffer(), src.width(), src.height(),
                  transparentColor);
}

void Canvas::blitRegion(const Canvas& src, const Rect& srcRegion,
                         int16_t dx, int16_t dy,
                         int32_t transparentColor) {
    if (!m_buffer || !src.valid()) return;

    for (uint16_t row = 0; row < srcRegion.h; row++) {
        int16_t sy = srcRegion.y + row;
        int16_t py = dy + row;
        if (sy < 0 || sy >= src.height() || py < 0 || py >= m_height) continue;

        const uint16_t* srcRow = src.buffer() + (uint32_t)sy * src.width();
        uint16_t* dstRow = m_buffer + (uint32_t)py * m_width;

        for (uint16_t col = 0; col < srcRegion.w; col++) {
            int16_t sx = srcRegion.x + col;
            int16_t px = dx + col;
            if (sx < 0 || sx >= src.width() || px < 0 || px >= m_width) continue;

            uint16_t pixel = srcRow[sx];
            if (transparentColor >= 0 && pixel == (uint16_t)transparentColor)
                continue;
            dstRow[px] = pixel;
        }
    }
    markDirty({dx, dy, srcRegion.w, srcRegion.h});
}

void Canvas::pushToScreen(int16_t screenX, int16_t screenY, bool dirtyOnly) {
    if (!m_buffer) return;

    if (dirtyOnly && m_dirty && !m_dirtyRect.empty()) {
        // Only push the dirty region
        Rect dr = m_dirtyRect.clipped({0, 0, m_width, m_height});
        if (dr.empty()) { clearDirty(); return; }

        // Build a temporary contiguous buffer for the dirty region
        // (HAL::pushRegion expects contiguous row-major data)
        uint32_t regionSize = (uint32_t)dr.w * dr.h;
        uint16_t* tmpBuf = (uint16_t*)malloc(regionSize * 2);
        if (tmpBuf) {
            for (uint16_t r = 0; r < dr.h; r++) {
                memcpy(tmpBuf + (uint32_t)r * dr.w,
                       m_buffer + (uint32_t)(dr.y + r) * m_width + dr.x,
                       dr.w * 2);
            }
            HAL::pushRegion(screenX + dr.x, screenY + dr.y,
                            dr.w, dr.h, tmpBuf);
            free(tmpBuf);
        }
    } else if (!dirtyOnly) {
        // Push the entire canvas
        HAL::pushRegion(screenX, screenY, m_width, m_height, m_buffer);
    }
    clearDirty();
}

// ── Canvas Pool ──────────────────────────────────────────────────

CanvasPool& CanvasPool::instance() {
    static CanvasPool pool;
    return pool;
}

Canvas* CanvasPool::acquire(uint16_t w, uint16_t h, bool psram) {
    for (uint8_t i = 0; i < MAX_CANVASES; i++) {
        if (!m_slots[i].inUse) {
            if (m_slots[i].canvas.create(w, h, psram)) {
                m_slots[i].inUse = true;
                m_activeCount++;
                return &m_slots[i].canvas;
            }
            return nullptr;  // Allocation failed
        }
    }
    return nullptr;  // Pool exhausted
}

void CanvasPool::release(Canvas* canvas) {
    if (!canvas) return;
    for (uint8_t i = 0; i < MAX_CANVASES; i++) {
        if (&m_slots[i].canvas == canvas && m_slots[i].inUse) {
            m_slots[i].canvas.destroy();
            m_slots[i].inUse = false;
            m_activeCount--;
            return;
        }
    }
}

} // namespace CardGFX
