# Cardputer ADV Chess

A two-player pass-and-play chess game for the M5Stack Cardputer Advance, built with the [CardGFX](lib/cardgfx/README.md) UI framework.

## Features

- Full chess rules: castling, en passant, pawn promotion, check/checkmate/stalemate detection
- 50-move rule and insufficient material draw detection
- Move history panel with scrollable list (captures shown with "x" notation)
- Undo support
- Status bar showing current turn, move number, and check/game-over indicators

## Controls

| Key | Action |
|-----|--------|
| **;** or **FN + ;** | Move cursor up |
| **.** or **FN + .** | Move cursor down |
| **,** or **FN + ,** | Move cursor left |
| **/** or **FN + /** | Move cursor right |
| **Enter** or **Space** | Select piece / confirm move |
| **Esc** (side button) | Deselect piece / cancel |
| **U** | Undo last move |
| **N** | New game (with confirmation) |

> The Cardputer has no hardware arrow keys. The `;` `,` `.` `/` keys are mapped to arrows at the framework level, so they work as directional controls in all scenes.

### Promotion

When a pawn reaches the back rank, a dialog appears with four buttons:

- **Queen** -- promote to queen
- **Knight** -- promote to knight
- **Rook** -- promote to rook
- **Bishop** -- promote to bishop

Use **,** / **/** (left/right) to navigate between buttons, **Enter** to confirm.

### Game Over

When checkmate, stalemate, 50-move rule, or insufficient material is detected, a dialog offers:

- **New Game** -- reset the board
- **View** -- dismiss the dialog and review the position (press **U** to undo moves)

## Screen Layout

```
+---------- StatusBar (240x12) ----------+
|  "White"     "Move 1"       "Check!"  |
+--- Board (120x120) --+- Side (120x123) -+
|                       | Moves (label)    |
|    8x8 grid           | Move history     |
|    15px cells          |   (scrollable)   |
|                       |                  |
|                       | [U]ndo [N]ew     |
+-----------------------+------------------+
```

## Building

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VSCode extension)
- M5Stack Cardputer Advance

### Build & Upload

```bash
# Build
pio run

# Upload to device
pio run --target upload

# Open serial monitor (115200 baud)
pio device monitor
```

## Project Structure

```
.
├── src/
│   ├── main.cpp              # Entry point
│   ├── chess_scene.h/.cpp    # UI scene (board, widgets, input)
│   ├── chess_types.h         # Piece, Square, Move data types
│   ├── chess_board.h/.cpp    # Board state, make/unmake move
│   └── chess_rules.h/.cpp    # Move generation, check detection
├── lib/
│   └── cardgfx/              # CardGFX UI framework (see its README)
├── platformio.ini             # Build configuration
└── README.md
```

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| [M5Unified](https://github.com/m5stack/M5Unified) | ^0.2.10 | Unified hardware abstraction |
| [M5Cardputer](https://github.com/m5stack/M5Cardputer) | ^1.1.1 | Cardputer keyboard and hardware |
| [M5GFX](https://github.com/m5stack/M5GFX) | ^0.2.16 | Graphics library |

## License

MIT
