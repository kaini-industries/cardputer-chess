#ifndef CARDGFX_WIDGET_BITMAP_H
#define CARDGFX_WIDGET_BITMAP_H

#include "../cardgfx_widget.h"

namespace CardGFX {

/**
 * Bitmap: displays a static bitmap image.
 *
 * Supports both 1-bit (monochrome) and RGB565 bitmaps.
 * Useful for icons, piece sprites, logos.
 */
class Bitmap : public Widget {
public:
    enum class Format : uint8_t {
        Mono,    // 1-bit, packed MSB-first
        RGB565   // 16-bit color
    };

    Bitmap() { m_focusable = false; }

    // ── 1-bit bitmap ─────────────────────────────────────────────

    void setMono(const uint8_t* data, uint16_t w, uint16_t h,
                 uint16_t fgColor, int32_t bgColor = -1) {
        m_monoData = data;
        m_rgbData = nullptr;
        m_imgW = w;
        m_imgH = h;
        m_fgColor = fgColor;
        m_bgColor = bgColor;
        m_format = Format::Mono;
        markDirty();
    }

    // ── RGB565 bitmap ────────────────────────────────────────────

    void setRGB565(const uint16_t* data, uint16_t w, uint16_t h,
                   int32_t transparentColor = -1) {
        m_rgbData = data;
        m_monoData = nullptr;
        m_imgW = w;
        m_imgH = h;
        m_transparentColor = transparentColor;
        m_format = Format::RGB565;
        markDirty();
    }

    // ── Configuration ────────────────────────────────────────────

    void setScale(uint8_t s) { m_scale = s; markDirty(); }
    void setCentered(bool c) { m_centered = c; markDirty(); }

    // ── Lifecycle ────────────────────────────────────────────────

    void onDraw(Canvas& canvas, const Theme& theme) override {
        canvas.fill(theme.bgPrimary);

        int16_t dx = 0, dy = 0;
        uint16_t displayW = m_imgW * m_scale;
        uint16_t displayH = m_imgH * m_scale;

        if (m_centered) {
            dx = ((int16_t)m_bounds.w - (int16_t)displayW) / 2;
            dy = ((int16_t)m_bounds.h - (int16_t)displayH) / 2;
        }

        if (m_format == Format::Mono && m_monoData) {
            if (m_scale == 1) {
                canvas.drawBitmap1Bit(dx, dy, m_monoData, m_imgW, m_imgH,
                                      m_fgColor, m_bgColor);
            } else {
                // Scaled mono bitmap
                uint16_t bytesPerRow = (m_imgW + 7) / 8;
                for (uint16_t row = 0; row < m_imgH; row++) {
                    for (uint16_t col = 0; col < m_imgW; col++) {
                        uint8_t byte = m_monoData[row * bytesPerRow + col / 8];
                        bool set = byte & (0x80 >> (col % 8));
                        if (set) {
                            canvas.fillRect(dx + col * m_scale,
                                            dy + row * m_scale,
                                            m_scale, m_scale, m_fgColor);
                        } else if (m_bgColor >= 0) {
                            canvas.fillRect(dx + col * m_scale,
                                            dy + row * m_scale,
                                            m_scale, m_scale,
                                            (uint16_t)m_bgColor);
                        }
                    }
                }
            }
        } else if (m_format == Format::RGB565 && m_rgbData) {
            if (m_scale == 1) {
                canvas.drawBitmap565(dx, dy, m_rgbData, m_imgW, m_imgH,
                                     m_transparentColor);
            } else {
                // Scaled RGB565 bitmap
                for (uint16_t row = 0; row < m_imgH; row++) {
                    for (uint16_t col = 0; col < m_imgW; col++) {
                        uint16_t pixel = m_rgbData[row * m_imgW + col];
                        if (m_transparentColor >= 0 &&
                            pixel == (uint16_t)m_transparentColor) continue;
                        canvas.fillRect(dx + col * m_scale,
                                        dy + row * m_scale,
                                        m_scale, m_scale, pixel);
                    }
                }
            }
        }
    }

private:
    const uint8_t*  m_monoData = nullptr;
    const uint16_t* m_rgbData  = nullptr;
    uint16_t m_imgW = 0;
    uint16_t m_imgH = 0;
    uint16_t m_fgColor = 0xFFFF;
    int32_t  m_bgColor = -1;   // -1 = transparent
    int32_t  m_transparentColor = -1;
    uint8_t  m_scale = 1;
    bool     m_centered = true;
    Format   m_format = Format::Mono;
};

} // namespace CardGFX

#endif // CARDGFX_WIDGET_BITMAP_H
