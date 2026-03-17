# Cardputer ADV Chess

A two-player chess game for the M5Stack Cardputer Advance with local pass-and-play and wireless multiplayer via ESP-NOW, built with the [CardGFX](lib/cardgfx/README.md) UI framework.

## Features

- Full chess rules: castling, en passant, pawn promotion, check/checkmate/stalemate detection
- 50-move rule and insufficient material draw detection
- Wireless multiplayer over ESP-NOW (no WiFi network required)
- Local pass-and-play with automatic board rotation per turn
- AI opponent with three difficulty levels (Easy, Medium, Hard)
- Animated piece movement between turns
- Move history panel with standard algebraic notation (SAN)
- Undo support (local mode)
- Status bar showing current turn, move number, and check/game-over indicators

## Multiplayer

On launch, a lobby screen presents three options:

| Mode | Description |
|------|-------------|
| **Local** | Pass-and-play on a single device. The board auto-rotates after each move so the current player's pieces are always at the bottom. |
| **vs AI** | Play against the computer. Choose difficulty (Easy/Medium/Hard) and your color. |
| **Host** | Broadcast a game over ESP-NOW and wait for an opponent to join. Host plays White. |
| **Join** | Scan for a nearby host and connect. Joiner plays Black. |

ESP-NOW is a connectionless WiFi peer-to-peer protocol — no router or network setup needed. Both devices just need to be within WiFi range (~30m indoors). Pairing times out after 60 seconds.

## Controls

### In Game

| Key | Action |
|-----|--------|
| **;** or **FN + ;** | Move cursor up |
| **.** or **FN + .** | Move cursor down |
| **,** or **FN + ,** | Move cursor left |
| **/** or **FN + /** | Move cursor right |
| **Enter** or **Space** | Select piece / confirm move |
| **Esc** (side button) | Deselect piece / cancel |
| **U** | Undo last move |
| **N** | Return to lobby (with confirmation) |

> The Cardputer has no hardware arrow keys. The `;` `,` `.` `/` keys are mapped to arrows at the framework level, so they work as directional controls in all scenes.

### In Lobby

| Key | Action |
|-----|--------|
| **,** **/** | Navigate menu buttons |
| **Enter** | Select |
| **Esc** | Cancel hosting/joining and return to menu |

### Promotion

When a pawn reaches the back rank, a dialog appears with four buttons:

- **Queen** -- promote to queen
- **Knight** -- promote to knight
- **Rook** -- promote to rook
- **Bishop** -- promote to bishop

Use **,** **/** (left/right) to navigate between buttons, **Enter** to confirm.

### Game Over

When checkmate, stalemate, 50-move rule, or insufficient material is detected, a dialog offers:

- **New Game** -- return to the lobby
- **View** -- dismiss the dialog and review the position (press **U** to undo moves)

## Installation

### M5 Burner (easiest)

1. Open [M5Burner](https://docs.m5stack.com/en/download) and filter by **Cardputer**
2. Find **Cardputer ADV Chess** and click **Burn**

### Build from Source

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
│   ├── lobby_scene.h/.cpp    # Pre-game lobby (mode select, ESP-NOW pairing)
│   ├── chess_scene.h/.cpp    # Game UI scene (board, widgets, input)
│   ├── chess_types.h         # Piece, Square, Move data types
│   ├── chess_board.h/.cpp    # Board state, make/unmake move
│   ├── chess_rules.h/.cpp    # Move generation, check detection
│   ├── chess_ai.h/.cpp       # AI opponent (alpha-beta with iterative deepening)
│   ├── chess_net_protocol.h  # Network message types and protocol
│   └── esp_now_transport.h/.cpp  # ESP-NOW send/receive layer
├── lib/
│   └── cardgfx/              # CardGFX UI framework (see its README)
├── firmware/                   # M5Burner merged binaries (build artifact)
├── generate_m5burner_bin.py   # Post-build script for M5Burner binary
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
