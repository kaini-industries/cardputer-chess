#include "visual_test_scene.h"
#include <Arduino.h>
#include <cstdio>

using namespace CardGFX;

namespace CardGFXTest {

const VisualTestScene::TestInfo VisualTestScene::s_testInfo[NUM_TESTS] = {
    {"Solid Fill",     "Auto R/G/B. n=next"},
    {"Label Trio",     "L/C/R align? n=next"},
    {"Widget Hide",    "ENT=toggle. Stale px?"},
    {"Grid Render",    "4x4 board, cursor 0,0?"},
    {"Grid Nav",       "Arrows=move. Trails?"},
    {"Modal Show",     "ENT=show modal"},
    {"Modal Dismiss",  "ENT=dismiss. Label back?"},
    {"Scene Push/Pop", "Blue overlay. n=next"},
    {"List Scroll",    "Arrows=scroll. Scrollbar?"},
    {"Overlap",        "Blue over red at overlap?"},
    {"Rapid Toggle",   "Auto 5s toggle. Flicker?"},
    {"Stress",         "All widgets. Corruption?"},
    {"Grid+Modal",     "Auto 3x modal. Grid OK?"},
    {"Cell Repaint",   "ENT=toggle highlights. Ghost?"},
    {"Multi Dirty",    "ENT=hide label. Grid OK?"},
    {"Modal+Focus",    "Auto modal. Cursor OK?"},
    {"Rapid Cursor",   "Auto zigzag. Trails?"},
    {"Edge Redraw",    "ENT=toggle color. Bleed?"},
};

VisualTestScene::VisualTestScene() : Scene("visual_test") {}

void VisualTestScene::onEnter() {
    // Status bar at top
    m_statusLabel.setBounds({0, 0, SCREEN_W, 12});
    m_statusLabel.setAlign(Label::Align::Center);
    m_statusLabel.setDrawBg(true);
    m_statusLabel.setBgColor(HAL::rgb565(0, 0, 80));
    m_statusLabel.setColor(HAL::rgb565(255, 255, 255));
    addWidget(&m_statusLabel);

    // Hint bar at bottom
    m_hintLabel.setBounds({0, SCREEN_H - 12, SCREEN_W, 12});
    m_hintLabel.setAlign(Label::Align::Center);
    m_hintLabel.setDrawBg(true);
    m_hintLabel.setBgColor(HAL::rgb565(0, 0, 80));
    m_hintLabel.setColor(HAL::rgb565(200, 200, 200));
    addWidget(&m_hintLabel);

    m_currentStep = 0;
    m_stepDirty = true;
    setupStep(0);
}

void VisualTestScene::onExit() {
    cleanupStep();
}

void VisualTestScene::onTick(uint32_t dt_ms) {
    if (m_stepDirty) {
        char buf[40];
        snprintf(buf, sizeof(buf), "V%d/%d: %s",
                 m_currentStep + 1, NUM_TESTS,
                 s_testInfo[m_currentStep].name);
        m_statusLabel.setText(buf);
        m_hintLabel.setText(s_testInfo[m_currentStep].hint);
        m_stepDirty = false;
    }

    // V1: auto-advance through solid fills
    if (m_currentStep == 0) {
        m_timer += dt_ms;
        if (m_timer >= 500) {
            m_timer = 0;
            m_subStep++;
            if (m_subStep >= 3) m_subStep = 0;

            uint16_t colors[] = {
                HAL::rgb565(255, 0, 0),
                HAL::rgb565(0, 255, 0),
                HAL::rgb565(0, 0, 255)
            };
            m_testLabel1.setColor(colors[m_subStep]);
            m_testLabel1.setBgColor(colors[m_subStep]);
            m_testLabel1.markDirty();
        }
    }

    // V13: Grid+Modal auto cycle (show 1s, hide 1s, repeat 3x)
    if (m_currentStep == 12) {
        m_timer += dt_ms;
        if (m_timer >= 1000) {
            m_timer = 0;
            m_modalVisible = !m_modalVisible;
            if (m_modalVisible) {
                m_testModal.show();
            } else {
                m_testModal.hide();
                m_modalCycleCount++;
            }
            if (m_modalCycleCount >= 3) {
                // Done cycling — leave modal hidden, grid visible
                m_modalCycleCount = 0;
            }
        }
    }

    // V16: Modal over focused grid auto cycle (show 1s, hide 1s, repeat 3x)
    if (m_currentStep == 15) {
        m_timer += dt_ms;
        if (m_timer >= 1000) {
            m_timer = 0;
            m_modalVisible = !m_modalVisible;
            if (m_modalVisible) {
                m_testModal.show();
            } else {
                m_testModal.hide();
                m_modalCycleCount++;
            }
            if (m_modalCycleCount >= 3) {
                m_modalCycleCount = 0;
            }
        }
    }

    // V17: Rapid grid cursor zigzag (200ms per move)
    if (m_currentStep == 16) {
        m_timer += dt_ms;
        if (m_timer >= 200) {
            m_timer = 0;
            // Zigzag: go right across row, drop down, go left, drop down...
            uint8_t row = m_cursorPath / 8;
            uint8_t posInRow = m_cursorPath % 8;
            uint8_t col = (row % 2 == 0) ? posInRow : (7 - posInRow);
            m_testGrid.setCursor(col, row);

            m_cursorPath++;
            if (m_cursorPath >= 64) m_cursorPath = 0; // loop
        }
    }

    // V11: rapid toggle
    if (m_currentStep == 10) {
        m_toggleTimer += dt_ms;
        if (m_toggleTimer >= 500) {
            m_toggleTimer = 0;
            m_toggleCount++;
            m_toggleVisible = !m_toggleVisible;
            m_testLabel1.setVisible(m_toggleVisible);

            if (m_toggleCount >= 10) {
                // Done — stop toggling, show label
                m_testLabel1.setVisible(true);
                m_toggleCount = 0;
            }
        }
    }
}

void VisualTestScene::onDrawOverlay(Canvas& fb, const Theme& theme) {
    if (m_needsClear) {
        fb.fill(theme.bgPrimary);
        // Mark all widgets dirty so they redraw next frame
        for (uint8_t i = 0; i < m_widgetCount; i++) {
            m_widgets[i]->markDirty();
        }
        m_needsClear = false;
    }
}

bool VisualTestScene::onInput(const InputEvent& event) {
    if (!event.isDown()) return false;

    switch (event.key) {
    // ── Navigation (letter keys bypass focus chain) ────────────
    case 'n':
        nextStep();
        return true;

    case 'p':
        prevStep();
        return true;

    case Key::ESCAPE:
        Serial.println("\n=== Visual Tests: User exited ===");
        return true;

    // ── ENTER: step-specific actions ─────────────────────────
    case Key::ENTER:
        // V3: toggle label visibility
        if (m_currentStep == 2) {
            m_testLabel1.setVisible(!m_testLabel1.isVisible());
            return true;
        }
        // V6: show modal
        if (m_currentStep == 5) {
            m_testModal.show();
            return true;
        }
        // V7: dismiss modal
        if (m_currentStep == 6) {
            m_testModal.hide();
            return true;
        }
        // V14: toggle cell highlights
        if (m_currentStep == 13) {
            m_highlightOn = !m_highlightOn;
            if (m_highlightOn) {
                for (uint8_t r = 0; r < 8; r++)
                    for (uint8_t c = 0; c < 8; c++)
                        if ((r + c) % 2 == 0)
                            m_testGrid.setHighlighted(c, r, true);
            } else {
                m_testGrid.clearAllHighlights();
            }
            return true;
        }
        // V15: toggle label visibility
        if (m_currentStep == 14) {
            m_testLabel1.setVisible(!m_testLabel1.isVisible());
            return true;
        }
        // V18: toggle left label color
        if (m_currentStep == 17) {
            m_colorToggle = !m_colorToggle;
            m_testLabel1.setBgColor(m_colorToggle
                ? HAL::rgb565(0, 200, 0)
                : HAL::rgb565(200, 0, 0));
            m_testLabel1.markDirty();
            return true;
        }
        // Let focused widgets handle ENTER otherwise
        return false;

    default:
        // All other keys (arrows, etc.) pass through to focused widgets
        return false;
    }
}

void VisualTestScene::nextStep() {
    if (m_currentStep < NUM_TESTS - 1) {
        cleanupStep();
        m_currentStep++;
        m_stepDirty = true;
        setupStep(m_currentStep);
    } else {
        Serial.println("\n=== Visual Tests: Complete ===");
    }
}

void VisualTestScene::prevStep() {
    if (m_currentStep > 0) {
        cleanupStep();
        m_currentStep--;
        m_stepDirty = true;
        setupStep(m_currentStep);
    }
}

void VisualTestScene::cleanupStep() {
    // Remove test widgets (not status/hint)
    removeWidget(&m_testLabel1);
    removeWidget(&m_testLabel2);
    removeWidget(&m_testLabel3);
    removeWidget(&m_testGrid);
    removeWidget(&m_testModal);
    removeWidget(&m_testList);

    m_testLabel1 = Label();
    m_testLabel2 = Label();
    m_testLabel3 = Label();
    m_testGrid = Grid();
    m_testModal = Modal();
    m_testList = List();

    m_timer = 0;
    m_subStep = 0;
    m_toggleTimer = 0;
    m_toggleCount = 0;
    m_toggleVisible = true;
    m_modalCycleCount = 0;
    m_modalVisible = false;
    m_highlightOn = false;
    m_cursorPath = 0;
    m_cursorForward = true;
    m_colorToggle = false;
    m_needsClear = true;
}

void VisualTestScene::setupStep(uint8_t step) {
    switch (step) {
    case 0:  setupV1_SolidFill();   break;
    case 1:  setupV2_LabelTrio();   break;
    case 2:  setupV3_WidgetHide();  break;
    case 3:  setupV4_GridRender();  break;
    case 4:  setupV5_GridNav();     break;
    case 5:  setupV6_ModalShow();   break;
    case 6:  setupV7_ModalDismiss();break;
    case 7:  setupV8_ScenePushPop();break;
    case 8:  setupV9_ListScroll();  break;
    case 9:  setupV10_Overlap();    break;
    case 10: setupV11_RapidToggle();break;
    case 11: setupV12_Stress();     break;
    case 12: setupV13_GridModalCycle();    break;
    case 13: setupV14_GridCellRepaint();   break;
    case 14: setupV15_MultiWidgetDirty();  break;
    case 15: setupV16_ModalOverGridFocus();break;
    case 16: setupV17_RapidGridCursor();   break;
    case 17: setupV18_WidgetBoundsRedraw();break;
    }
}

// ── V1: Solid Fill ───────────────────────────────────────────────

void VisualTestScene::setupV1_SolidFill() {
    m_testLabel1.setBounds({0, 12, SCREEN_W, SCREEN_H - 24});
    m_testLabel1.setDrawBg(true);
    m_testLabel1.setBgColor(HAL::rgb565(255, 0, 0));
    m_testLabel1.setColor(HAL::rgb565(255, 0, 0));
    m_testLabel1.setText("");
    addWidget(&m_testLabel1);
    m_subStep = 0;
    m_timer = 0;
}

// ── V2: Label Trio ───────────────────────────────────────────────

void VisualTestScene::setupV2_LabelTrio() {
    int16_t y = 30;
    uint16_t h = 14;

    m_testLabel1.setBounds({10, y, 220, h});
    m_testLabel1.setAlign(Label::Align::Left);
    m_testLabel1.setText("Left aligned");
    m_testLabel1.setDrawBg(true);
    addWidget(&m_testLabel1);

    m_testLabel2.setBounds({10, (int16_t)(y + 20), 220, h});
    m_testLabel2.setAlign(Label::Align::Center);
    m_testLabel2.setText("Center aligned");
    m_testLabel2.setDrawBg(true);
    addWidget(&m_testLabel2);

    m_testLabel3.setBounds({10, (int16_t)(y + 40), 220, h});
    m_testLabel3.setAlign(Label::Align::Right);
    m_testLabel3.setText("Right aligned");
    m_testLabel3.setDrawBg(true);
    addWidget(&m_testLabel3);
}

// ── V3: Widget Hide/Show ─────────────────────────────────────────

void VisualTestScene::setupV3_WidgetHide() {
    m_testLabel1.setBounds({40, 40, 160, 40});
    m_testLabel1.setDrawBg(true);
    m_testLabel1.setBgColor(HAL::rgb565(200, 0, 0));
    m_testLabel1.setColor(HAL::rgb565(255, 255, 255));
    m_testLabel1.setText("VISIBLE");
    m_testLabel1.setScale(2);
    addWidget(&m_testLabel1);
}

// ── V4: Grid Render ──────────────────────────────────────────────

void VisualTestScene::setupV4_GridRender() {
    m_testGrid.setBounds({60, 18, 120, 100});
    m_testGrid.setGridSize(4, 4);
    m_testGrid.setCellSize(30, 25);
    m_testGrid.setCursor(0, 0);
    m_testGrid.setHighlighted(1, 1, true);
    m_testGrid.setMarked(2, 2, true);
    m_testGrid.setSelected(3, 3, true);
    m_testGrid.setFocused(true);
    addWidget(&m_testGrid);
}

// ── V5: Grid Navigation ─────────────────────────────────────────

void VisualTestScene::setupV5_GridNav() {
    m_testGrid.setBounds({60, 18, 120, 100});
    m_testGrid.setGridSize(4, 4);
    m_testGrid.setCellSize(30, 25);
    m_testGrid.setCursor(0, 0);
    m_testGrid.setFocused(true);
    addWidget(&m_testGrid, true);
    focusChain().focusWidget(&m_testGrid);
}

// ── V6: Modal Show ───────────────────────────────────────────────

void VisualTestScene::setupV6_ModalShow() {
    m_testLabel1.setBounds({20, 40, 200, 14});
    m_testLabel1.setText("Background label (should be covered)");
    m_testLabel1.setDrawBg(true);
    addWidget(&m_testLabel1);

    m_testModal.setBounds({0, 12, SCREEN_W, SCREEN_H - 24});
    m_testModal.setTitle("Test Modal");
    m_testModal.setMessage("Press ENTER to show");
    m_testModal.addButton("OK", [](){});
    addWidget(&m_testModal);
}

// ── V7: Modal Dismiss ────────────────────────────────────────────

void VisualTestScene::setupV7_ModalDismiss() {
    m_testLabel1.setBounds({20, 40, 200, 14});
    m_testLabel1.setText("This should reappear after dismiss");
    m_testLabel1.setDrawBg(true);
    addWidget(&m_testLabel1);

    m_testModal.setBounds({0, 12, SCREEN_W, SCREEN_H - 24});
    m_testModal.setTitle("Dismiss Me");
    m_testModal.setMessage("Press ENTER to dismiss");
    m_testModal.addButton("Close", [](){});
    m_testModal.show(); // start visible
    addWidget(&m_testModal);
}

// ── V8: Scene Push/Pop ───────────────────────────────────────────

void VisualTestScene::setupV8_ScenePushPop() {
    m_testLabel1.setBounds({20, 50, 200, 28});
    m_testLabel1.setDrawBg(true);
    m_testLabel1.setBgColor(HAL::rgb565(0, 0, 180));
    m_testLabel1.setColor(HAL::rgb565(255, 255, 255));
    m_testLabel1.setText("Blue scene overlay");
    m_testLabel1.setScale(2);
    addWidget(&m_testLabel1);
    // Note: actual scene push/pop would require SceneManager access.
    // This step visually simulates by showing/hiding the blue label.
}

// ── V9: List Scroll ──────────────────────────────────────────────

void VisualTestScene::setupV9_ListScroll() {
    m_testList.setBounds({40, 14, 160, 105});
    m_testList.setFocusable(true);
    for (int i = 0; i < 20; i++) {
        char buf[20];
        snprintf(buf, sizeof(buf), "Item %d", i + 1);
        m_testList.addItem(buf);
    }
    addWidget(&m_testList, true);
    focusChain().focusWidget(&m_testList);
}

// ── V10: Overlap ─────────────────────────────────────────────────

void VisualTestScene::setupV10_Overlap() {
    m_testLabel1.setBounds({30, 30, 80, 50});
    m_testLabel1.setDrawBg(true);
    m_testLabel1.setBgColor(HAL::rgb565(200, 0, 0));
    m_testLabel1.setColor(HAL::rgb565(255, 255, 255));
    m_testLabel1.setText("RED");
    m_testLabel1.setScale(2);
    addWidget(&m_testLabel1);

    m_testLabel2.setBounds({80, 50, 80, 50});
    m_testLabel2.setDrawBg(true);
    m_testLabel2.setBgColor(HAL::rgb565(0, 0, 200));
    m_testLabel2.setColor(HAL::rgb565(255, 255, 255));
    m_testLabel2.setText("BLUE");
    m_testLabel2.setScale(2);
    addWidget(&m_testLabel2);
}

// ── V11: Rapid Toggle ────────────────────────────────────────────

void VisualTestScene::setupV11_RapidToggle() {
    m_testLabel1.setBounds({40, 30, 160, 60});
    m_testLabel1.setDrawBg(true);
    m_testLabel1.setBgColor(HAL::rgb565(0, 150, 0));
    m_testLabel1.setColor(HAL::rgb565(255, 255, 255));
    m_testLabel1.setText("TOGGLE");
    m_testLabel1.setScale(2);
    addWidget(&m_testLabel1);

    m_toggleVisible = true;
    m_toggleTimer = 0;
    m_toggleCount = 0;
}

// ── V12: Stress ──────────────────────────────────────────────────

void VisualTestScene::setupV12_Stress() {
    // Label
    m_testLabel1.setBounds({5, 14, 70, 12});
    m_testLabel1.setText("Label OK");
    m_testLabel1.setDrawBg(true);
    addWidget(&m_testLabel1);

    // Grid
    m_testGrid.setBounds({5, 28, 60, 60});
    m_testGrid.setGridSize(3, 3);
    m_testGrid.setCellSize(20, 20);
    m_testGrid.setCursor(1, 1);
    m_testGrid.setHighlighted(0, 0, true);
    m_testGrid.setFocused(true);
    addWidget(&m_testGrid);

    // List
    m_testList.setBounds({70, 14, 80, 80});
    for (int i = 0; i < 10; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Row %d", i);
        m_testList.addItem(buf);
    }
    addWidget(&m_testList);

    // Second label
    m_testLabel2.setBounds({155, 14, 80, 12});
    m_testLabel2.setText("Right side");
    m_testLabel2.setDrawBg(true);
    m_testLabel2.setAlign(Label::Align::Right);
    addWidget(&m_testLabel2);

    // Third label
    m_testLabel3.setBounds({155, 28, 80, 12});
    m_testLabel3.setText("All good?");
    m_testLabel3.setDrawBg(true);
    m_testLabel3.setAlign(Label::Align::Center);
    addWidget(&m_testLabel3);
}

// ── V13: Grid + Modal Cycle ──────────────────────────────────────

void VisualTestScene::setupV13_GridModalCycle() {
    // 8x8 grid with highlights (chess-like)
    m_testGrid.setBounds({20, 14, 120, 105});
    m_testGrid.setGridSize(8, 8);
    m_testGrid.setCellSize(15, 13);
    m_testGrid.setCursor(3, 3);
    m_testGrid.setHighlighted(2, 2, true);
    m_testGrid.setHighlighted(4, 4, true);
    m_testGrid.setMarked(0, 0, true);
    m_testGrid.setMarked(7, 7, true);
    m_testGrid.setFocused(true);
    addWidget(&m_testGrid);

    // Full-screen modal that will cycle on/off
    m_testModal.setBounds({0, 12, SCREEN_W, SCREEN_H - 24});
    m_testModal.setTitle("Modal Cycle");
    m_testModal.setMessage("Auto show/hide 3x");
    m_testModal.addButton("OK", [](){});
    addWidget(&m_testModal);

    m_timer = 0;
    m_modalCycleCount = 0;
    m_modalVisible = false;
}

// ── V14: Grid Cell Repaint ──────────────────────────────────────

void VisualTestScene::setupV14_GridCellRepaint() {
    // 8x8 grid, cursor at center
    m_testGrid.setBounds({60, 14, 120, 105});
    m_testGrid.setGridSize(8, 8);
    m_testGrid.setCellSize(15, 13);
    m_testGrid.setCursor(4, 4);
    m_testGrid.setFocused(true);
    addWidget(&m_testGrid);
    m_highlightOn = false;
}

// ── V15: Multi-Widget Dirty ─────────────────────────────────────

void VisualTestScene::setupV15_MultiWidgetDirty() {
    // Grid on the left
    m_testGrid.setBounds({5, 14, 75, 105});
    m_testGrid.setGridSize(5, 8);
    m_testGrid.setCellSize(15, 13);
    m_testGrid.setCursor(2, 4);
    m_testGrid.setHighlighted(1, 1, true);
    m_testGrid.setHighlighted(3, 5, true);
    m_testGrid.setFocused(true);
    addWidget(&m_testGrid);

    // Label in the middle (toggleable)
    m_testLabel1.setBounds({85, 30, 70, 60});
    m_testLabel1.setDrawBg(true);
    m_testLabel1.setBgColor(HAL::rgb565(200, 0, 0));
    m_testLabel1.setColor(HAL::rgb565(255, 255, 255));
    m_testLabel1.setText("HIDE ME");
    m_testLabel1.setScale(1);
    addWidget(&m_testLabel1);

    // List on the right
    m_testList.setBounds({160, 14, 75, 105});
    for (int i = 0; i < 12; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Row %d", i + 1);
        m_testList.addItem(buf);
    }
    addWidget(&m_testList);
}

// ── V16: Modal Over Focused Grid ────────────────────────────────

void VisualTestScene::setupV16_ModalOverGridFocus() {
    // Focused grid with active cursor
    m_testGrid.setBounds({20, 14, 120, 105});
    m_testGrid.setGridSize(8, 8);
    m_testGrid.setCellSize(15, 13);
    m_testGrid.setCursor(4, 4);
    m_testGrid.setFocused(true);
    addWidget(&m_testGrid, true);
    focusChain().focusWidget(&m_testGrid);

    // Modal that auto-cycles
    m_testModal.setBounds({0, 12, SCREEN_W, SCREEN_H - 24});
    m_testModal.setTitle("Focus Test");
    m_testModal.setMessage("Watch cursor after dismiss");
    m_testModal.addButton("OK", [](){});
    addWidget(&m_testModal);

    m_timer = 0;
    m_modalCycleCount = 0;
    m_modalVisible = false;
}

// ── V17: Rapid Grid Cursor ──────────────────────────────────────

void VisualTestScene::setupV17_RapidGridCursor() {
    m_testGrid.setBounds({60, 14, 120, 105});
    m_testGrid.setGridSize(8, 8);
    m_testGrid.setCellSize(15, 13);
    m_testGrid.setCursor(0, 0);
    m_testGrid.setFocused(true);
    addWidget(&m_testGrid);

    m_timer = 0;
    m_cursorPath = 0;
}

// ── V18: Widget Bounds Edge Redraw ──────────────────────────────

void VisualTestScene::setupV18_WidgetBoundsRedraw() {
    // Two labels touching edge-to-edge at x=120
    m_testLabel1.setBounds({10, 40, 110, 40});
    m_testLabel1.setDrawBg(true);
    m_testLabel1.setBgColor(HAL::rgb565(200, 0, 0));
    m_testLabel1.setColor(HAL::rgb565(255, 255, 255));
    m_testLabel1.setText("LEFT");
    m_testLabel1.setScale(2);
    addWidget(&m_testLabel1);

    m_testLabel2.setBounds({120, 40, 110, 40});
    m_testLabel2.setDrawBg(true);
    m_testLabel2.setBgColor(HAL::rgb565(0, 0, 200));
    m_testLabel2.setColor(HAL::rgb565(255, 255, 255));
    m_testLabel2.setText("RIGHT");
    m_testLabel2.setScale(2);
    addWidget(&m_testLabel2);

    m_colorToggle = false;
}

} // namespace CardGFXTest
