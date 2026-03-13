#include "cardgfx_test.h"

using namespace CardGFX;

namespace CardGFXTest {

// ── Helper: simple widget that fills its canvas with a solid color ──

class ColorWidget : public Widget {
public:
    uint16_t fillColor = 0;
    uint16_t drawCount = 0;

    void onDraw(Canvas& canvas, const Theme& theme) override {
        canvas.fill(fillColor);
        drawCount++;
    }
};

// ── A1: Widget hide clears pixels ────────────────────────────────

static void testA1_WidgetHideClearsPixels() {
    TEST_BEGIN("A1: Widget hide clears pixels");

    const Theme& theme = Themes::Dark;

    // Create framebuffer
    Canvas fb;
    TEST_ASSERT(fb.create(100, 50, false), "fb.create failed");
    fb.fill(theme.bgPrimary);

    // Create scene with a label
    Scene scene("testA1");
    Label label;
    label.setBounds({10, 10, 60, 14});
    label.setText("Hello");
    label.setDrawBg(true);
    scene.addWidget(&label);

    // Draw — label should render text pixels
    drawSceneFrame(scene, fb, theme);
    Rect labelRegion = {10, 10, 60, 14};
    TEST_ASSERT(regionContainsColor(fb, labelRegion, theme.fgPrimary),
                "label text should contain fgPrimary pixels");

    // Hide the label
    label.setVisible(false);

    // Draw again — hidden widget pre-pass should clear the region
    drawSceneFrame(scene, fb, theme);
    TEST_ASSERT(regionIsSolidColor(fb, labelRegion, theme.bgPrimary),
                "hidden label region should be all bgPrimary");

    TEST_END();
}

// ── A2: Scene push/pop clears framebuffer ────────────────────────

static void testA2_ScenePushPopClearsFramebuffer() {
    TEST_BEGIN("A2: Scene push/pop clears framebuffer");

    const Theme& theme = Themes::Dark;
    const uint16_t RED = HAL::rgb565(255, 0, 0);
    const uint16_t BLUE = HAL::rgb565(0, 0, 255);

    Canvas fb;
    TEST_ASSERT(fb.create(100, 50, false), "fb.create failed");
    fb.fill(theme.bgPrimary);

    // Scene A: red widget at (10,10,30,30)
    Scene sceneA("sceneA");
    ColorWidget widgetA;
    widgetA.setBounds({10, 10, 30, 30});
    widgetA.fillColor = RED;
    sceneA.addWidget(&widgetA);

    // Scene B: blue widget at (60,10,30,30) — different position
    Scene sceneB("sceneB");
    ColorWidget widgetB;
    widgetB.setBounds({60, 10, 30, 30});
    widgetB.fillColor = BLUE;
    sceneB.addWidget(&widgetB);

    // Use SceneManager
    SceneManager sm;
    InputManager input;

    // Push scene A and render
    sm.push(&sceneA);
    sm.processFrame(16, input, fb, theme);
    TEST_ASSERT(regionContainsColor(fb, {10, 10, 30, 30}, RED),
                "sceneA should show red widget");

    // Push scene B — processFrame should clear fb then draw B
    sm.push(&sceneB);
    sm.processFrame(16, input, fb, theme);
    TEST_ASSERT(regionIsSolidColor(fb, {10, 10, 30, 30}, theme.bgPrimary),
                "sceneA's red region should be cleared after scene change");
    TEST_ASSERT(regionContainsColor(fb, {60, 10, 30, 30}, BLUE),
                "sceneB should show blue widget");

    // Pop scene B — should clear fb and re-render scene A
    sm.pop();
    widgetA.markDirty(); // scene A re-enters, needs redraw
    sm.processFrame(16, input, fb, theme);
    TEST_ASSERT(regionIsSolidColor(fb, {60, 10, 30, 30}, theme.bgPrimary),
                "sceneB's blue region should be cleared after pop");
    TEST_ASSERT(regionContainsColor(fb, {10, 10, 30, 30}, RED),
                "sceneA's red widget should be restored after pop");

    TEST_END();
}

// ── A3: Paint order (painter's algorithm) ────────────────────────

static void testA3_PaintOrder() {
    TEST_BEGIN("A3: Paint order (overlapping widgets)");

    const Theme& theme = Themes::Dark;
    const uint16_t RED = HAL::rgb565(255, 0, 0);
    const uint16_t BLUE = HAL::rgb565(0, 0, 255);

    Canvas fb;
    TEST_ASSERT(fb.create(100, 80, false), "fb.create failed");
    fb.fill(theme.bgPrimary);

    Scene scene("testA3");

    // Widget A (red) at (10,10,40,40) — added first
    ColorWidget widgetA;
    widgetA.setBounds({10, 10, 40, 40});
    widgetA.fillColor = RED;
    scene.addWidget(&widgetA);

    // Widget B (blue) at (30,30,40,40) — added second, overlaps A
    ColorWidget widgetB;
    widgetB.setBounds({30, 30, 40, 40});
    widgetB.fillColor = BLUE;
    scene.addWidget(&widgetB);

    drawSceneFrame(scene, fb, theme);

    // Overlap region (30,30)-(50,50) should be blue (B drew last)
    TEST_ASSERT(regionIsSolidColor(fb, {30, 30, 20, 20}, BLUE),
                "overlap region should be blue (B painted over A)");

    // Non-overlap of A (10,10)-(30,30) should be red
    TEST_ASSERT(regionIsSolidColor(fb, {10, 10, 20, 20}, RED),
                "non-overlap of A should be red");

    // Non-overlap of B (50,50)-(70,70) should be blue
    TEST_ASSERT(regionIsSolidColor(fb, {50, 50, 20, 20}, BLUE),
                "non-overlap of B should be blue");

    TEST_END();
}

// ── A4: Only dirty widgets redraw ────────────────────────────────

static void testA4_OnlyDirtyWidgetsRedraw() {
    TEST_BEGIN("A4: Only dirty widgets redraw");

    const Theme& theme = Themes::Dark;
    const uint16_t RED = HAL::rgb565(255, 0, 0);
    const uint16_t GREEN = HAL::rgb565(0, 255, 0);

    Canvas fb;
    TEST_ASSERT(fb.create(100, 50, false), "fb.create failed");
    fb.fill(theme.bgPrimary);

    Scene scene("testA4");

    ColorWidget widgetA;
    widgetA.setBounds({5, 5, 20, 20});
    widgetA.fillColor = RED;
    scene.addWidget(&widgetA);

    ColorWidget widgetB;
    widgetB.setBounds({50, 5, 20, 20});
    widgetB.fillColor = GREEN;
    scene.addWidget(&widgetB);

    // Draw both
    drawSceneFrame(scene, fb, theme);
    TEST_ASSERT(widgetA.drawCount == 1, "A should have drawn once");
    TEST_ASSERT(widgetB.drawCount == 1, "B should have drawn once");

    // Only mark A dirty
    widgetA.markDirty();
    drawSceneFrame(scene, fb, theme);
    TEST_ASSERT(widgetA.drawCount == 2, "A should have drawn twice (was dirty)");
    TEST_ASSERT(widgetB.drawCount == 1, "B should still be at 1 (not dirty)");

    TEST_END();
}

// ── A5: Modal overlay covers content ─────────────────────────────

static void testA5_ModalOverlayCoversContent() {
    TEST_BEGIN("A5: Modal overlay covers content");

    const Theme& theme = Themes::Dark;

    Canvas fb;
    TEST_ASSERT(fb.create(120, 70, false), "fb.create failed");
    fb.fill(theme.bgPrimary);

    Scene scene("testA5");

    // Background label
    Label label;
    label.setBounds({10, 10, 60, 14});
    label.setText("Background");
    label.setDrawBg(true);
    scene.addWidget(&label);

    // Full-screen modal
    Modal modal;
    modal.setBounds({0, 0, 120, 70});
    modal.setTitle("Test Modal");
    modal.setMessage("Hello");
    modal.addButton("OK", [](){});
    scene.addWidget(&modal);

    // Draw with modal hidden — label should be visible
    drawSceneFrame(scene, fb, theme);
    TEST_ASSERT(regionContainsColor(fb, {10, 10, 60, 14}, theme.fgPrimary),
                "label text should be visible before modal show");

    // Show modal and redraw
    modal.show();
    drawSceneFrame(scene, fb, theme);

    // The modal fills its canvas with bgPrimary then draws dialog box
    // The label region should no longer contain label text (it's covered)
    // Modal draws over the entire framebuffer area
    // Check that the modal's dialog elements are present
    TEST_ASSERT(regionContainsColor(fb, {0, 0, 120, 70}, theme.bgSecondary),
                "modal dialog box should be visible");

    TEST_END();
}

// ── A6: Modal dismiss restores content ───────────────────────────

static void testA6_ModalDismissRestoresContent() {
    TEST_BEGIN("A6: Modal dismiss restores content");

    const Theme& theme = Themes::Dark;

    Canvas fb;
    TEST_ASSERT(fb.create(120, 70, false), "fb.create failed");
    fb.fill(theme.bgPrimary);

    Scene scene("testA6");

    Label label;
    label.setBounds({10, 10, 60, 14});
    label.setText("Restore Me");
    label.setDrawBg(true);
    scene.addWidget(&label);

    Modal modal;
    modal.setBounds({0, 0, 120, 70});
    modal.setTitle("Modal");
    modal.addButton("OK", [](){});
    scene.addWidget(&modal);

    // Draw label visible
    drawSceneFrame(scene, fb, theme);
    TEST_ASSERT(regionContainsColor(fb, {10, 10, 60, 14}, theme.fgPrimary),
                "label should be visible initially");

    // Show modal, draw
    modal.show();
    drawSceneFrame(scene, fb, theme);

    // Hide modal, draw — label should be restored
    modal.hide();
    drawSceneFrame(scene, fb, theme);

    TEST_ASSERT(regionContainsColor(fb, {10, 10, 60, 14}, theme.fgPrimary),
                "label text should be restored after modal dismiss");

    // Modal region should not contain dialog elements
    // (bgSecondary is the modal dialog box color)
    // After hide, the modal's area should be cleared and label redrawn
    TEST_ASSERT(!regionIsSolidColor(fb, {10, 10, 60, 14}, theme.bgSecondary),
                "label region should not be modal dialog color");

    TEST_END();
}

// ── Suite Entry Point ────────────────────────────────────────────

void runRenderingTests() {
    TEST_SUITE_BEGIN("Rendering Correctness");

    testA1_WidgetHideClearsPixels();
    testA2_ScenePushPopClearsFramebuffer();
    testA3_PaintOrder();
    testA4_OnlyDirtyWidgetsRedraw();
    testA5_ModalOverlayCoversContent();
    testA6_ModalDismissRestoresContent();

    TEST_SUITE_END("Rendering Correctness");
}

} // namespace CardGFXTest
