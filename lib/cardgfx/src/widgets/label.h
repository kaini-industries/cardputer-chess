#ifndef CARDGFX_WIDGET_LABEL_H
#define CARDGFX_WIDGET_LABEL_H

#include "../cardgfx_widget.h"
#include <cstring>

namespace CardGFX {

/**
 * Label: displays static or dynamic text.
 *
 * Usage:
 *   Label lbl;
 *   lbl.setText("Hello");
 *   lbl.setAlign(Label::Align::Center);
 *   lbl.setBounds({10, 10, 100, 14});
 */
class Label : public Widget {
public:
    enum class Align : uint8_t { Left, Center, Right };

    Label() { m_focusable = false; }

    void setText(const char* text) {
        if (!text) { m_text[0] = '\0'; }
        else { strncpy(m_text, text, MAX_TEXT - 1); m_text[MAX_TEXT - 1] = '\0'; }
        markDirty();
    }

    const char* text() const { return m_text; }

    void setAlign(Align a)         { m_align = a; markDirty(); }
    void setScale(uint8_t s)       { m_scale = s; markDirty(); }
    void setColor(uint16_t c)      { m_color = c; m_useCustomColor = true; markDirty(); }
    void setBgColor(uint16_t c)    { m_bgColor = c; m_useCustomBg = true; markDirty(); }
    void setDrawBg(bool draw)      { m_drawBg = draw; markDirty(); }
    void clearCustomColor()        { m_useCustomColor = false; markDirty(); }

    void onDraw(Canvas& canvas, const Theme& theme) override {
        uint16_t fg = m_useCustomColor ? m_color : theme.fgPrimary;
        uint16_t bg = m_useCustomBg ? m_bgColor : theme.bgPrimary;

        if (m_drawBg) {
            canvas.fill(bg);
        }

        uint8_t scale = m_scale > 0 ? m_scale : theme.fontScaleMd;
        uint16_t tw = canvas.textWidth(m_text, scale);
        int16_t x = 0;

        switch (m_align) {
        case Align::Left:   x = 1; break;
        case Align::Center: x = ((int16_t)m_bounds.w - (int16_t)tw) / 2; break;
        case Align::Right:  x = (int16_t)m_bounds.w - (int16_t)tw - 1; break;
        }

        // Vertically center text
        int16_t y = ((int16_t)m_bounds.h - FONT_CHAR_H * scale) / 2;
        if (y < 0) y = 0;

        canvas.drawText(x, y, m_text, fg, scale);
    }

private:
    static constexpr uint16_t MAX_TEXT = 64;
    char     m_text[MAX_TEXT] = {};
    Align    m_align = Align::Left;
    uint8_t  m_scale = 0;  // 0 = use theme default
    uint16_t m_color = 0;
    uint16_t m_bgColor = 0;
    bool     m_useCustomColor = false;
    bool     m_useCustomBg = false;
    bool     m_drawBg = true;
};

} // namespace CardGFX

#endif // CARDGFX_WIDGET_LABEL_H
