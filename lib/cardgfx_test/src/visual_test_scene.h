#ifndef CARDGFX_VISUAL_TEST_SCENE_H
#define CARDGFX_VISUAL_TEST_SCENE_H

#include <cardgfx.h>

namespace CardGFXTest {

/**
 * Interactive visual test scene.
 *
 * Runs step-by-step rendering scenarios on the physical display.
 * User presses ENTER/RIGHT to advance, LEFT to go back, ESC to exit.
 * Each step shows what's being tested and what to look for.
 */
class VisualTestScene : public CardGFX::Scene {
public:
    VisualTestScene();

    void onEnter() override;
    void onExit() override;
    void onTick(uint32_t dt_ms) override;
    void onDrawOverlay(CardGFX::Canvas& fb, const CardGFX::Theme& theme) override;
    bool onInput(const CardGFX::InputEvent& event) override;

private:
    static constexpr uint8_t NUM_TESTS = 18;

    uint8_t m_currentStep = 0;
    bool    m_stepDirty = true;
    uint32_t m_timer = 0;         // for auto-advancing tests
    uint8_t  m_subStep = 0;       // within-step state

    // Test widgets (reused across steps)
    CardGFX::Label     m_statusLabel;
    CardGFX::Label     m_hintLabel;
    CardGFX::Label     m_testLabel1;
    CardGFX::Label     m_testLabel2;
    CardGFX::Label     m_testLabel3;
    CardGFX::Grid      m_testGrid;
    CardGFX::Modal     m_testModal;
    CardGFX::List      m_testList;

    // Rapid toggle state (V11)
    bool m_toggleVisible = true;
    uint32_t m_toggleTimer = 0;
    uint8_t m_toggleCount = 0;

    // V13/V16: auto modal cycle state
    uint8_t m_modalCycleCount = 0;
    bool    m_modalVisible = false;

    // V14: highlight toggle pattern
    bool m_highlightOn = false;

    // V17: rapid cursor state
    uint8_t m_cursorPath = 0;   // position along zigzag
    bool    m_cursorForward = true;

    // V18: color toggle
    bool m_colorToggle = false;

    // Screen clear on step transition
    bool m_needsClear = false;

    struct TestInfo {
        const char* name;
        const char* hint;
    };
    static const TestInfo s_testInfo[NUM_TESTS];

    void setupStep(uint8_t step);
    void cleanupStep();
    void nextStep();
    void prevStep();

    void setupV1_SolidFill();
    void setupV2_LabelTrio();
    void setupV3_WidgetHide();
    void setupV4_GridRender();
    void setupV5_GridNav();
    void setupV6_ModalShow();
    void setupV7_ModalDismiss();
    void setupV8_ScenePushPop();
    void setupV9_ListScroll();
    void setupV10_Overlap();
    void setupV11_RapidToggle();
    void setupV12_Stress();
    void setupV13_GridModalCycle();
    void setupV14_GridCellRepaint();
    void setupV15_MultiWidgetDirty();
    void setupV16_ModalOverGridFocus();
    void setupV17_RapidGridCursor();
    void setupV18_WidgetBoundsRedraw();
};

} // namespace CardGFXTest

#endif // CARDGFX_VISUAL_TEST_SCENE_H
