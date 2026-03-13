#ifndef CARDGFX_CONFIG_H
#define CARDGFX_CONFIG_H

#include <cstdint>

namespace CardGFX {

// ── Display ──────────────────────────────────────────────────────
constexpr uint16_t SCREEN_W              = 240;
constexpr uint16_t SCREEN_H              = 135;
constexpr uint8_t  COLOR_DEPTH           = 16;   // RGB565
constexpr uint8_t  FONT_CHAR_W           = 5;
constexpr uint8_t  FONT_CHAR_H           = 7;
constexpr uint8_t  FONT_SPACING          = 1;

// ── Canvas Pool ──────────────────────────────────────────────────
// Max number of canvases that can be alive at once.
// The main framebuffer, region canvases, and scratch sprites all
// draw from this pool. Increase if your app is widget-heavy.
constexpr uint8_t  MAX_CANVASES          = 16;

// ── Widgets ──────────────────────────────────────────────────────
constexpr uint8_t  MAX_WIDGETS_PER_SCENE = 24;
constexpr uint8_t  MAX_FOCUS_CHAIN       = 16;

// ── Scenes ───────────────────────────────────────────────────────
constexpr uint8_t  MAX_SCENE_STACK       = 6;
constexpr uint8_t  MAX_SCENES_REGISTERED = 12;

// ── Input ────────────────────────────────────────────────────────
constexpr uint8_t  MAX_KEY_BINDINGS      = 32;
constexpr uint16_t KEY_REPEAT_DELAY_MS   = 400;
constexpr uint16_t KEY_REPEAT_RATE_MS    = 80;

// ── Timing ───────────────────────────────────────────────────────
constexpr uint32_t TARGET_FPS            = 30;
constexpr uint32_t FRAME_TIME_MS         = 1000 / TARGET_FPS;

// ── Memory ───────────────────────────────────────────────────────
// Set to 1 to allocate large buffers (framebuffer, region sprites)
// in PSRAM. Requires ESP32-S3 with PSRAM enabled.
#ifndef CARDGFX_USE_PSRAM
#define CARDGFX_USE_PSRAM 1
#endif

} // namespace CardGFX

#endif // CARDGFX_CONFIG_H
