#ifndef CARDGFX_WIDGET_STATUS_BAR_H
#define CARDGFX_WIDGET_STATUS_BAR_H

#include "../cardgfx_widget.h"
#include <cstring>

namespace CardGFX {

/**
 * StatusBar: a slim horizontal bar with text segments.
 *
 * Divided into left, center, and right sections.
 * Perfect for turn indicators, clocks, connection status.
 *
 * Usage:
 *   StatusBar bar;
 *   bar.setBounds({0, 0, 240, 12});
 *   bar.setLeft("White");
 *   bar.setCenter("Move 12");
 *   bar.setRight("Connected");
 */
class StatusBar : public Widget {
public:
    static constexpr uint8_t MAX_SEG_LEN = 24;

    StatusBar() { m_focusable = false; }

    void setLeft(const char* text) {
        strncpy(m_left, text ? text : "", MAX_SEG_LEN - 1);
        m_left[MAX_SEG_LEN - 1] = '\0';
        markDirty();
    }

    void setCenter(const char* text) {
        strncpy(m_center, text ? text : "", MAX_SEG_LEN - 1);
        m_center[MAX_SEG_LEN - 1] = '\0';
        markDirty();
    }

    void setRight(const char* text) {
        strncpy(m_right, text ? text : "", MAX_SEG_LEN - 1);
        m_right[MAX_SEG_LEN - 1] = '\0';
        markDirty();
    }

    void setBgColor(uint16_t c)    { m_bgColor = c; m_customBg = true; markDirty(); }
    void setTextColor(uint16_t c)  { m_textColor = c; m_customFg = true; markDirty(); }
    void setDrawSeparator(bool d)  { m_drawSep = d; markDirty(); }

    void onDraw(Canvas& canvas, const Theme& theme) override {
        uint16_t bg = m_customBg ? m_bgColor : theme.bgSecondary;
        uint16_t fg = m_customFg ? m_textColor : theme.fgPrimary;

        canvas.fill(bg);

        uint8_t scale = theme.fontScaleSm;
        int16_t textY = ((int16_t)m_bounds.h - FONT_CHAR_H * scale) / 2;
        if (textY < 0) textY = 0;

        // Left
        if (m_left[0]) {
            canvas.drawText(2, textY, m_left, fg, scale);
        }

        // Center
        if (m_center[0]) {
            uint16_t tw = canvas.textWidth(m_center, scale);
            int16_t cx = ((int16_t)m_bounds.w - (int16_t)tw) / 2;
            canvas.drawText(cx, textY, m_center, fg, scale);
        }

        // Right
        if (m_right[0]) {
            uint16_t tw = canvas.textWidth(m_right, scale);
            int16_t rx = (int16_t)m_bounds.w - (int16_t)tw - 2;
            canvas.drawText(rx, textY, m_right, fg, scale);
        }

        // Bottom separator line
        if (m_drawSep) {
            canvas.drawHLine(0, m_bounds.h - 1, m_bounds.w, theme.divider);
        }
    }

private:
    char     m_left[MAX_SEG_LEN]   = {};
    char     m_center[MAX_SEG_LEN] = {};
    char     m_right[MAX_SEG_LEN]  = {};
    uint16_t m_bgColor = 0;
    uint16_t m_textColor = 0;
    bool     m_customBg = false;
    bool     m_customFg = false;
    bool     m_drawSep = true;
};

} // namespace CardGFX

#endif // CARDGFX_WIDGET_STATUS_BAR_H
