#include "cardgfx_test.h"

using namespace CardGFX;

namespace CardGFXTest {

// ── D1: Grid cell states render correct colors ───────────────────

static void testD1_GridCellStates() {
    TEST_BEGIN("D1: Grid cell states render colors");

    const Theme& theme = Themes::Dark;

    Canvas fb;
    TEST_ASSERT(fb.create(80, 80, false), "fb.create failed");
    fb.fill(theme.bgPrimary);

    Scene scene("testD1");
    Grid grid;
    grid.setBounds({0, 0, 80, 80});
    grid.setGridSize(4, 4);
    grid.setCellSize(20, 20);
    grid.setFocused(true);
    scene.addWidget(&grid, true);

    // Set various cell states
    grid.setCursor(0, 0);
    grid.setHighlighted(1, 0, true);
    grid.setMarked(2, 0, true);
    grid.setSelected(3, 0, true);

    drawSceneFrame(scene, fb, theme);

    // Cell (0,0) — cursor: should contain gridCursor color (cursor outline)
    TEST_ASSERT(regionContainsColor(fb, {0, 0, 20, 20}, theme.gridCursor),
                "cursor cell should contain gridCursor color");

    // Cell (1,0) — highlighted: should have gridHighlight
    TEST_ASSERT(regionContainsColor(fb, {20, 0, 20, 20}, theme.gridHighlight),
                "highlighted cell should contain gridHighlight color");

    // Cell (2,0) — marked: should have accentMuted
    TEST_ASSERT(regionContainsColor(fb, {40, 0, 20, 20}, theme.accentMuted),
                "marked cell should contain accentMuted color");

    // Cell (3,0) — selected: should have accentActive
    TEST_ASSERT(regionContainsColor(fb, {60, 0, 20, 20}, theme.accentActive),
                "selected cell should contain accentActive color");

    TEST_END();
}

// ── D2: Grid cursor navigation ───────────────────────────────────

static void testD2_GridCursorNavigation() {
    TEST_BEGIN("D2: Grid cursor navigation");

    Grid grid;
    grid.setGridSize(4, 4);
    grid.setCellSize(16, 16);
    grid.setBounds({0, 0, 64, 64});
    grid.setCursor(0, 0);

    // Move right
    InputEvent right = makeKeyDown(Key::RIGHT);
    grid.onInput(right);
    TEST_ASSERT(grid.cursorCol() == 1 && grid.cursorRow() == 0,
                "cursor should be at (1,0) after RIGHT");

    // Move down
    InputEvent down = makeKeyDown(Key::DOWN);
    grid.onInput(down);
    TEST_ASSERT(grid.cursorCol() == 1 && grid.cursorRow() == 1,
                "cursor should be at (1,1) after DOWN");

    // Move left
    InputEvent left = makeKeyDown(Key::LEFT);
    grid.onInput(left);
    TEST_ASSERT(grid.cursorCol() == 0 && grid.cursorRow() == 1,
                "cursor should be at (0,1) after LEFT");

    // Move up
    InputEvent up = makeKeyDown(Key::UP);
    grid.onInput(up);
    TEST_ASSERT(grid.cursorCol() == 0 && grid.cursorRow() == 0,
                "cursor should be at (0,0) after UP");

    // Boundary: LEFT at col 0 should stay
    grid.onInput(left);
    TEST_ASSERT(grid.cursorCol() == 0, "cursor col should clamp at 0");

    // Boundary: UP at row 0 should stay
    grid.onInput(up);
    TEST_ASSERT(grid.cursorRow() == 0, "cursor row should clamp at 0");

    // Move to bottom-right corner
    grid.setCursor(3, 3);
    grid.onInput(right);
    TEST_ASSERT(grid.cursorCol() == 3, "cursor col should clamp at max");
    grid.onInput(down);
    TEST_ASSERT(grid.cursorRow() == 3, "cursor row should clamp at max");

    TEST_END();
}

// ── D3: Modal button navigation and callbacks ────────────────────

static void testD3_ModalButtonNavigation() {
    TEST_BEGIN("D3: Modal button navigation");

    Modal modal;
    modal.setBounds({0, 0, 120, 70});
    modal.setTitle("Test");

    uint8_t callbackHit = 0;
    modal.addButton("A", [&callbackHit]() { callbackHit = 1; });
    modal.addButton("B", [&callbackHit]() { callbackHit = 2; });
    modal.addButton("C", [&callbackHit]() { callbackHit = 3; });
    modal.show();

    // Initially button 0 ("A") selected
    // Move right to B
    InputEvent right = makeKeyDown(Key::RIGHT);
    modal.onInput(right);

    // Move right to C
    modal.onInput(right);

    // Press enter — should hit button C (index 2)
    InputEvent enter = makeKeyDown(Key::ENTER);
    modal.onInput(enter);
    TEST_ASSERT(callbackHit == 3, "pressing enter on button C should trigger callback 3");

    // Reset and test left navigation
    callbackHit = 0;
    modal.show(); // resets to button 0

    InputEvent left = makeKeyDown(Key::LEFT);
    modal.onInput(left); // already at 0, should stay
    modal.onInput(enter);
    TEST_ASSERT(callbackHit == 1, "button A callback should be triggered");

    // Test escape triggers last button
    callbackHit = 0;
    modal.show();
    InputEvent esc = makeKeyDown(Key::ESCAPE);
    modal.onInput(esc);
    TEST_ASSERT(callbackHit == 3, "escape should trigger last button (C)");

    TEST_END();
}

// ── D4: List scrolling ───────────────────────────────────────────

static void testD4_ListScrolling() {
    TEST_BEGIN("D4: List scrolling");

    List list;
    list.setBounds({0, 0, 80, 55}); // fits ~5 rows with default 11px height
    list.setFocusable(true);

    // Add 20 items
    for (int i = 0; i < 20; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Item %d", i);
        list.addItem(buf);
    }

    TEST_ASSERT(list.itemCount() == 20, "should have 20 items");
    TEST_ASSERT(list.selected() == 0, "initial selection should be 0");

    // Navigate down 6 times
    InputEvent down = makeKeyDown(Key::DOWN);
    for (int i = 0; i < 6; i++) {
        list.onInput(down);
    }
    TEST_ASSERT(list.selected() == 6, "selection should be 6 after 6 DOWNs");

    // Navigate up
    InputEvent up = makeKeyDown(Key::UP);
    list.onInput(up);
    TEST_ASSERT(list.selected() == 5, "selection should be 5 after UP");

    // Boundary: navigate to start
    for (int i = 0; i < 20; i++) list.onInput(up);
    TEST_ASSERT(list.selected() == 0, "selection should clamp at 0");

    // Boundary: navigate to end
    for (int i = 0; i < 30; i++) list.onInput(down);
    TEST_ASSERT(list.selected() == 19, "selection should clamp at 19");

    // Test scrollToBottom
    list.scrollToBottom();
    TEST_ASSERT(list.selected() == 19, "scrollToBottom should select last item");

    TEST_END();
}

// ── D5: Label text alignment ─────────────────────────────────────

static void testD5_LabelAlignment() {
    TEST_BEGIN("D5: Label text alignment");

    const Theme& theme = Themes::Dark;

    Canvas fb;
    TEST_ASSERT(fb.create(100, 14, false), "fb.create failed");
    fb.fill(theme.bgPrimary);

    Scene scene("testD5");

    // Center-aligned label
    Label label;
    label.setBounds({0, 0, 100, 14});
    label.setText("Hi");
    label.setAlign(Label::Align::Center);
    label.setDrawBg(true);
    scene.addWidget(&label);

    drawSceneFrame(scene, fb, theme);

    // "Hi" is 2 chars * (5+1) * scale1 = 12 pixels wide (approx)
    // Centered in 100px: x ~ 44
    // Left edge (0-10) should not have text
    TEST_ASSERT(regionIsSolidColor(fb, {0, 0, 10, 14}, theme.bgPrimary),
                "far-left should be empty (text is centered)");

    // Right edge (90-100) should not have text
    TEST_ASSERT(regionIsSolidColor(fb, {90, 0, 10, 14}, theme.bgPrimary),
                "far-right should be empty (text is centered)");

    // Center region should contain text pixels
    TEST_ASSERT(regionContainsColor(fb, {30, 0, 40, 14}, theme.fgPrimary),
                "center region should contain text");

    TEST_END();
}

// ── Suite Entry Point ────────────────────────────────────────────

void runWidgetTests() {
    TEST_SUITE_BEGIN("Widget-Specific");

    testD1_GridCellStates();
    testD2_GridCursorNavigation();
    testD3_ModalButtonNavigation();
    testD4_ListScrolling();
    testD5_LabelAlignment();

    TEST_SUITE_END("Widget-Specific");
}

} // namespace CardGFXTest
