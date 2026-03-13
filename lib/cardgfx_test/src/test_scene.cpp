#include "cardgfx_test.h"

using namespace CardGFX;

namespace CardGFXTest {

// ── Helper: scene that tracks lifecycle calls ────────────────────

class TestScene : public Scene {
public:
    TestScene(const char* name) : Scene(name) {}

    uint8_t enterCount = 0;
    uint8_t exitCount = 0;
    uint8_t tickCount = 0;
    bool lastEntered = false;
    bool lastExited = false;

    void onEnter() override {
        enterCount++;
        lastEntered = true;
        lastExited = false;
    }
    void onExit() override {
        exitCount++;
        lastExited = true;
        lastEntered = false;
    }
    void onTick(uint32_t dt_ms) override {
        tickCount++;
    }

    void reset() {
        enterCount = 0;
        exitCount = 0;
        tickCount = 0;
        lastEntered = false;
        lastExited = false;
    }
};

// ── B1: Push calls onEnter, second push calls onExit on first ───

static void testB1_PushCallsLifecycle() {
    TEST_BEGIN("B1: Push calls onEnter/onExit correctly");

    SceneManager sm;

    TestScene sceneA("A");
    TestScene sceneB("B");

    sm.push(&sceneA);
    TEST_ASSERT(sceneA.enterCount == 1, "sceneA.onEnter should be called on push");
    TEST_ASSERT(sceneA.exitCount == 0, "sceneA.onExit should not be called yet");
    TEST_ASSERT(sm.active() == &sceneA, "active should be sceneA");
    TEST_ASSERT(sm.depth() == 1, "depth should be 1");

    sm.push(&sceneB);
    TEST_ASSERT(sceneA.exitCount == 1, "sceneA.onExit should be called when B pushed");
    TEST_ASSERT(sceneB.enterCount == 1, "sceneB.onEnter should be called on push");
    TEST_ASSERT(sm.active() == &sceneB, "active should be sceneB");
    TEST_ASSERT(sm.depth() == 2, "depth should be 2");

    TEST_END();
}

// ── B2: Pop calls onExit on top, onEnter on uncovered ───────────

static void testB2_PopCallsLifecycle() {
    TEST_BEGIN("B2: Pop calls onExit/onEnter correctly");

    SceneManager sm;
    TestScene sceneA("A");
    TestScene sceneB("B");

    sm.push(&sceneA);
    sm.push(&sceneB);
    sceneA.reset();
    sceneB.reset();

    Scene* popped = sm.pop();
    TEST_ASSERT(popped == &sceneB, "popped scene should be B");
    TEST_ASSERT(sceneB.exitCount == 1, "sceneB.onExit should be called on pop");
    TEST_ASSERT(sceneA.enterCount == 1, "sceneA.onEnter should be called when uncovered");
    TEST_ASSERT(sm.active() == &sceneA, "active should be sceneA after pop");
    TEST_ASSERT(sm.depth() == 1, "depth should be 1 after pop");

    TEST_END();
}

// ── B3: Replace calls onExit on old, onEnter on new ─────────────

static void testB3_ReplaceCallsLifecycle() {
    TEST_BEGIN("B3: Replace calls lifecycle correctly");

    SceneManager sm;
    TestScene sceneA("A");
    TestScene sceneB("B");

    sm.push(&sceneA);
    sceneA.reset();

    sm.replace(&sceneB);
    TEST_ASSERT(sceneA.exitCount == 1, "sceneA.onExit should be called on replace");
    TEST_ASSERT(sceneB.enterCount == 1, "sceneB.onEnter should be called on replace");
    TEST_ASSERT(sm.active() == &sceneB, "active should be sceneB after replace");
    TEST_ASSERT(sm.depth() == 1, "depth should still be 1 after replace");

    TEST_END();
}

// ── B4: Multiple push/pop sequence ───────────────────────────────

static void testB4_MultiplePushPop() {
    TEST_BEGIN("B4: Multiple push/pop sequence");

    SceneManager sm;
    TestScene sceneA("A");
    TestScene sceneB("B");
    TestScene sceneC("C");

    sm.push(&sceneA);
    sm.push(&sceneB);
    sm.push(&sceneC);
    TEST_ASSERT(sm.depth() == 3, "depth should be 3");
    TEST_ASSERT(sm.active() == &sceneC, "active should be C");
    TEST_ASSERT(sceneA.exitCount == 1, "A exited once (when B pushed)");
    TEST_ASSERT(sceneB.exitCount == 1, "B exited once (when C pushed)");

    sm.pop(); // pop C
    TEST_ASSERT(sm.depth() == 2, "depth should be 2 after pop C");
    TEST_ASSERT(sm.active() == &sceneB, "active should be B");
    TEST_ASSERT(sceneC.exitCount == 1, "C exited once");
    TEST_ASSERT(sceneB.enterCount == 2, "B entered twice (initial + uncovered)");

    sm.pop(); // pop B
    TEST_ASSERT(sm.depth() == 1, "depth should be 1 after pop B");
    TEST_ASSERT(sm.active() == &sceneA, "active should be A");
    TEST_ASSERT(sceneA.enterCount == 2, "A entered twice (initial + uncovered)");

    sm.pop(); // pop A
    TEST_ASSERT(sm.depth() == 0, "depth should be 0");
    TEST_ASSERT(sm.active() == nullptr, "active should be nullptr");
    TEST_ASSERT(sm.empty(), "stack should be empty");

    TEST_END();
}

// ── Suite Entry Point ────────────────────────────────────────────

void runSceneTests() {
    TEST_SUITE_BEGIN("Scene Lifecycle");

    testB1_PushCallsLifecycle();
    testB2_PopCallsLifecycle();
    testB3_ReplaceCallsLifecycle();
    testB4_MultiplePushPop();

    TEST_SUITE_END("Scene Lifecycle");
}

} // namespace CardGFXTest
