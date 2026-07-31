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
    static constexpr uint8_t MAX_BTN_LEN    = 24;

    using ButtonCallback = std::function<void()>;
    using InputCallback = std::function<bool(const InputEvent&)>;

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

    // Optional modal-specific shortcuts (for example, a documented scene
    // hotkey). Return true to consume the event before normal button handling.
    void setInputCallback(InputCallback cb) { m_inputCallback = cb; }

    /**
     * Configure whether Space activates the selected button.
     * Enabled by default for backwards compatibility.
     */
    void setSpaceActivates(bool enabled) { m_spaceActivates = enabled; }

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
        m_inputCallback = nullptr;
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

        uint8_t scale = theme.fontScaleMd > 0 ? theme.fontScaleMd : 1;
        uint16_t lineH = FONT_CHAR_H * scale;

        uint16_t dw = m_bounds.w - 16;
        uint16_t maxDh = m_bounds.h - 10;
        const uint16_t lineAdvance = (FONT_CHAR_H + FONT_SPACING) * scale;
        const uint16_t textAreaW = dw > 8 ? dw - 8 : 1;
        const uint16_t charAdvance =
            (FONT_CHAR_W + FONT_SPACING) * scale;
        uint16_t charsPerLineWide = textAreaW / charAdvance;
        if (charsPerLineWide == 0) charsPerLineWide = 1;
        if (charsPerLineWide > MAX_MSG_LEN - 1) {
            charsPerLineWide = MAX_MSG_LEN - 1;
        }

        // Lay out explicit newlines and long lines without allocating. The
        // message buffer remains the single source of truth; slices only hold
        // byte offsets into it.
        MessageLine messageLines[MAX_MSG_LEN] = {};
        const uint8_t messageLineCount = buildMessageLines(
            messageLines, MAX_MSG_LEN,
            static_cast<uint8_t>(charsPerLineWide));

        const uint16_t titleBlockH = m_title[0] ? lineH + 4 : 0;
        const uint16_t btnH = lineH + 4;
        const uint16_t btnGap = 2;
        const uint16_t buttonBlockH = m_buttonCount > 0
            ? m_buttonCount * btnH + (m_buttonCount - 1) * btnGap
            : 0;

        // Preserve room for buttons first. On the 240x135 display this keeps
        // a multiline message from pushing the actionable controls off-screen.
        const uint16_t fixedContentH = 6 + titleBlockH + buttonBlockH;
        uint8_t visibleMessageLines = 0;
        if (messageLineCount > 0 && maxDh > fixedContentH + 2) {
            const uint16_t availableMessageH = maxDh - fixedContentH - 2;
            if (availableMessageH >= lineH) {
                uint16_t fittingLines =
                    1 + (availableMessageH - lineH) / lineAdvance;
                if (fittingLines > messageLineCount) {
                    fittingLines = messageLineCount;
                }
                visibleMessageLines = static_cast<uint8_t>(fittingLines);
            }
        }

        const uint16_t messageTextH = visibleMessageLines > 0
            ? lineH + (visibleMessageLines - 1) * lineAdvance
            : 0;
        const uint16_t messageBlockH = visibleMessageLines > 0
            ? messageTextH + 2
            : 0;
        const uint16_t contentH = fixedContentH + messageBlockH;
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
        if (visibleMessageLines > 0) {
            char line[MAX_MSG_LEN];
            for (uint8_t i = 0; i < visibleMessageLines; ++i) {
                const MessageLine& slice = messageLines[i];
                if (slice.length > 0) {
                    memcpy(line, m_message + slice.start, slice.length);
                }
                line[slice.length] = '\0';
                canvas.drawText(textX, textY + i * lineAdvance, line,
                                theme.fgPrimary, scale);
            }
            textY += messageBlockH;
        }

        // Buttons (vertical layout)
        if (m_buttonCount > 0) {
            uint16_t btnW = dw > 16 ? dw - 16 : 0;
            int16_t btnX = dx + 8;
            int16_t dialogBottom = dy + dh;

            for (uint8_t i = 0; i < m_buttonCount; i++) {
                int16_t btnY = textY + i * (btnH + btnGap);
                if (btnW == 0 || btnY + btnH > dialogBottom) {
                    break; // Overflow guard
                }

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

        // Modal shortcuts are actions, so they are edge-triggered just like
        // button activation. Directional keys retain their repeat behavior.
        if (event.isDown() && m_inputCallback && m_inputCallback(event)) {
            return true;
        }

        switch (event.key) {
        case Key::LEFT:
        case Key::UP:
            if (m_selectedButton > 0) { m_selectedButton--; markDirty(); }
            return true;
        case Key::RIGHT:
        case Key::DOWN:
            if (m_buttonCount > 0 &&
                m_selectedButton + 1 < m_buttonCount) {
                m_selectedButton++; markDirty();
            }
            return true;
        case Key::ENTER:
            if (event.isDown() && m_selectedButton < m_buttonCount &&
                m_buttons[m_selectedButton].callback) {
                m_buttons[m_selectedButton].callback();
            }
            return true;
        case Key::SPACE:
            if (event.isDown() && m_spaceActivates &&
                m_selectedButton < m_buttonCount &&
                m_buttons[m_selectedButton].callback) {
                m_buttons[m_selectedButton].callback();
            }
            return true;
        case Key::ESCAPE:
            if (event.isDown()) {
                if (m_hasEscapeCallback) {
                    m_escapeCallback();
                } else if (m_buttonCount > 0 &&
                           m_buttons[m_buttonCount - 1].callback) {
                    m_buttons[m_buttonCount - 1].callback();
                }
            }
            return true;
        default:
            return true;  // Modal consumes all input
        }
    }

private:
    struct MessageLine {
        uint8_t start = 0;
        uint8_t length = 0;
    };

    struct Button {
        char label[MAX_BTN_LEN] = {};
        ButtonCallback callback = nullptr;
    };

    uint8_t buildMessageLines(MessageLine* lines, uint8_t capacity,
                              uint8_t maxChars) const {
        if (!lines || capacity == 0 || maxChars == 0 || !m_message[0]) {
            return 0;
        }

        const uint8_t messageLen =
            static_cast<uint8_t>(strlen(m_message));
        uint8_t count = 0;
        uint8_t pos = 0;

        while (pos < messageLen && count < capacity) {
            // Spaces introduced at a wrap boundary are separators rather than
            // indentation; skipping them avoids an empty line after a word
            // whose length exactly fills the preceding physical line.
            while (pos < messageLen && m_message[pos] == ' ') ++pos;
            if (pos >= messageLen) break;

            if (m_message[pos] == '\n') {
                lines[count].start = pos;
                lines[count].length = 0;
                ++count;
                ++pos;
                continue;
            }

            const uint8_t start = pos;
            uint8_t length = 0;
            while (pos < messageLen && m_message[pos] != '\n' &&
                   length < maxChars) {
                ++pos;
                ++length;
            }

            // Prefer a word boundary when a physical line is too long. The
            // source is untouched; the next slice simply begins after spaces.
            if (pos < messageLen && m_message[pos] != '\n' &&
                length == maxChars) {
                uint8_t split = length;
                while (split > 0 && m_message[start + split - 1] != ' ') {
                    --split;
                }
                if (split > 0) {
                    length = static_cast<uint8_t>(split - 1);
                    pos = static_cast<uint8_t>(start + split);
                    while (pos < messageLen && m_message[pos] == ' ') ++pos;
                }
            } else if (pos < messageLen && m_message[pos] == '\n') {
                ++pos;
            }

            lines[count].start = start;
            lines[count].length = length;
            ++count;
        }

        // A terminal newline denotes a final empty line.
        if (count < capacity && messageLen > 0 &&
            m_message[messageLen - 1] == '\n') {
            lines[count].start = messageLen;
            lines[count].length = 0;
            ++count;
        }
        return count;
    }

    char    m_title[MAX_TITLE_LEN] = {};
    char    m_message[MAX_MSG_LEN] = {};
    Button  m_buttons[MAX_BUTTONS] = {};
    uint8_t m_buttonCount = 0;
    uint8_t m_selectedButton = 0;
    bool m_spaceActivates = true;
    ButtonCallback m_escapeCallback = nullptr;
    bool m_hasEscapeCallback = false;
    InputCallback m_inputCallback = nullptr;
};

} // namespace CardGFX

#endif // CARDGFX_WIDGET_MODAL_H
