#ifndef CARDGFX_CANVAS_H
#define CARDGFX_CANVAS_H

#include "cardgfx_config.h"
#include <cstdint>
#include <cstring>

namespace CardGFX {

// ── Rect ─────────────────────────────────────────────────────────

struct Rect {
    int16_t x = 0;
    int16_t y = 0;
    uint16_t w = 0;
    uint16_t h = 0;

    bool empty() const { return w == 0 || h == 0; }

    bool contains(int16_t px, int16_t py) const {
        return px >= x && px < x + (int16_t)w &&
               py >= y && py < y + (int16_t)h;
    }

    bool intersects(const Rect& o) const {
        return !(x + (int16_t)w <= o.x || o.x + (int16_t)o.w <= x ||
                 y + (int16_t)h <= o.y || o.y + (int16_t)o.h <= y);
    }

    /** Expand this rect to include another rect. */
    void merge(const Rect& o) {
        if (o.empty()) return;
        if (empty()) { *this = o; return; }
        int16_t x2 = x + (int16_t)w;
        int16_t y2 = y + (int16_t)h;
        int16_t ox2 = o.x + (int16_t)o.w;
        int16_t oy2 = o.y + (int16_t)o.h;
        x = (x < o.x) ? x : o.x;
        y = (y < o.y) ? y : o.y;
        x2 = (x2 > ox2) ? x2 : ox2;
        y2 = (y2 > oy2) ? y2 : oy2;
        w = (uint16_t)(x2 - x);
        h = (uint16_t)(y2 - y);
    }

    /** Compute the intersection of two rects. */
    Rect clipped(const Rect& bounds) const {
        int16_t cx = (x > bounds.x) ? x : bounds.x;
        int16_t cy = (y > bounds.y) ? y : bounds.y;
        int16_t cx2 = (x + (int16_t)w < bounds.x + (int16_t)bounds.w)
                        ? x + (int16_t)w
                        : bounds.x + (int16_t)bounds.w;
        int16_t cy2 = (y + (int16_t)h < bounds.y + (int16_t)bounds.h)
                        ? y + (int16_t)h
                        : bounds.y + (int16_t)bounds.h;
        if (cx2 <= cx || cy2 <= cy) return {0, 0, 0, 0};
        return {cx, cy, (uint16_t)(cx2 - cx), (uint16_t)(cy2 - cy)};
    }
};

// ── Canvas ───────────────────────────────────────────────────────

/**
 * A drawable surface backed by an RGB565 pixel buffer.
 *
 * Canvases are the fundamental drawing unit. Widgets draw into
 * canvases, and canvases blit onto each other or directly to screen.
 *
 * Every draw operation expands the dirty rect. When compositing,
 * only the dirty region needs to be pushed.
 */
class Canvas {
public:
    Canvas();
    ~Canvas();

    // No copy — canvases own their buffers
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    // Move is OK
    Canvas(Canvas&& other) noexcept;
    Canvas& operator=(Canvas&& other) noexcept;

    /**
     * Allocate the pixel buffer.
     * @param w       Width in pixels.
     * @param h       Height in pixels.
     * @param psram   If true, allocate in PSRAM (for large buffers).
     * @return true on success.
     */
    bool create(uint16_t w, uint16_t h, bool psram = false);

    /** Release the pixel buffer. */
    void destroy();

    /** Is this canvas allocated and ready to draw? */
    bool valid() const { return m_buffer != nullptr; }

    // ── Dimensions ───────────────────────────────────────────────

    uint16_t width() const  { return m_width; }
    uint16_t height() const { return m_height; }

    // ── Dirty Tracking ───────────────────────────────────────────

    bool isDirty() const        { return m_dirty; }
    const Rect& dirtyRect() const { return m_dirtyRect; }
    void markDirty(const Rect& r);
    void markAllDirty();
    void clearDirty();

    // ── Direct Buffer Access ─────────────────────────────────────

    uint16_t* buffer()             { return m_buffer; }
    const uint16_t* buffer() const { return m_buffer; }

    /** Get a pointer to the start of a specific row. */
    uint16_t* row(uint16_t y) {
        return m_buffer + (uint32_t)y * m_width;
    }

    /** Get/set a single pixel (no bounds check — be careful). */
    uint16_t getPixel(int16_t x, int16_t y) const {
        return m_buffer[(uint32_t)y * m_width + x];
    }
    void setPixel(int16_t x, int16_t y, uint16_t color) {
        if (x < 0 || y < 0 || x >= m_width || y >= m_height) return;
        m_buffer[(uint32_t)y * m_width + x] = color;
        markDirty({x, y, 1, 1});
    }

    // ── Drawing Primitives ───────────────────────────────────────

    /** Fill the entire canvas with a color. */
    void fill(uint16_t color);

    /** Fill a rectangle. */
    void fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                  uint16_t color);

    /** Draw a 1px rectangle outline. */
    void drawRect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                  uint16_t color);

    /** Draw a horizontal line. */
    void drawHLine(int16_t x, int16_t y, uint16_t len, uint16_t color);

    /** Draw a vertical line. */
    void drawVLine(int16_t x, int16_t y, uint16_t len, uint16_t color);

    /** Draw a line between two points (Bresenham). */
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                  uint16_t color);

    /** Draw a filled circle. */
    void fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color);

    /** Draw a circle outline. */
    void drawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color);

    /** Draw a filled rounded rectangle. */
    void fillRoundRect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                       int16_t r, uint16_t color);

    /**
     * Draw a text string.
     * Uses a simple built-in 5x7 font. For fancier text, use the
     * HAL driver's text engine via Canvas::drawTextEx().
     *
     * @param x, y   Top-left position.
     * @param text    Null-terminated string.
     * @param color   Text color (RGB565).
     * @param scale   Pixel scale multiplier (1 = 5x7, 2 = 10x14, etc.)
     */
    void drawText(int16_t x, int16_t y, const char* text,
                  uint16_t color, uint8_t scale = 1);

    /**
     * Measure text width in pixels for the built-in font.
     */
    uint16_t textWidth(const char* text, uint8_t scale = 1) const;

    /**
     * Blit a 1-bit bitmap (monochrome icon/glyph).
     * Each byte holds 8 horizontal pixels, MSB first.
     *
     * @param x, y    Destination position.
     * @param bmp     Bitmap data.
     * @param bw, bh  Bitmap dimensions in pixels.
     * @param fg      Foreground color (set bits).
     * @param bg      Background color (clear bits), or -1 for transparent.
     */
    void drawBitmap1Bit(int16_t x, int16_t y,
                        const uint8_t* bmp, uint16_t bw, uint16_t bh,
                        uint16_t fg, int32_t bg = -1);

    /**
     * Blit an RGB565 bitmap.
     *
     * @param x, y    Destination position.
     * @param bmp     RGB565 pixel data (row-major).
     * @param bw, bh  Bitmap dimensions.
     * @param transparentColor  Color key for transparency, or -1 for none.
     */
    void drawBitmap565(int16_t x, int16_t y,
                       const uint16_t* bmp, uint16_t bw, uint16_t bh,
                       int32_t transparentColor = -1);

    /**
     * Blit another canvas onto this one.
     *
     * @param src              Source canvas.
     * @param dx, dy           Destination position on this canvas.
     * @param transparentColor Color key for transparency, or -1.
     */
    void blit(const Canvas& src, int16_t dx, int16_t dy,
              int32_t transparentColor = -1);

    /**
     * Blit a sub-region of another canvas onto this one.
     */
    void blitRegion(const Canvas& src, const Rect& srcRegion,
                    int16_t dx, int16_t dy,
                    int32_t transparentColor = -1);

    /**
     * Push this canvas (or its dirty region) to the physical screen.
     * @param screenX, screenY  Where on screen to place the top-left.
     * @param dirtyOnly         If true, only push the dirty region.
     */
    void pushToScreen(int16_t screenX, int16_t screenY,
                      bool dirtyOnly = true);

private:
    uint16_t* m_buffer   = nullptr;
    uint16_t  m_width    = 0;
    uint16_t  m_height   = 0;
    bool      m_psram    = false;
    bool      m_dirty    = false;
    Rect      m_dirtyRect = {};

    /** Clamp a draw rect to canvas bounds, return false if fully clipped. */
    bool clipRect(int16_t& x, int16_t& y, uint16_t& w, uint16_t& h) const;
};

// ── Canvas Pool ──────────────────────────────────────────────────

/**
 * Pre-allocated pool of canvases.
 * Avoids heap fragmentation from repeated alloc/free of sprite buffers.
 */
class CanvasPool {
public:
    /**
     * Acquire a canvas from the pool.
     * @param w, h   Desired dimensions.
     * @param psram  Allocate buffer in PSRAM?
     * @return Pointer to a canvas, or nullptr if the pool is full.
     */
    Canvas* acquire(uint16_t w, uint16_t h, bool psram = false);

    /**
     * Release a canvas back to the pool.
     * The canvas buffer is freed (or can be kept for reuse).
     */
    void release(Canvas* canvas);

    /** Number of canvases currently in use. */
    uint8_t activeCount() const { return m_activeCount; }

    /** Get the singleton pool instance. */
    static CanvasPool& instance();

private:
    CanvasPool() = default;

    struct Slot {
        Canvas canvas;
        bool   inUse = false;
    };

    Slot    m_slots[MAX_CANVASES] = {};
    uint8_t m_activeCount = 0;
};

} // namespace CardGFX

#endif // CARDGFX_CANVAS_H
