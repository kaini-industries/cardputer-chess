#include "cardgfx.h"
#include <Arduino.h>

namespace CardGFX {

// ── Singletons ───────────────────────────────────────────────────

static SceneManager  s_sceneManager;
static InputManager  s_inputManager;
static Canvas        s_framebuffer;
static Theme         s_activeTheme = Themes::Dark;
static uint32_t      s_lastTickMs = 0;
static bool          s_initialized = false;

// ── Public API ───────────────────────────────────────────────────

bool init(uint8_t rotation, uint8_t brightness) {
    if (s_initialized) return true;

    // 1. Initialize display hardware
    if (!HAL::init(rotation, brightness)) return false;

    // 2. Initialize input
    if (!s_inputManager.init()) return false;

    // 3. Create main framebuffer in PSRAM
    if (!s_framebuffer.create(SCREEN_W, SCREEN_H, true)) return false;
    s_framebuffer.fill(s_activeTheme.bgPrimary);

    // 4. Set initial tick time
    s_lastTickMs = millis();

    s_initialized = true;
    return true;
}

uint32_t tick() {
    if (!s_initialized) return 0;

    // Calculate delta time
    uint32_t now = millis();
    uint32_t dt = now - s_lastTickMs;
    s_lastTickMs = now;

    // 1. Poll input
    s_inputManager.poll();

    // 2. Process frame (tick scenes, route input, draw, push to screen)
    s_sceneManager.processFrame(dt, s_inputManager, s_framebuffer, s_activeTheme);

    // 3. Frame rate limiting
    uint32_t elapsed = millis() - now;
    if (elapsed < FRAME_TIME_MS) {
        delay(FRAME_TIME_MS - elapsed);
    }

    return dt;
}

void shutdown() {
    s_framebuffer.destroy();
    s_initialized = false;
}

SceneManager& scenes() { return s_sceneManager; }
InputManager& input()  { return s_inputManager; }
const Theme& theme()   { return s_activeTheme; }

void setTheme(const Theme& theme) {
    s_activeTheme = theme;
    // Mark all widgets as dirty so they redraw with new theme
    Scene* active = s_sceneManager.active();
    if (active) {
        // Force a full redraw
        s_framebuffer.markAllDirty();
    }
}

} // namespace CardGFX
