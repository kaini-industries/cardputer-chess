#ifndef CARDGFX_H
#define CARDGFX_H

/**
 * CardGFX - Lightweight sprite-based UI framework
 * for M5Stack Cardputer (ESP32-S3 / ST7789 240x135)
 *
 * Single-include header. Pull in everything with:
 *   #include <cardgfx.h>
 */

#include "cardgfx_config.h"
#include "cardgfx_hal.h"
#include "cardgfx_canvas.h"
#include "cardgfx_theme.h"
#include "cardgfx_layout.h"
#include "cardgfx_input.h"
#include "cardgfx_widget.h"
#include "cardgfx_scene.h"

// Built-in widgets
#include "widgets/label.h"
#include "widgets/text_input.h"
#include "widgets/list.h"
#include "widgets/grid.h"
#include "widgets/modal.h"
#include "widgets/status_bar.h"
#include "widgets/bitmap.h"

namespace CardGFX {

/**
 * Initialize the entire CardGFX system.
 * Call once in setup() before using any other CardGFX functions.
 *
 * @param rotation Display rotation (0-3). Default 1 = landscape.
 * @param brightness Backlight brightness (0-255). Default 128.
 * @return true if initialization succeeded.
 */
bool init(uint8_t rotation = 1, uint8_t brightness = 128);

/**
 * Tick the framework. Call once per loop() iteration.
 * Handles timing, input polling, scene updates, and rendering.
 *
 * @return Delta time in milliseconds since last tick.
 */
uint32_t tick();

/**
 * Shutdown and release resources.
 */
void shutdown();

/**
 * Get the active scene manager instance.
 */
SceneManager& scenes();

/**
 * Get the input manager instance.
 */
InputManager& input();

/**
 * Get the active theme.
 */
const Theme& theme();

/**
 * Set the active theme.
 */
void setTheme(const Theme& theme);

} // namespace CardGFX

#endif // CARDGFX_H
