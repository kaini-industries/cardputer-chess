#include "cardgfx_hal.h"
#include <M5Cardputer.h>

namespace CardGFX {
namespace HAL {

static bool s_initialized = false;

bool init(uint8_t rotation, uint8_t brightness) {
    if (s_initialized) return true;

    // M5Cardputer auto-initializes in M5.begin(), but we
    // configure display settings here for CardGFX's needs.
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5Cardputer.begin(cfg, true);  // true = enable display

    M5Cardputer.Display.setRotation(rotation);
    M5Cardputer.Display.setBrightness(brightness);
    M5Cardputer.Display.setColorDepth(COLOR_DEPTH);
    M5Cardputer.Display.setSwapBytes(true);

    // Start with a black screen
    M5Cardputer.Display.fillScreen(0x0000);

    // Allow display controller to fully stabilize before pushImage calls
    delay(50);

    s_initialized = true;
    return true;
}

void pushRegion(int16_t x, int16_t y, uint16_t w, uint16_t h,
                const uint16_t* buffer) {
    if (!s_initialized || !buffer) return;

    // pushImage handles the SPI transaction internally.
    // The buffer is RGB565, row-major.
    M5Cardputer.Display.pushImage(x, y, w, h, buffer);
}

void fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h,
              uint16_t color) {
    if (!s_initialized) return;
    M5Cardputer.Display.fillRect(x, y, w, h, color);
}

void clear(uint16_t color) {
    if (!s_initialized) return;
    M5Cardputer.Display.fillScreen(color);
}

void setBrightness(uint8_t brightness) {
    if (!s_initialized) return;
    M5Cardputer.Display.setBrightness(brightness);
}

void* driverHandle() {
    return &M5Cardputer.Display;
}

} // namespace HAL
} // namespace CardGFX
