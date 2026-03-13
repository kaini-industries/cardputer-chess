#ifndef CARDGFX_THEME_H
#define CARDGFX_THEME_H

#include "cardgfx_hal.h"
#include <cstdint>

namespace CardGFX {

/**
 * Theme: a plain data struct that widgets reference for colors,
 * spacing, and typography settings. No runtime polymorphism.
 *
 * Create custom themes as static const structs.
 */
struct Theme {
    // ── Colors ───────────────────────────────────────────────────
    uint16_t bgPrimary;       // Main background
    uint16_t bgSecondary;     // Panels, cards, sidebars
    uint16_t bgTertiary;      // Input fields, wells

    uint16_t fgPrimary;       // Main text
    uint16_t fgSecondary;     // Muted / secondary text
    uint16_t fgDisabled;      // Disabled / inactive text

    uint16_t accent;          // Primary action / highlight color
    uint16_t accentActive;    // Pressed / selected state
    uint16_t accentMuted;     // Subtle accent tint

    uint16_t success;         // Positive / confirmation
    uint16_t warning;         // Caution
    uint16_t error;           // Error / danger

    uint16_t border;          // Default borders
    uint16_t borderFocus;     // Focused element border
    uint16_t divider;         // Subtle separator lines

    // ── Grid-specific (for chessboard-like widgets) ──────────────
    uint16_t gridCellA;       // Alternating cell color A
    uint16_t gridCellB;       // Alternating cell color B
    uint16_t gridCursor;      // Cursor / selection highlight
    uint16_t gridHighlight;   // Secondary highlight (valid moves, etc.)

    // ── Typography ───────────────────────────────────────────────
    // Scale multiplier for the built-in 5x7 font.
    // 1 = tiny (5x7), 2 = normal (10x14), 3 = large (15x21)
    uint8_t fontScaleSm;
    uint8_t fontScaleMd;
    uint8_t fontScaleLg;

    // ── Spacing ──────────────────────────────────────────────────
    uint8_t padding;          // Inner padding for widgets
    uint8_t margin;           // Outer margin between widgets
    uint8_t borderWidth;      // Default border thickness
    uint8_t cornerRadius;     // Rounded corner radius
};

// ── Built-in Themes ──────────────────────────────────────────────

namespace Themes {

/**
 * Dark theme — high contrast on dark background.
 * Well-suited for the Cardputer's small TFT.
 */
inline const Theme Dark = {
    // Background
    .bgPrimary    = HAL::rgb565(20,  20,  30),   // Near-black blue
    .bgSecondary  = HAL::rgb565(30,  30,  45),   // Dark panel
    .bgTertiary   = HAL::rgb565(40,  40,  58),   // Input well

    // Foreground
    .fgPrimary    = HAL::rgb565(230, 230, 240),  // Bright white
    .fgSecondary  = HAL::rgb565(160, 160, 180),  // Muted
    .fgDisabled   = HAL::rgb565(80,  80,  100),  // Dim

    // Accent
    .accent       = HAL::rgb565(233, 69,  96),   // Red-pink
    .accentActive = HAL::rgb565(255, 100, 120),  // Brighter on press
    .accentMuted  = HAL::rgb565(80,  30,  40),   // Subtle tint

    // Status
    .success      = HAL::rgb565(46,  204, 113),
    .warning      = HAL::rgb565(241, 196, 15),
    .error        = HAL::rgb565(231, 76,  60),

    // Borders
    .border       = HAL::rgb565(60,  60,  80),
    .borderFocus  = HAL::rgb565(233, 69,  96),
    .divider      = HAL::rgb565(45,  45,  65),

    // Grid
    .gridCellA    = HAL::rgb565(180, 140, 100),  // Light wood
    .gridCellB    = HAL::rgb565(100, 70,  50),   // Dark wood
    .gridCursor   = HAL::rgb565(255, 255, 80),   // Bright yellow
    .gridHighlight= HAL::rgb565(80,  180, 80),   // Green for valid moves

    // Typography
    .fontScaleSm  = 1,
    .fontScaleMd  = 1,   // On 135px tall screen, scale 1 is practical
    .fontScaleLg  = 2,

    // Spacing
    .padding      = 2,
    .margin       = 2,
    .borderWidth  = 1,
    .cornerRadius = 2,
};

/**
 * Light theme — bright background, dark text.
 */
inline const Theme Light = {
    .bgPrimary    = HAL::rgb565(240, 240, 245),
    .bgSecondary  = HAL::rgb565(255, 255, 255),
    .bgTertiary   = HAL::rgb565(225, 225, 235),

    .fgPrimary    = HAL::rgb565(30,  30,  40),
    .fgSecondary  = HAL::rgb565(100, 100, 115),
    .fgDisabled   = HAL::rgb565(180, 180, 190),

    .accent       = HAL::rgb565(15,  52,  96),
    .accentActive = HAL::rgb565(30,  80,  140),
    .accentMuted  = HAL::rgb565(200, 215, 235),

    .success      = HAL::rgb565(39,  174, 96),
    .warning      = HAL::rgb565(211, 168, 10),
    .error        = HAL::rgb565(192, 57,  43),

    .border       = HAL::rgb565(200, 200, 210),
    .borderFocus  = HAL::rgb565(15,  52,  96),
    .divider      = HAL::rgb565(220, 220, 230),

    .gridCellA    = HAL::rgb565(238, 238, 210),
    .gridCellB    = HAL::rgb565(118, 150, 86),
    .gridCursor   = HAL::rgb565(255, 215, 0),
    .gridHighlight= HAL::rgb565(130, 200, 100),

    .fontScaleSm  = 1,
    .fontScaleMd  = 1,
    .fontScaleLg  = 2,

    .padding      = 2,
    .margin       = 2,
    .borderWidth  = 1,
    .cornerRadius = 2,
};

/**
 * High-contrast theme — maximum readability.
 */
inline const Theme HighContrast = {
    .bgPrimary    = HAL::rgb565(0,   0,   0),
    .bgSecondary  = HAL::rgb565(10,  10,  10),
    .bgTertiary   = HAL::rgb565(25,  25,  25),

    .fgPrimary    = HAL::rgb565(255, 255, 255),
    .fgSecondary  = HAL::rgb565(200, 200, 200),
    .fgDisabled   = HAL::rgb565(100, 100, 100),

    .accent       = HAL::rgb565(0,   200, 255),
    .accentActive = HAL::rgb565(100, 230, 255),
    .accentMuted  = HAL::rgb565(0,   60,  80),

    .success      = HAL::rgb565(0,   255, 0),
    .warning      = HAL::rgb565(255, 255, 0),
    .error        = HAL::rgb565(255, 0,   0),

    .border       = HAL::rgb565(255, 255, 255),
    .borderFocus  = HAL::rgb565(0,   200, 255),
    .divider      = HAL::rgb565(128, 128, 128),

    .gridCellA    = HAL::rgb565(255, 255, 255),
    .gridCellB    = HAL::rgb565(0,   0,   0),
    .gridCursor   = HAL::rgb565(0,   200, 255),
    .gridHighlight= HAL::rgb565(0,   255, 0),

    .fontScaleSm  = 1,
    .fontScaleMd  = 1,
    .fontScaleLg  = 2,

    .padding      = 2,
    .margin       = 2,
    .borderWidth  = 2,
    .cornerRadius = 0,
};

} // namespace Themes
} // namespace CardGFX

#endif // CARDGFX_THEME_H
