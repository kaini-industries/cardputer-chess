#include "cardgfx_widget.h"

namespace CardGFX {

// ── FocusChain ───────────────────────────────────────────────────

void FocusChain::clear() {
    // Unfocus current
    if (m_focusIndex >= 0 && m_focusIndex < m_count) {
        m_chain[m_focusIndex]->setFocused(false);
    }
    m_count = 0;
    m_focusIndex = -1;
}

bool FocusChain::add(Widget* widget) {
    if (!widget || !widget->isFocusable() || m_count >= MAX_FOCUS_CHAIN)
        return false;

    // Check for duplicates
    for (uint8_t i = 0; i < m_count; i++) {
        if (m_chain[i] == widget) return false;
    }

    m_chain[m_count++] = widget;

    // Auto-focus the first widget added
    if (m_count == 1) {
        m_focusIndex = 0;
        widget->setFocused(true);
    }
    return true;
}

void FocusChain::remove(Widget* widget) {
    if (!widget) return;

    for (uint8_t i = 0; i < m_count; i++) {
        if (m_chain[i] == widget) {
            widget->setFocused(false);

            // Shift remaining items
            for (uint8_t j = i; j < m_count - 1; j++) {
                m_chain[j] = m_chain[j + 1];
            }
            m_count--;
            m_chain[m_count] = nullptr;

            // Adjust focus index
            if (m_count == 0) {
                m_focusIndex = -1;
            } else if (m_focusIndex >= m_count) {
                m_focusIndex = m_count - 1;
                m_chain[m_focusIndex]->setFocused(true);
            } else if (m_focusIndex == i) {
                // Re-focus the widget now at this index
                if (m_focusIndex < m_count) {
                    m_chain[m_focusIndex]->setFocused(true);
                }
            }
            return;
        }
    }
}

void FocusChain::focusNext() {
    if (m_count == 0) return;

    // Unfocus current
    if (m_focusIndex >= 0 && m_focusIndex < m_count) {
        m_chain[m_focusIndex]->setFocused(false);
    }

    // Find next focusable widget (skip hidden/non-focusable)
    for (uint8_t attempts = 0; attempts < m_count; attempts++) {
        m_focusIndex = (m_focusIndex + 1) % m_count;
        Widget* w = m_chain[m_focusIndex];
        if (w->isFocusable() && w->isVisible()) {
            w->setFocused(true);
            return;
        }
    }
}

void FocusChain::focusPrev() {
    if (m_count == 0) return;

    if (m_focusIndex >= 0 && m_focusIndex < m_count) {
        m_chain[m_focusIndex]->setFocused(false);
    }

    for (uint8_t attempts = 0; attempts < m_count; attempts++) {
        m_focusIndex = (m_focusIndex - 1 + m_count) % m_count;
        Widget* w = m_chain[m_focusIndex];
        if (w->isFocusable() && w->isVisible()) {
            w->setFocused(true);
            return;
        }
    }
}

void FocusChain::focusWidget(Widget* widget) {
    if (!widget) return;
    for (uint8_t i = 0; i < m_count; i++) {
        if (m_chain[i] == widget) {
            if (m_focusIndex >= 0 && m_focusIndex < m_count) {
                m_chain[m_focusIndex]->setFocused(false);
            }
            m_focusIndex = i;
            widget->setFocused(true);
            return;
        }
    }
}

Widget* FocusChain::focused() const {
    if (m_focusIndex >= 0 && m_focusIndex < m_count) {
        return m_chain[m_focusIndex];
    }
    return nullptr;
}

} // namespace CardGFX
