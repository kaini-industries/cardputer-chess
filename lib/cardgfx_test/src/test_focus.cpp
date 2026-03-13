#include "cardgfx_test.h"

using namespace CardGFX;

namespace CardGFXTest {

// ── Helper: widget that tracks input events ──────────────────────

class InputTracker : public Widget {
public:
    InputTracker() { m_focusable = true; }

    uint8_t inputCount = 0;
    uint8_t lastKey = Key::NONE;

    bool onInput(const InputEvent& event) override {
        if (event.isDown()) {
            inputCount++;
            lastKey = event.key;
        }
        return true; // consume
    }
};

// ── C1: focusNext/focusPrev cycle ────────────────────────────────

static void testC1_FocusCycle() {
    TEST_BEGIN("C1: focusNext/focusPrev cycle");

    FocusChain chain;
    InputTracker w1, w2, w3;
    w1.setVisible(true);
    w2.setVisible(true);
    w3.setVisible(true);

    chain.add(&w1);
    chain.add(&w2);
    chain.add(&w3);

    // First added widget should be auto-focused
    TEST_ASSERT(chain.focused() == &w1, "w1 should be initially focused");
    TEST_ASSERT(w1.isFocused(), "w1.isFocused() should be true");

    chain.focusNext();
    TEST_ASSERT(chain.focused() == &w2, "w2 should be focused after focusNext");
    TEST_ASSERT(!w1.isFocused(), "w1 should have lost focus");
    TEST_ASSERT(w2.isFocused(), "w2 should have gained focus");

    chain.focusNext();
    TEST_ASSERT(chain.focused() == &w3, "w3 should be focused");

    chain.focusNext();
    TEST_ASSERT(chain.focused() == &w1, "should wrap back to w1");

    chain.focusPrev();
    TEST_ASSERT(chain.focused() == &w3, "focusPrev from w1 should wrap to w3");

    TEST_END();
}

// ── C2: Hidden widgets skipped in focus traversal ────────────────

static void testC2_HiddenWidgetsSkipped() {
    TEST_BEGIN("C2: Hidden widgets skipped in focus");

    FocusChain chain;
    InputTracker w1, w2, w3;
    w1.setVisible(true);
    w2.setVisible(true);
    w3.setVisible(true);

    chain.add(&w1);
    chain.add(&w2);
    chain.add(&w3);

    // Hide w2
    w2.setVisible(false);

    // Focus should be on w1
    TEST_ASSERT(chain.focused() == &w1, "w1 should be focused");

    chain.focusNext();
    TEST_ASSERT(chain.focused() == &w3, "should skip hidden w2 and focus w3");

    chain.focusPrev();
    TEST_ASSERT(chain.focused() == &w1, "should skip hidden w2 going back to w1");

    TEST_END();
}

// ── C3: focusWidget directly sets focus ──────────────────────────

static void testC3_FocusWidgetDirect() {
    TEST_BEGIN("C3: focusWidget() directly sets focus");

    FocusChain chain;
    InputTracker w1, w2, w3;
    w1.setVisible(true);
    w2.setVisible(true);
    w3.setVisible(true);

    chain.add(&w1);
    chain.add(&w2);
    chain.add(&w3);

    TEST_ASSERT(chain.focused() == &w1, "initial focus on w1");

    chain.focusWidget(&w3);
    TEST_ASSERT(chain.focused() == &w3, "focus should be on w3");
    TEST_ASSERT(!w1.isFocused(), "w1 should not be focused");
    TEST_ASSERT(w3.isFocused(), "w3 should be focused");

    chain.focusWidget(&w2);
    TEST_ASSERT(chain.focused() == &w2, "focus should be on w2");
    TEST_ASSERT(!w3.isFocused(), "w3 should have lost focus");

    TEST_END();
}

// ── C4: Input routed only to focused widget ──────────────────────

static void testC4_InputRoutedToFocused() {
    TEST_BEGIN("C4: Input routed only to focused widget");

    Scene scene("testC4");
    InputTracker w1, w2;
    w1.setVisible(true);
    w1.setBounds({0, 0, 20, 20});
    w2.setVisible(true);
    w2.setBounds({30, 0, 20, 20});

    scene.addWidget(&w1, true);
    scene.addWidget(&w2, true);

    // w1 should be focused (first added)
    TEST_ASSERT(scene.focusChain().focused() == &w1, "w1 should be focused");

    // Route an event
    InputEvent event = makeKeyDown('a');
    scene.routeInput(event);

    TEST_ASSERT(w1.inputCount == 1, "w1 should have received input");
    TEST_ASSERT(w2.inputCount == 0, "w2 should NOT have received input");
    TEST_ASSERT(w1.lastKey == 'a', "w1 should have received key 'a'");

    // Switch focus to w2
    scene.focusChain().focusWidget(&w2);
    InputEvent event2 = makeKeyDown('b');
    scene.routeInput(event2);

    TEST_ASSERT(w1.inputCount == 1, "w1 should still be at 1");
    TEST_ASSERT(w2.inputCount == 1, "w2 should now have received input");
    TEST_ASSERT(w2.lastKey == 'b', "w2 should have received key 'b'");

    TEST_END();
}

// ── Suite Entry Point ────────────────────────────────────────────

void runFocusTests() {
    TEST_SUITE_BEGIN("Focus Chain");

    testC1_FocusCycle();
    testC2_HiddenWidgetsSkipped();
    testC3_FocusWidgetDirect();
    testC4_InputRoutedToFocused();

    TEST_SUITE_END("Focus Chain");
}

} // namespace CardGFXTest
