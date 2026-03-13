#ifndef CARDGFX_WIDGET_LIST_H
#define CARDGFX_WIDGET_LIST_H

#include "../cardgfx_widget.h"
#include <cstring>
#include <cstdio>
#include <functional>

namespace CardGFX {

/**
 * List: a scrollable vertical list of text items.
 *
 * Supports selection, scrolling, and item callbacks.
 * Perfect for move history, menus, chat logs.
 */
class List : public Widget {
public:
    static constexpr uint8_t  MAX_ITEMS      = 64;
    static constexpr uint8_t  MAX_ITEM_LEN   = 32;

    using SelectCallback = std::function<void(uint8_t index, const char* text)>;

    List() { m_focusable = true; }

    // ── Items ────────────────────────────────────────────────────

    bool addItem(const char* text) {
        if (m_itemCount >= MAX_ITEMS || !text) return false;
        strncpy(m_items[m_itemCount], text, MAX_ITEM_LEN - 1);
        m_items[m_itemCount][MAX_ITEM_LEN - 1] = '\0';
        m_itemCount++;
        markDirty();
        return true;
    }

    void setItem(uint8_t index, const char* text) {
        if (index >= m_itemCount || !text) return;
        strncpy(m_items[index], text, MAX_ITEM_LEN - 1);
        m_items[index][MAX_ITEM_LEN - 1] = '\0';
        markDirty();
    }

    const char* getItem(uint8_t index) const {
        if (index >= m_itemCount) return "";
        return m_items[index];
    }

    void clearItems() {
        m_itemCount = 0;
        m_selected = 0;
        m_scrollOffset = 0;
        markDirty();
    }

    uint8_t itemCount() const { return m_itemCount; }

    // ── Selection ────────────────────────────────────────────────

    uint8_t selected() const { return m_selected; }

    void setSelected(uint8_t index) {
        if (index < m_itemCount) {
            m_selected = index;
            ensureVisible(index);
            markDirty();
        }
    }

    // ── Configuration ────────────────────────────────────────────

    void setOnSelect(SelectCallback cb) { m_onSelect = cb; }
    void setItemHeight(uint8_t h) { m_itemHeight = h; markDirty(); }
    void setShowIndex(bool show) { m_showIndex = show; markDirty(); }
    void setAutoScroll(bool auto_scroll) { m_autoScroll = auto_scroll; }

    /** Scroll to show the last item (useful for logs/history). */
    void scrollToBottom() {
        if (m_itemCount == 0) return;
        uint8_t visible = visibleCount();
        if (m_itemCount > visible) {
            m_scrollOffset = m_itemCount - visible;
        }
        m_selected = m_itemCount - 1;
        markDirty();
    }

    // ── Lifecycle ────────────────────────────────────────────────

    void onDraw(Canvas& canvas, const Theme& theme) override {
        canvas.fill(theme.bgSecondary);

        // Border
        canvas.drawRect(0, 0, m_bounds.w, m_bounds.h,
                        m_focused ? theme.borderFocus : theme.border);

        uint8_t scale = theme.fontScaleMd;
        uint8_t rowH = m_itemHeight > 0 ? m_itemHeight : (FONT_CHAR_H * scale + 4);
        uint8_t visible = (m_bounds.h - 2) / rowH;

        for (uint8_t i = 0; i < visible && (m_scrollOffset + i) < m_itemCount; i++) {
            uint8_t idx = m_scrollOffset + i;
            int16_t y = 1 + i * rowH;

            // Highlight selected row
            if (idx == m_selected && m_focused) {
                canvas.fillRect(1, y, m_bounds.w - 2, rowH, theme.accent);
            } else if (idx == m_selected) {
                canvas.fillRect(1, y, m_bounds.w - 2, rowH, theme.accentMuted);
            }

            // Text
            uint16_t fg = (idx == m_selected && m_focused)
                          ? theme.bgPrimary : theme.fgPrimary;

            int16_t textX = 3;
            int16_t textY = y + (rowH - FONT_CHAR_H * scale) / 2;

            if (m_showIndex) {
                char indexBuf[8];
                snprintf(indexBuf, sizeof(indexBuf), "%2d.", idx + 1);
                canvas.drawText(textX, textY, indexBuf, theme.fgSecondary, scale);
                textX += canvas.textWidth("00.", scale) + 2;
            }

            canvas.drawText(textX, textY, m_items[idx], fg, scale);
        }

        // Scrollbar
        if (m_itemCount > visible) {
            uint16_t barH = m_bounds.h - 2;
            uint16_t thumbH = (visible * barH) / m_itemCount;
            if (thumbH < 4) thumbH = 4;
            uint16_t thumbY = 1 + (m_scrollOffset * (barH - thumbH))
                              / (m_itemCount - visible);
            canvas.fillRect(m_bounds.w - 3, thumbY, 2, thumbH, theme.fgSecondary);
        }
    }

    bool onInput(const InputEvent& event) override {
        if (!event.isDown() && !event.isRepeat()) return false;

        uint8_t k = event.key;

        // Allow ; , . / as arrow shortcuts without FN
        if (k == ';' || k == ':') k = Key::UP;
        else if (k == '.' || k == '>') k = Key::DOWN;
        else if (k == ',' || k == '<') k = Key::LEFT;
        else if (k == '/' || k == '?') k = Key::RIGHT;

        switch (k) {
        case Key::UP:
            if (m_selected > 0) {
                m_selected--;
                ensureVisible(m_selected);
                markDirty();
            }
            return true;

        case Key::DOWN:
            if (m_selected < m_itemCount - 1) {
                m_selected++;
                ensureVisible(m_selected);
                markDirty();
            }
            return true;

        case Key::ENTER:
            if (m_onSelect && m_selected < m_itemCount) {
                m_onSelect(m_selected, m_items[m_selected]);
            }
            return true;

        default:
            return false;
        }
    }

private:
    char     m_items[MAX_ITEMS][MAX_ITEM_LEN] = {};
    uint8_t  m_itemCount = 0;
    uint8_t  m_selected = 0;
    uint8_t  m_scrollOffset = 0;
    uint8_t  m_itemHeight = 0;  // 0 = auto from font
    bool     m_showIndex = false;
    bool     m_autoScroll = false;

    SelectCallback m_onSelect = nullptr;

    uint8_t visibleCount() const {
        uint8_t rowH = m_itemHeight > 0 ? m_itemHeight : 11;
        return (m_bounds.h - 2) / rowH;
    }

    void ensureVisible(uint8_t index) {
        uint8_t visible = visibleCount();
        if (index < m_scrollOffset) {
            m_scrollOffset = index;
        } else if (index >= m_scrollOffset + visible) {
            m_scrollOffset = index - visible + 1;
        }
    }
};

} // namespace CardGFX

#endif // CARDGFX_WIDGET_LIST_H
