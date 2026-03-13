#include <Arduino.h>
#include <cardgfx.h>
#include "cardgfx_test.h"
#include "visual_test_scene.h"

using namespace CardGFX;

static CardGFXTest::VisualTestScene s_visualScene;

void setup() {
    // Initialize CardGFX (display + input + framebuffer)
    if (!CardGFX::init(1, 128)) {
        Serial.begin(115200);
        Serial.println("CardGFX init FAILED");
        while (true) delay(1000);
    }

    Serial.println();
    Serial.println("========================================");
    Serial.println("  CardGFX Test Suite");
    Serial.println("========================================");
    Serial.println();

    // Phase 1: Run automated assertion tests
    uint16_t failures = CardGFXTest::runAllTests();

    Serial.println();
    if (failures == 0) {
        Serial.println("All assertion tests passed. Starting visual tests...");
    } else {
        Serial.printf("%u assertion test(s) FAILED. Starting visual tests anyway...\n", failures);
    }
    Serial.println();

    // Phase 2: Launch visual diagnostic scene
    CardGFX::scenes().registerScene(&s_visualScene);
    CardGFX::scenes().push(&s_visualScene);

    Serial.println("Visual test scene loaded. Use n=next, p=prev, ESC=exit.");
}

void loop() {
    CardGFX::tick();
}
