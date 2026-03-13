#ifndef CARDGFX_TEST_H
#define CARDGFX_TEST_H

#include <cardgfx.h>
#include <Arduino.h>

namespace CardGFXTest {

// ── Test Result Tracking ─────────────────────────────────────────

struct TestResults {
    uint16_t passed = 0;
    uint16_t failed = 0;
    uint16_t total() const { return passed + failed; }
};

extern TestResults g_results;

// ── Macros ───────────────────────────────────────────────────────

#define TEST_SUITE_BEGIN(name) \
    Serial.printf("\n--- %s ---\n", name)

#define TEST_SUITE_END(name) \
    Serial.printf("--- %s done ---\n", name)

#define TEST_BEGIN(name) \
    { \
        Serial.printf("  [TEST] %s ... ", name); \
        uint32_t _testStart = millis(); \
        bool _testPassed = true

#define TEST_ASSERT(cond, msg) \
    do { \
        if (_testPassed && !(cond)) { \
            Serial.printf("FAIL (%s)\n", msg); \
            CardGFXTest::g_results.failed++; \
            _testPassed = false; \
        } \
    } while(0)

#define TEST_END() \
        if (_testPassed) { \
            Serial.printf("PASS (%lums)\n", millis() - _testStart); \
            CardGFXTest::g_results.passed++; \
        } \
    }

// ── Pixel Assertion Helpers ──────────────────────────────────────

using namespace CardGFX;

/**
 * Check that every pixel in a region is the expected color.
 */
bool regionIsSolidColor(const Canvas& c, Rect region, uint16_t color);

/**
 * Check that at least one pixel in a region is the expected color.
 */
bool regionContainsColor(const Canvas& c, Rect region, uint16_t color);

/**
 * Check that no pixel in a region is the expected color.
 */
bool regionExcludesColor(const Canvas& c, Rect region, uint16_t color);

/**
 * Count how many pixels in a region match the given color.
 */
uint32_t countColor(const Canvas& c, Rect region, uint16_t color);

// ── Event Construction Helpers ───────────────────────────────────

InputEvent makeKeyDown(uint8_t key, uint8_t mods = Mod::NONE);
InputEvent makeKeyUp(uint8_t key, uint8_t mods = Mod::NONE);

// ── Scene Draw Helper ────────────────────────────────────────────

/**
 * Draw one frame of a scene (drawWidgets + onDrawOverlay) without
 * going through SceneManager or pushToScreen.
 */
void drawSceneFrame(Scene& scene, Canvas& fb, const Theme& theme);

// ── Test Suite Entry Points ──────────────────────────────────────

void runRenderingTests();
void runSceneTests();
void runFocusTests();
void runWidgetTests();

/**
 * Run all assertion test suites. Returns total failures.
 */
uint16_t runAllTests();

// ── Print Summary ────────────────────────────────────────────────

void printSummary();

} // namespace CardGFXTest

#endif // CARDGFX_TEST_H
