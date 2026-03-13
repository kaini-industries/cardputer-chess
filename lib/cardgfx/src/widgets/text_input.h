#ifndef CARDGFX_WIDGET_TEXT_INPUT_H
#define CARDGFX_WIDGET_TEXT_INPUT_H

#include "../cardgfx_widget.h"
#include <cstring>
#include <functional>

namespace CardGFX {

/**
 * TextInput: single-line text entry with cursor.
 *
 * Handles printable character input, backspace, cursor movement,
 * and emits an onSubmit callback when Enter is pressed.
 */
class TextInput : public Widget {
public:
    using SubmitCallback = std::function<void(const char* text)>;

    TextInput() {
        m_focusable = true;
        m_acceptsText = true;
        m_buffer[0] = '\0';
    }

    // ── Content ──────────────────────────────────────────────────

    void setText(const char* text) {
        if (!text) { m_buffer[0] = '\0'; m_cursor = 0; }
        else {
            strncpy(m_buffer, text, MAX_LEN - 1);
            m_buffer[MAX_LEN - 1] = '\0';
            m_cursor = strlen(m_buffer);
        }
        markDirty();
    }

    const char* text() const { return m_buffer; }
    uint8_t length() const { return (uint8_t)strlen(m_buffer); }

    void clear() { m_buffer[0] = '\0'; m_cursor = 0; markDirty(); }

    // ── Configuration ────────────────────────────────────────────

    void setMaxLength(uint8_t max) { m_maxLen = (max < MAX_LEN) ? max : MAX_LEN - 1; }
    void setPlaceholder(const char* ph) {
        strncpy(m_placeholder, ph, MAX_LEN - 1);
        m_placeholder[MAX_LEN - 1] = '\0';
    }
    void setOnSubmit(SubmitCallback cb) { m_onSubmit = cb; }
    void setUppercase(bool uc) { m_uppercase = uc; }

    // ── Lifecycle ────────────────────────────────────────────────

    void onDraw(Canvas& canvas, const Theme& theme) override {
        // Background
        uint16_t bg = m_focused ? theme.bgTertiary : theme.bgSecondary;
        canvas.fill(bg);

        // Border
        uint16_t borderColor = m_focused ? theme.borderFocus : theme.border;
        canvas.drawRect(0, 0, m_bounds.w, m_bounds.h, borderColor);

        uint8_t scale = theme.fontScaleMd;
        int16_t textY = ((int16_t)m_bounds.h - FONT_CHAR_H * scale) / 2;
        int16_t textX = 2;

        bool empty = (m_buffer[0] == '\0');

        if (empty && !m_focused && m_placeholder[0] != '\0') {
            // Draw placeholder
            canvas.drawText(textX, textY, m_placeholder,
                            theme.fgDisabled, scale);
        } else {
            // Draw text
            canvas.drawText(textX, textY, m_buffer,
                            theme.fgPrimary, scale);

            // Draw cursor (blinking)
            if (m_focused && m_cursorVisible) {
                uint16_t cursorX = textX;
                // Measure width up to cursor position
                char tmp[MAX_LEN];
                strncpy(tmp, m_buffer, m_cursor);
                tmp[m_cursor] = '\0';
                cursorX += canvas.textWidth(tmp, scale);

                canvas.drawVLine(cursorX, textY,
                                 7 * scale, theme.accent);
            }
        }
    }

    bool onInput(const InputEvent& event) override {
        if (!event.isDown() && !event.isRepeat()) return false;

        uint8_t len = length();

        // Printable character
        if (event.isPrintable() && len < m_maxLen) {
            char c = event.character;
            if (m_uppercase && c >= 'a' && c <= 'z') c -= 32;

            // Insert at cursor
            memmove(&m_buffer[m_cursor + 1], &m_buffer[m_cursor],
                    len - m_cursor + 1);
            m_buffer[m_cursor] = c;
            m_cursor++;
            markDirty();
            return true;
        }

        switch (event.key) {
        case Key::BACKSPACE:
            if (m_cursor > 0) {
                memmove(&m_buffer[m_cursor - 1], &m_buffer[m_cursor],
                        len - m_cursor + 1);
                m_cursor--;
                markDirty();
            }
            return true;

        case Key::DEL:
            if (m_cursor < len) {
                memmove(&m_buffer[m_cursor], &m_buffer[m_cursor + 1],
                        len - m_cursor);
                markDirty();
            }
            return true;

        case Key::LEFT:
            if (m_cursor > 0) { m_cursor--; markDirty(); }
            return true;

        case Key::RIGHT:
            if (m_cursor < len) { m_cursor++; markDirty(); }
            return true;

        case Key::HOME:
            m_cursor = 0; markDirty();
            return true;

        case Key::ENTER:
            if (m_onSubmit) m_onSubmit(m_buffer);
            return true;

        default:
            return false;
        }
    }

    void onTick(uint32_t dt_ms) override {
        if (!m_focused) return;
        m_blinkTimer += dt_ms;
        if (m_blinkTimer >= 500) {
            m_blinkTimer = 0;
            m_cursorVisible = !m_cursorVisible;
            markDirty();
        }
    }

    void onFocus(bool gained) override {
        if (gained) {
            m_cursorVisible = true;
            m_blinkTimer = 0;
        }
        markDirty();
    }

private:
    static constexpr uint8_t MAX_LEN = 48;

    char     m_buffer[MAX_LEN] = {};
    char     m_placeholder[MAX_LEN] = {};
    uint8_t  m_cursor = 0;
    uint8_t  m_maxLen = MAX_LEN - 1;
    bool     m_uppercase = false;

    // Cursor blink
    bool     m_cursorVisible = true;
    uint32_t m_blinkTimer = 0;

    SubmitCallback m_onSubmit = nullptr;
};

} // namespace CardGFX

#endif // CARDGFX_WIDGET_TEXT_INPUT_H
