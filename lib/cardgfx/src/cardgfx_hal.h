#ifndef CARDGFX_HAL_H
#define CARDGFX_HAL_H

#include "cardgfx_config.h"
#include <cstdint>

namespace CardGFX {

/**
 * Hardware Abstraction Layer for the display.
 *
 * This is the ONLY module that touches the display driver directly
 * (M5GFX / LovyanGFX under the hood via M5Cardputer). Everything
 * above works with pixel buffers and never talks to hardware.
 *
 * Porting to a different board means swapping this one file.
 */
namespace HAL {

    /**
     * Initialize the display hardware.
     * @param rotation 0-3 rotation value. 1 = landscape (default).
     * @param brightness Backlight PWM (0-255).
     * @return true on success.
     */
    bool init(uint8_t rotation = 1, uint8_t brightness = 128);

    /**
     * Push a rectangular pixel buffer region to the physical display.
     * This is the fundamental blit operation — everything renders
     * through this call.
     *
     * @param x      Screen X of the top-left corner.
     * @param y      Screen Y of the top-left corner.
     * @param w      Width of the region in pixels.
     * @param h      Height of the region in pixels.
     * @param buffer Pointer to RGB565 pixel data (row-major, w*h entries).
     */
    void pushRegion(int16_t x, int16_t y, uint16_t w, uint16_t h,
                    const uint16_t* buffer);

    /**
     * Fill a screen rectangle with a solid color.
     * Useful for fast clears without going through a canvas.
     */
    void fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                  uint16_t color);

    /**
     * Fill the entire screen with a color.
     */
    void clear(uint16_t color = 0x0000);

    /**
     * Set backlight brightness.
     * @param brightness 0 (off) to 255 (max).
     */
    void setBrightness(uint8_t brightness);

    /**
     * Get the underlying display driver pointer.
     * Use sparingly — prefer the abstracted calls.
     * Returns void* to avoid leaking driver types into the API.
     */
    void* driverHandle();

    /**
     * Convert 8-bit RGB to RGB565.
     */
    inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    /**
     * Swap byte order for SPI transmission (big-endian displays).
     */
    inline uint16_t swapBytes(uint16_t color) {
        return (color >> 8) | (color << 8);
    }

} // namespace HAL
} // namespace CardGFX

#endif // CARDGFX_HAL_H
