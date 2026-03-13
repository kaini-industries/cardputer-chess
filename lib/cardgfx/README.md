# CardGFX

**Lightweight sprite-based UI framework for M5Stack Cardputer (ESP32-S3 / ST7789 240×135)**

CardGFX provides a composable widget system, dirty-rect rendering, scene management, and keyboard input handling — everything you need to build interactive apps on the Cardputer without wrestling with low-level display code.

## Design Principles

- **Zero-flicker rendering** — Double-buffered sprites with dirty rectangle tracking. Only changed regions get pushed to the display.
- **Composable widgets** — Flat widget system with function-pointer dispatch. No deep class hierarchies. Each widget owns a rectangular region and handles its own drawing and input.
- **Memory-predictable** — Canvas pool with pre-allocated buffers. No dynamic allocation after init. Large buffers go to PSRAM, keeping SRAM free for application logic.

## Architecture

```
Application Layer           ← Your game / app code
Scene Manager               ← Screen states & transitions
Widget System               ← Composable UI components
Layout Engine + Theme       ← Positioning + visual config
Canvas Layer                ← Sprite-backed drawing surfaces
Input Manager               ← Keyboard event routing
Display HAL                 ← Wraps M5GFX / LovyanGFX
```

## Quick Start

```cpp
#include <cardgfx.h>
using namespace CardGFX;

class MyScene : public Scene {
public:
    MyScene() : Scene("main") {}
    Label hello;

    void setup() {
        hello.setText("Hello Cardputer!");
        hello.setBounds({10, 50, 220, 20});
        hello.setAlign(Label::Align::Center);
        hello.setScale(2);
        addWidget(&hello);
    }

    void onEnter() override {}
};

MyScene mainScene;

void setup() {
    CardGFX::init();
    mainScene.setup();
    CardGFX::scenes().registerScene(&mainScene);
    CardGFX::scenes().push(&mainScene);
}

void loop() {
    CardGFX::tick();
}
```

## Built-in Widgets

| Widget      | Description                              |
|-------------|------------------------------------------|
| `Label`     | Static/dynamic text with alignment       |
| `TextInput` | Keyboard text entry with cursor + blink  |
| `List`      | Scrollable vertical list with selection  |
| `Grid`      | 2D navigable grid (chessboard, menus)    |
| `Modal`     | Overlay dialog with up to 4 buttons      |
| `StatusBar` | Slim bar with left/center/right segments |
| `Bitmap`    | Static image (1-bit or RGB565)           |

## Theming

Three built-in themes: `Themes::Dark`, `Themes::Light`, `Themes::HighContrast`. Create custom themes as static const `Theme` structs.

```cpp
CardGFX::setTheme(Themes::Dark);
```

## Scene Management

Scenes use a push/pop stack. Pushing a modal pauses the scene underneath without destroying it.

```cpp
CardGFX::scenes().push(&gameScene);          // Enter game
CardGFX::scenes().push(&drawOfferModal);     // Overlay modal
CardGFX::scenes().pop();                     // Return to game
```

## Input

The `InputManager` generates structured `InputEvent`s with key codes, modifiers, and auto-repeat. Key bindings route shortcuts before the focus chain.

```cpp
CardGFX::input().bind(Key::ESCAPE, Mod::NONE, 1, []() {
    CardGFX::scenes().pop();
    return true;
});
```

### Arrow Key Mapping

The Cardputer has no hardware arrow keys. CardGFX automatically maps bare `;` `,` `.` `/` to UP, LEFT, DOWN, RIGHT at the framework level (`Scene::routeInput()`). This mapping is suppressed when the focused widget has `acceptsText()` set (e.g., `TextInput`), so those keys can be typed normally into text fields.

## Grid Widget

The `Grid` widget is the most powerful built-in. It handles cursor navigation, cell state flags (selected, highlighted, marked), and custom cell rendering via callback.

```cpp
Grid board;
board.setGridSize(8, 8);
board.setCellSize(15, 15);
board.setCellRenderer([](Canvas& c, uint8_t col, uint8_t row,
                         int16_t cx, int16_t cy, uint8_t cw, uint8_t ch,
                         Grid::CellState state, const Theme& theme,
                         void* ctx) {
    // Draw your cell content here
});
board.setOnAction([](uint8_t col, uint8_t row) {
    // Handle Enter/Space on a cell
});
```

## Memory Budget

| Allocation                  | Size    | Location |
|-----------------------------|---------|----------|
| Main framebuffer (240×135)  | ~63 KB  | PSRAM    |
| Widget scratch canvases     | ~8 KB   | SRAM     |
| Widget state + scenes       | ~3 KB   | SRAM     |
| Theme data                  | ~128 B  | Flash    |

## Installation

Copy the `cardgfx/` folder into your PlatformIO project's `lib/` directory, or symlink it. Requires `m5stack/M5Cardputer` as a dependency.

## File Structure

```
cardgfx/
├── library.json
├── src/
│   ├── cardgfx.h              # Single-include header
│   ├── cardgfx.cpp            # Main init/tick/shutdown
│   ├── cardgfx_config.h       # Compile-time constants
│   ├── cardgfx_hal.h/.cpp     # Display hardware abstraction
│   ├── cardgfx_canvas.h/.cpp  # Sprite-backed drawing surfaces
│   ├── cardgfx_theme.h        # Theme structs + built-ins
│   ├── cardgfx_layout.h/.cpp  # Box model + positioning
│   ├── cardgfx_input.h/.cpp   # Keyboard + event routing
│   ├── cardgfx_widget.h/.cpp  # Widget base + focus chain
│   ├── cardgfx_scene.h/.cpp   # Scene stack + transitions
│   └── widgets/
│       ├── label.h
│       ├── text_input.h
│       ├── list.h
│       ├── grid.h
│       ├── modal.h
│       ├── status_bar.h
│       └── bitmap.h
```

## License

MIT
