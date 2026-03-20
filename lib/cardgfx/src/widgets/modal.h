#ifndef CARDGFX_WIDGET_MODAL_H
#define CARDGFX_WIDGET_MODAL_H

#include "../cardgfx_widget.h"
#include <cstring>
#include <functional>

namespace CardGFX {

/**
 * Modal: a centered overlay dialog with title, message, and up to 8 buttons.
 *
 * Designed to be pushed as a scene overlay. The Modal handles its own
 * focus between buttons and calls the appropriate callback on selection.
 *
 * Usage:
 *   Modal dialog;
 *   dialog.setTitle("Draw Offer");
 *   dialog.setMessage("Opponent offers a draw.");
 *   dialog.addButton("Accept", [](){ ... });
 *   dialog.addButton("Decline", [](){ ... });
 */
class Modal : public Widget {
public:
    static constexpr uint8_t MAX_BUTTONS    = 8;
    static constexpr uint8_t MAX_TITLE_LEN  = 24;
    static constexpr uint8_t MAX_MSG_LEN    = 80;
    static constexpr uint8_t MAX_BTN_LEN    = 12;

    using ButtonCallback = std::function<void()>;

    Modal() {
        m_focusable = true;
        m_visible = false;  // Hidden by default
    }

    // ── Content ──────────────────────────────────────────────────

    void setTitle(const char* title) {
        strncpy(m_title, title, MAX_TITLE_LEN - 1);
        m_title[MAX_TITLE_LEN - 1] = '\0';
        markDirty();
    }

    void setMessage(const char* msg) {
        strncpy(m_message, msg, MAX_MSG_LEN - 1);
        m_message[MAX_MSG_LEN - 1] = '\0';
        markDirty();
    }

    void setEscapeCallback(ButtonCallback cb) {
        m_escapeCallback = cb;
        m_hasEscapeCallback = (cb != nullptr);
    }

    bool addButton(const char* label, ButtonCallback cb) {
        if (m_buttonCount >= MAX_BUTTONS) return false;
        strncpy(m_buttons[m_buttonCount].label, label, MAX_BTN_LEN - 1);
        m_buttons[m_buttonCount].label[MAX_BTN_LEN - 1] = '\0';
        m_buttons[m_buttonCount].callback = cb;
        m_buttonCount++;
        markDirty();
        return true;
    }

    void clearButtons() {
        m_buttonCount = 0;
        m_selectedButton = 0;
        m_escapeCallback = nullptr;
        m_hasEscapeCallback = false;
        markDirty();
    }

    /** Show the modal and reset selection. */
    void show() {
        m_visible = true;
        m_selectedButton = 0;
        markDirty();
    }

    /** Hide the modal. */
    void hide() {
        m_visible = false;
        markDirty();
    }

    // ── Lifecycle ────────────────────────────────────────────────

    void onDraw(Canvas& canvas, const Theme& theme) override {
        // Semi-transparent overlay (darken background)
        canvas.fill(theme.bgPrimary);

        if (m_bounds.w < 20 || m_bounds.h < 14) return;

        uint8_t scale = theme.fontScaleMd;
        uint16_t lineH = FONT_CHAR_H * scale;

        // Compute content height to auto-size dialog
        uint16_t contentH = 6; // top + bottom padding (3 each)
        if (m_title[0])   contentH += lineH + 4; // title + gap + divider
        if (m_message[0]) contentH += lineH + 2; // message + gap
        if (m_buttonCount > 0) {
            uint16_t btnH = lineH + 4;
            uint16_t btnGap = 2;
            contentH += m_buttonCount * btnH + (m_buttonCount - 1) * btnGap;
        }

        uint16_t dw = m_bounds.w - 16;
        uint16_t maxDh = m_bounds.h - 10;
        uint16_t dh = contentH < maxDh ? contentH : maxDh;
        int16_t  dx = 8;
        int16_t  dy = (m_bounds.h - dh) / 2; // Center vertically

        // Shadow
        canvas.fillRect(dx + 2, dy + 2, dw, dh, theme.border);
        // Box
        canvas.fillRect(dx, dy, dw, dh, theme.bgSecondary);
        canvas.drawRect(dx, dy, dw, dh, theme.borderFocus);

        int16_t textX = dx + 4;
        int16_t textY = dy + 3;

        // Title
        if (m_title[0]) {
            canvas.drawText(textX, textY, m_title, theme.accent, scale);
            textY += lineH + 4;
            canvas.drawHLine(dx + 2, textY - 2, dw - 4, theme.divider);
        }

        // Message
        if (m_message[0]) {
            canvas.drawText(textX, textY, m_message, theme.fgPrimary, scale);
            textY += lineH + 2;
        }

        // Buttons (vertical layout)
        if (m_buttonCount > 0) {
            uint16_t btnH = lineH + 4;
            uint16_t btnGap = 2;
            uint16_t btnW = dw - 16;
            int16_t btnX = dx + 8;
            int16_t dialogBottom = dy + dh;

            for (uint8_t i = 0; i < m_buttonCount; i++) {
                int16_t btnY = textY + i * (btnH + btnGap);
                if (btnY + btnH > dialogBottom) break; // Overflow guard

                if (i == m_selectedButton) {
                    canvas.fillRect(btnX, btnY, btnW, btnH, theme.accent);
                    int16_t tw = canvas.textWidth(m_buttons[i].label, scale);
                    canvas.drawText(btnX + (btnW - tw) / 2, btnY + 2,
                                    m_buttons[i].label,
                                    theme.bgPrimary, scale);
                } else {
                    canvas.drawRect(btnX, btnY, btnW, btnH, theme.border);
                    int16_t tw = canvas.textWidth(m_buttons[i].label, scale);
                    canvas.drawText(btnX + (btnW - tw) / 2, btnY + 2,
                                    m_buttons[i].label,
                                    theme.fgPrimary, scale);
                }
            }
        }
    }

    bool onInput(const InputEvent& event) override {
        if (!event.isDown() && !event.isRepeat()) return false;

        switch (event.key) {
        case Key::LEFT:
        case Key::UP:
            if (m_selectedButton > 0) { m_selectedButton--; markDirty(); }
            return true;
        case Key::RIGHT:
        case Key::DOWN:
            if (m_selectedButton < m_buttonCount - 1) {
                m_selectedButton++; markDirty();
            }
            return true;
        case Key::ENTER:
        case Key::SPACE:
            if (m_selectedButton < m_buttonCount &&
                m_buttons[m_selectedButton].callback) {
                m_buttons[m_selectedButton].callback();
            }
            return true;
        case Key::ESCAPE:
            if (m_hasEscapeCallback) {
                m_escapeCallback();
            } else if (m_buttonCount > 0 &&
                       m_buttons[m_buttonCount - 1].callback) {
                m_buttons[m_buttonCount - 1].callback();
            }
            return true;
        default:
            return true;  // Modal consumes all input
        }
    }

private:
    struct Button {
        char label[MAX_BTN_LEN] = {};
        ButtonCallback callback = nullptr;
    };

    char    m_title[MAX_TITLE_LEN] = {};
    char    m_message[MAX_MSG_LEN] = {};
    Button  m_buttons[MAX_BUTTONS] = {};
    uint8_t m_buttonCount = 0;
    uint8_t m_selectedButton = 0;
    ButtonCallback m_escapeCallback = nullptr;
    bool m_hasEscapeCallback = false;
};

} // namespace CardGFX

#endif // CARDGFX_WIDGET_MODAL_H
