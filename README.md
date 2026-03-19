# Cardputer ADV Chess

A chess game for the M5Stack Cardputer Advance featuring local pass-and-play, AI opponent, and wireless multiplayer via ESP-NOW. Built with the [CardGFX](lib/cardgfx/README.md) UI framework.

## Features

- Full chess rules: castling, en passant, pawn promotion, check/checkmate/stalemate detection
- 50-move rule and insufficient material draw detection
- Three game modes: local pass-and-play, vs AI, and wireless multiplayer
- AI opponent with three difficulty levels (Easy, Medium, Hard)
- Wireless multiplayer over ESP-NOW (no WiFi network required)
- Animated piece movement between turns
- Move history panel with standard algebraic notation (SAN)
- Persistent game saving — games auto-save after each move and survive power cycles
- Undo support (local and AI modes)
- Resign support (online mode)
- Status bar showing current turn, move number, and check/game-over indicators

## Game Modes

On launch, a lobby screen presents the available options. If a saved game exists, a **Resume** button appears at the top of the menu.

| Mode | Description |
|------|-------------|
| **Resume** | Continue a previously saved game (only shown when a save exists). |

| Mode | Description |
|------|-------------|
| **Local** | Pass-and-play on a single device. The board auto-rotates after each move so the current player's pieces are always at the bottom. |
| **vs AI** | Play against the computer. Choose difficulty (Easy, Medium, or Hard) and your color (White or Black). |
| **Host** | Broadcast a game over ESP-NOW and wait for an opponent to join. Host plays White. |
| **Join** | Scan for a nearby host and connect. Joiner plays Black. |

Starting a new game (Local, vs AI, Host, or Join) clears any existing save. Network games are not saved since the connection cannot survive a power cycle.

## AI Opponent

| Difficulty | Search Depth | Time Limit | Notes |
|------------|-------------|------------|-------|
| **Easy** | 2 | 200ms | 30% chance to pick a random legal move |
| **Medium** | 4 | 1s | Standard play |
| **Hard** | 6+ | 3s | Iterative deepening for maximum depth within time |

The AI uses alpha-beta pruning with move ordering (captures scored by MVV-LVA, promotions prioritized) and quiescence search to avoid the horizon effect. Positional evaluation uses piece-square tables.

## Wireless Multiplayer

ESP-NOW is a connectionless WiFi peer-to-peer protocol — no router or network setup needed. Both devices just need to be within WiFi range (~30m indoors). Pairing times out after 60 seconds.

The host broadcasts a discovery message every 500ms. When a joiner connects, both devices exchange handshake messages and the game begins. Moves are sent with sequence numbers and acknowledged to ensure reliable delivery.

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
| **U** | Undo last move (local/AI only) |
| **N** | Return to lobby with confirmation (local/AI only) |
| **R** | Resign with confirmation (online only) |

> The Cardputer has no hardware arrow keys. The `;` `,` `.` `/` keys are mapped to arrows at the framework level, so they work as directional controls in all scenes.

### In Lobby

| Key | Action |
|-----|--------|
| **,** **/** | Navigate menu buttons |
| **Enter** | Select |
| **Esc** | Cancel hosting/joining and return to menu |

### Promotion

When a pawn reaches the back rank, a dialog appears with four choices: Queen, Knight, Rook, Bishop. Use **,** **/** to navigate, **Enter** to confirm.

### Game Over

When checkmate, stalemate, 50-move rule, or insufficient material is detected, a dialog offers:

- **New Game** -- return to the lobby
- **View** -- dismiss the dialog and review the position (press **U** to undo moves)

## Installation

### M5 Burner (easiest)

1. Open [M5Burner](https://docs.m5stack.com/en/download) and filter by **Cardputer**
2. Find **Cardputer ADV Chess** and click **Burn**

### Build from Source

**Prerequisites:** [PlatformIO](https://platformio.org/) (CLI or VSCode extension) and an M5Stack Cardputer Advance.

```bash
# Build
pio run

# Upload to device
pio run --target upload

# Open serial monitor (115200 baud)
pio device monitor
```

The build automatically generates a merged M5Burner-compatible binary at `firmware/cardputer-chess-<version>.bin`.

## Testing

The project includes a CardGFX framework test suite that runs on-device:

```bash
# Build and upload tests
pio run -e test-cardputer --target upload

# View results on serial monitor
pio device monitor
```

Tests cover focus management, widget rendering, scene lifecycle, and widget functionality. After automated tests complete, interactive visual diagnostics are available (press **N**/**P** to navigate, **Esc** to exit).

## Project Structure

```
.
├── src/
│   ├── main.cpp                # Entry point
│   ├── lobby_scene.h/.cpp      # Pre-game lobby (mode select, ESP-NOW pairing)
│   ├── chess_scene.h/.cpp      # Game UI (board, widgets, input, animation)
│   ├── chess_types.h           # Piece, Square, Move data types
│   ├── chess_board.h/.cpp      # Board state, make/unmake move
│   ├── chess_rules.h/.cpp      # Move generation, check detection
│   ├── chess_ai.h/.cpp         # AI opponent (alpha-beta with iterative deepening)
│   ├── chess_storage.h/.cpp    # Persistent game save/load (ESP32 NVS)
│   ├── chess_net_protocol.h    # Network message types and protocol
│   └── esp_now_transport.h/.cpp  # ESP-NOW send/receive layer
├── lib/
│   ├── cardgfx/                # CardGFX UI framework (see its README)
│   └── cardgfx_test/           # CardGFX test suite
├── test/
│   └── test_main.cpp           # Test runner entry point
├── firmware/                   # M5Burner merged binaries (build artifact)
├── generate_m5burner_bin.py    # Post-build script for M5Burner binary
├── platformio.ini              # Build configuration
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
