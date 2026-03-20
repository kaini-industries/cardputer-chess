# Cardputer ADV Chess

A chess game for the M5Stack Cardputer Advance featuring multiple variants, local pass-and-play, AI opponent, wireless multiplayer via ESP-NOW, and a built-in puzzle trainer. Built with the [CardGFX](lib/cardgfx/README.md) UI framework.

## Features

- **Three chess variants:** Standard, Atomic, and Chess960 (Fischer Random)
- Full chess rules: castling, en passant, pawn promotion, check/checkmate/stalemate detection
- 50-move rule and insufficient material draw detection
- **Time controls:** No timer, Bullet (1+0), Blitz (3+2, 5+3), Rapid (10+0)
- Four game modes: local pass-and-play, vs AI, wireless multiplayer, and puzzles
- AI opponent with three difficulty levels (Easy, Medium, Hard)
- Wireless multiplayer over ESP-NOW (no WiFi network required)
- **Puzzle trainer** with mate-in-1, mate-in-2, and tactic puzzles with progress tracking
- Animated piece movement between turns
- Move history panel with standard algebraic notation (SAN)
- Move review mode — step through past moves with arrow keys
- Persistent game saving — games auto-save after each move and survive power cycles
- Undo support (local and AI modes)
- Resign support (online mode)
- Status bar showing current turn, move number, clock times, and check/game-over indicators

## Chess Variants

| Variant | Description |
|---------|-------------|
| **Standard** | Classic chess rules. |
| **Atomic** | Captures trigger explosions that destroy all non-pawn pieces in a 3x3 area. Kings cannot capture. Adjacent kings nullify check. Destroying the opponent's king by explosion wins immediately. |
| **Chess960** | Starting positions are randomized (960 possible arrangements). Castling rules are generalized to work with any initial rook/king placement. |

Variant and time control are selected before each game from the lobby.

## Game Modes

On launch, a lobby screen presents the available options. If a saved game exists, a **Resume** button appears at the top of the menu.

| Mode | Description |
|------|-------------|
| **Resume** | Continue a previously saved game (only shown when a save exists). |
| **Local** | Pass-and-play on a single device. The board auto-rotates after each move so the current player's pieces are always at the bottom. |
| **vs AI** | Play against the computer. Choose difficulty (Easy, Medium, or Hard) and your color (White or Black). |
| **Host** | Broadcast a game over ESP-NOW and wait for an opponent to join. Host plays White. |
| **Join** | Scan for a nearby host and connect. Joiner plays Black. |
| **Puzzles** | Solve chess puzzles. Choose a category (Mate in 1, Mate in 2, Tactics) or play the next unsolved puzzle. |

Starting a new game (Local, vs AI, Host, or Join) clears any existing save. Network games are not saved since the connection cannot survive a power cycle.

## Puzzles

The puzzle trainer includes mate-in-1, mate-in-2, and tactic puzzles. Each puzzle presents a position where you must find the correct move (or sequence of moves).

- **Hints:** Press **H** to reveal progressive hints (highlighted squares)
- **Progress tracking:** Solved puzzles are saved to flash and persist across power cycles
- **Auto-play:** After your correct move, the opponent's response plays automatically before your next move
- **Categories:** Filter by puzzle type or play the next unsolved puzzle

## AI Opponent

| Difficulty | Search Depth | Time Limit | Notes |
|------------|-------------|------------|-------|
| **Easy** | 2 | 200ms | 30% chance to pick a random legal move |
| **Medium** | 4 | 1s | Standard play |
| **Hard** | 6+ | 3s | Iterative deepening for maximum depth within time |

The AI uses alpha-beta pruning with move ordering (captures scored by MVV-LVA, promotions prioritized) and quiescence search to avoid the horizon effect. Positional evaluation uses piece-square tables. In Atomic mode, the AI additionally penalizes having friendly pieces adjacent to its own king (explosion risk).

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
| **Esc** (side button) | Deselect piece / cancel / back |
| **U** | Undo last move (local/AI only) |
| **N** | Return to lobby with confirmation (local/AI only) |
| **R** | Resign with confirmation (online only) |
| **H** | Show hint (puzzle mode only) |
| **Left / Right** | Step through move history (review mode) |

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

When checkmate, stalemate, 50-move rule, insufficient material, or king explosion (Atomic) is detected, a dialog offers:

- **New Game** -- return to the lobby
- **View** -- dismiss the dialog and review the position (press **U** to undo moves)

In puzzle mode, the dialog offers **Next** (advance to the next puzzle) and **Menu** (return to lobby).

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
│   ├── lobby_scene.h/.cpp      # Pre-game lobby (mode/variant/time select, ESP-NOW pairing)
│   ├── chess_scene.h/.cpp      # Game UI (board, widgets, input, animation)
│   ├── chess_types.h           # Piece, Square, Move, Variant, TimeControl types
│   ├── chess_board.h/.cpp      # Board state, make/unmake move, atomic explosions
│   ├── chess_rules.h/.cpp      # Move generation, check detection, game-end conditions
│   ├── chess_ai.h/.cpp         # AI opponent (alpha-beta with iterative deepening)
│   ├── chess_opening_book.h/.cpp # Opening book for AI
│   ├── chess_zobrist.h/.cpp    # Zobrist hashing for position identification
│   ├── chess960.h/.cpp         # Chess960 position generation
│   ├── chess_storage.h/.cpp    # Persistent game save/load (ESP32 NVS)
│   ├── chess_net_protocol.h    # Network message types and protocol
│   ├── esp_now_transport.h/.cpp  # ESP-NOW send/receive layer
│   ├── puzzle_data.h/.cpp      # Puzzle database and loader
│   └── puzzle_storage.h/.cpp   # Puzzle progress persistence
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
