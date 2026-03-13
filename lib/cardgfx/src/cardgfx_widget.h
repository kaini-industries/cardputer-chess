#ifndef CARDGFX_WIDGET_H
#define CARDGFX_WIDGET_H

#include "cardgfx_config.h"
#include "cardgfx_canvas.h"
#include "cardgfx_layout.h"
#include "cardgfx_theme.h"
#include "cardgfx_input.h"
#include <cstdint>

namespace CardGFX {

/**
 * Base widget.
 *
 * Widgets own a rectangular screen region, draw into a canvas,
 * and optionally handle input when focused. No deep inheritance —
 * override the virtual methods you need.
 */
class Widget {
public:
    Widget() = default;
    virtual ~Widget() = default;

    // ── Identity ─────────────────────────────────────────────────

    uint8_t id() const { return m_id; }
    void setId(uint8_t id) { m_id = id; }

    // ── Bounds ───────────────────────────────────────────────────

    const Bounds& bounds() const { return m_bounds; }
    void setBounds(const Bounds& b) { m_bounds = b; markDirty(); }
    void setPosition(int16_t x, int16_t y) {
        m_bounds.x = x; m_bounds.y = y; markDirty();
    }
    void setSize(uint16_t w, uint16_t h) {
        m_bounds.w = w; m_bounds.h = h; markDirty();
    }

    // ── Visibility ───────────────────────────────────────────────

    bool isVisible() const { return m_visible; }
    void setVisible(bool v) { m_visible = v; markDirty(); }

    // ── Focus ────────────────────────────────────────────────────

    bool isFocusable() const { return m_focusable; }
    void setFocusable(bool f) { m_focusable = f; }

    bool acceptsText() const { return m_acceptsText; }

    bool isFocused() const { return m_focused; }
    void setFocused(bool f) {
        if (m_focused != f) {
            m_focused = f;
            onFocus(f);
            markDirty();
        }
    }

    // ── Dirty State ──────────────────────────────────────────────

    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

    // ── Lifecycle (override these) ───────────────────────────────

    /**
     * Called when the widget should draw itself.
     * @param canvas  Canvas to draw into (sized to widget bounds).
     * @param theme   Active theme for colors/style.
     */
    virtual void onDraw(Canvas& canvas, const Theme& theme) {}

    /**
     * Called when a key event is routed to this widget (while focused).
     * @return true if the event was consumed.
     */
    virtual bool onInput(const InputEvent& event) { return false; }

    /**
     * Called when focus is gained or lost.
     */
    virtual void onFocus(bool gained) {}

    /**
     * Called every frame with delta time.
     * Use for animations, timers, state transitions.
     */
    virtual void onTick(uint32_t dt_ms) {}

protected:
    uint8_t  m_id        = 0;
    Bounds   m_bounds    = {};
    bool     m_visible   = true;
    bool     m_focusable = false;
    bool     m_focused   = false;
    bool     m_dirty       = true;
    bool     m_acceptsText = false;
};

// ── Focus Chain ──────────────────────────────────────────────────

/**
 * Manages the ordered list of focusable widgets.
 * Tab / arrow keys move focus. The focused widget receives input first.
 */
class FocusChain {
public:
    void clear();

    /**
     * Add a widget to the focus chain.
     * @return true if added.
     */
    bool add(Widget* widget);

    /**
     * Remove a widget from the chain.
     */
    void remove(Widget* widget);

    /** Move focus to the next widget in the chain. */
    void focusNext();

    /** Move focus to the previous widget. */
    void focusPrev();

    /** Set focus to a specific widget. */
    void focusWidget(Widget* widget);

    /** Get the currently focused widget, or nullptr. */
    Widget* focused() const;

    /** Number of widgets in the chain. */
    uint8_t count() const { return m_count; }

private:
    Widget* m_chain[MAX_FOCUS_CHAIN] = {};
    uint8_t m_count = 0;
    int8_t  m_focusIndex = -1;  // -1 = nothing focused
};

} // namespace CardGFX

#endif // CARDGFX_WIDGET_H
