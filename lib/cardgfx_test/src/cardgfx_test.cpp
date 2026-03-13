#include "cardgfx_test.h"

namespace CardGFXTest {

TestResults g_results;

// ── Pixel Helpers ────────────────────────────────────────────────

bool regionIsSolidColor(const Canvas& c, Rect region, uint16_t color) {
    const uint16_t* buf = c.buffer();
    uint16_t w = c.width();
    int16_t x2 = region.x + region.w;
    int16_t y2 = region.y + region.h;

    for (int16_t y = region.y; y < y2; y++) {
        for (int16_t x = region.x; x < x2; x++) {
            if (buf[(uint32_t)y * w + x] != color) return false;
        }
    }
    return true;
}

bool regionContainsColor(const Canvas& c, Rect region, uint16_t color) {
    const uint16_t* buf = c.buffer();
    uint16_t w = c.width();
    int16_t x2 = region.x + region.w;
    int16_t y2 = region.y + region.h;

    for (int16_t y = region.y; y < y2; y++) {
        for (int16_t x = region.x; x < x2; x++) {
            if (buf[(uint32_t)y * w + x] == color) return true;
        }
    }
    return false;
}

bool regionExcludesColor(const Canvas& c, Rect region, uint16_t color) {
    return !regionContainsColor(c, region, color);
}

uint32_t countColor(const Canvas& c, Rect region, uint16_t color) {
    const uint16_t* buf = c.buffer();
    uint16_t w = c.width();
    int16_t x2 = region.x + region.w;
    int16_t y2 = region.y + region.h;
    uint32_t count = 0;

    for (int16_t y = region.y; y < y2; y++) {
        for (int16_t x = region.x; x < x2; x++) {
            if (buf[(uint32_t)y * w + x] == color) count++;
        }
    }
    return count;
}

// ── Event Helpers ────────────────────────────────────────────────

InputEvent makeKeyDown(uint8_t key, uint8_t mods) {
    InputEvent e;
    e.type = EventType::KeyDown;
    e.key = key;
    e.character = (key >= 32 && key < 127) ? (char)key : 0;
    e.modifiers = mods;
    return e;
}

InputEvent makeKeyUp(uint8_t key, uint8_t mods) {
    InputEvent e;
    e.type = EventType::KeyUp;
    e.key = key;
    e.character = (key >= 32 && key < 127) ? (char)key : 0;
    e.modifiers = mods;
    return e;
}

// ── Scene Helper ─────────────────────────────────────────────────

void drawSceneFrame(Scene& scene, Canvas& fb, const Theme& theme) {
    scene.drawWidgets(fb, theme);
    scene.onDrawOverlay(fb, theme);
}

// ── Run All ──────────────────────────────────────────────────────

uint16_t runAllTests() {
    g_results = {};

    runRenderingTests();
    runSceneTests();
    runFocusTests();
    runWidgetTests();

    printSummary();
    return g_results.failed;
}

void printSummary() {
    Serial.println();
    Serial.println("========================================");
    Serial.printf("  RESULTS: %u passed, %u failed, %u total\n",
                  g_results.passed, g_results.failed, g_results.total());
    Serial.println("========================================");
    if (g_results.failed == 0) {
        Serial.println("  ALL TESTS PASSED");
    } else {
        Serial.println("  ** FAILURES DETECTED **");
    }
    Serial.println("========================================");
}

} // namespace CardGFXTest
