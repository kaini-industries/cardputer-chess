#include <Arduino.h>
#include <cardgfx.h>
#include "chess_scene.h"

using namespace CardGFX;

static ChessScene chessScene;

// =====================================================================
// Setup & Loop
// =====================================================================

void setup() {
    if (!CardGFX::init(1, 128)) {
        while (true) delay(1000);
    }

    chessScene.setup();
    CardGFX::scenes().registerScene(&chessScene);
    CardGFX::scenes().push(&chessScene);

    Serial.println("BOOT OK - Chess");
}

void loop() {
    CardGFX::tick();
}
