#include <Arduino.h>
#include <cardgfx.h>
#include "chess_scene.h"
#include "lobby_scene.h"

using namespace CardGFX;

static ChessScene chessScene;
static LobbyScene lobbyScene;

// =====================================================================
// Setup & Loop
// =====================================================================

void setup() {
    if (!CardGFX::init(1, 128)) {
        while (true) delay(1000);
    }

    chessScene.setup();
    lobbyScene.setup(&chessScene);

    CardGFX::scenes().registerScene(&chessScene);
    CardGFX::scenes().registerScene(&lobbyScene);
    CardGFX::scenes().push(&lobbyScene);

    Serial.println("BOOT OK - Chess");
}

void loop() {
    CardGFX::tick();
}
