#include "lobby_scene.h"
#include "chess_scene.h"
#include "chess_storage.h"
#include "chess960.h"
#include "esp_now_transport.h"
#include <Arduino.h>
#include <esp_random.h>
#include <cstdio>
#include <cstring>

// =====================================================================
// LobbyScene Implementation
// =====================================================================

LobbyScene::LobbyScene() : Scene("lobby") {}

void LobbyScene::setup(ChessScene* chessScene) {
    m_chessScene = chessScene;

    // ── Status Bar ───────────────────────────────────────────────
    m_statusBar.setBounds({0, 0, SCREEN_W, 12});
    m_statusBar.setDrawSeparator(true);
    m_statusBar.setLeft("Chess");
    m_statusBar.setCenter("");
    m_statusBar.setRight("");
    addWidget(&m_statusBar);

    // ── Title Label ──────────────────────────────────────────────
    m_titleLabel.setText("Wireless Chess");
    m_titleLabel.setBounds({0, 30, SCREEN_W, 14});
    m_titleLabel.setAlign(Label::Align::Center);
    addWidget(&m_titleLabel);

    // ── Status Label ─────────────────────────────────────────────
    m_statusLabel.setText("");
    m_statusLabel.setBounds({0, 55, SCREEN_W, 14});
    m_statusLabel.setAlign(Label::Align::Center);
    addWidget(&m_statusLabel);

    // ── Menu Modal ───────────────────────────────────────────────
    m_menuModal.setBounds({0, 0, SCREEN_W, SCREEN_H});
    m_menuModal.setVisible(false);
    addWidget(&m_menuModal, true);
}

void LobbyScene::onEnter() {
    m_state = LobbyState::Menu;
    showMenu();
}

void LobbyScene::onExit() {
    if (m_state == LobbyState::Hosting || m_state == LobbyState::Joining) {
        EspNowTransport::instance().shutdown();
    }
    m_menuModal.hide();
}

void LobbyScene::showMenu() {
    m_state = LobbyState::Menu;
    m_statusLabel.setText("");
    m_statusBar.setLeft("Chess");
    m_statusBar.setRight("v" FIRMWARE_VERSION);
    m_titleLabel.setVisible(true);

    m_menuModal.clearButtons();
    char titleBuf[32];
    snprintf(titleBuf, sizeof(titleBuf), "Chess v%s", FIRMWARE_VERSION);
    m_menuModal.setTitle(titleBuf);
    m_menuModal.setMessage("Choose mode:");

    if (ChessStorage::hasSave()) {
        m_menuModal.addButton("Resume", [this]() {
            m_menuModal.hide();
            if (m_chessScene->loadSavedGame()) {
                CardGFX::scenes().push(m_chessScene);
            } else {
                ChessStorage::clearSave();
                showMenu();
            }
        });
    }

    m_menuModal.addButton("Local", [this]() {
        m_menuModal.hide();
        ChessStorage::clearSave();
        m_pendingMode = PendingMode::Local;
        showVariantMenu();
    });
    m_menuModal.addButton("vs AI", [this]() {
        m_menuModal.hide();
        ChessStorage::clearSave();
        m_pendingMode = PendingMode::AI;
        showVariantMenu();
    });
    m_menuModal.addButton("Host", [this]() {
        m_menuModal.hide();
        ChessStorage::clearSave();
        m_pendingMode = PendingMode::Host;
        showVariantMenu();
    });
    m_menuModal.addButton("Join", [this]() {
        m_menuModal.hide();
        ChessStorage::clearSave();
        startJoining();
    });
    m_menuModal.addButton("Puzzles", [this]() {
        m_menuModal.hide();
        showPuzzleMenu();
    });

    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::showVariantMenu() {
    m_state = LobbyState::VariantSelect;
    m_statusBar.setLeft("Variant");
    m_statusBar.setRight("ESC=Back");
    m_titleLabel.setVisible(false);

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Variant");
    m_menuModal.setMessage("Choose variant:");

    m_menuModal.addButton("Standard", [this]() {
        m_selectedVariant = ChessVariant::Standard;
        m_positionIndex = 518;
        m_menuModal.hide();
        onVariantSelected();
    });
    m_menuModal.addButton("Atomic", [this]() {
        m_selectedVariant = ChessVariant::Atomic;
        m_positionIndex = 518;
        m_menuModal.hide();
        onVariantSelected();
    });
    m_menuModal.addButton("Chess960", [this]() {
        m_selectedVariant = ChessVariant::Chess960;
        m_positionIndex = chess960RandomIndex();
        m_menuModal.hide();
        onVariantSelected();
    });

    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::onVariantSelected() {
    showTimeControlMenu();
}

void LobbyScene::showTimeControlMenu() {
    m_state = LobbyState::TimeSelect;
    m_statusBar.setLeft("Time");
    m_statusBar.setRight("ESC=Back");

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Time Control");
    m_menuModal.setMessage("Choose clock:");

    m_menuModal.addButton("No Timer", [this]() {
        m_selectedTimeControl = TimeControl::None;
        m_menuModal.hide();
        onTimeSelected();
    });
    m_menuModal.addButton("1+0", [this]() {
        m_selectedTimeControl = TimeControl::Bullet1;
        m_menuModal.hide();
        onTimeSelected();
    });
    m_menuModal.addButton("3+2", [this]() {
        m_selectedTimeControl = TimeControl::Blitz3;
        m_menuModal.hide();
        onTimeSelected();
    });
    m_menuModal.addButton("5+3", [this]() {
        m_selectedTimeControl = TimeControl::Blitz5;
        m_menuModal.hide();
        onTimeSelected();
    });
    m_menuModal.addButton("10+0", [this]() {
        m_selectedTimeControl = TimeControl::Rapid10;
        m_menuModal.hide();
        onTimeSelected();
    });

    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::onTimeSelected() {
    if (m_pendingMode == PendingMode::Local) {
        startLocalGame();
    } else if (m_pendingMode == PendingMode::AI) {
        showAIDifficultyMenu();
    } else {
        startHosting();
    }
}

void LobbyScene::configureChessScene() {
    m_chessScene->setVariant(m_selectedVariant);
    m_chessScene->setPositionIndex(m_positionIndex);
    m_chessScene->setTimeControl(m_selectedTimeControl);
}

void LobbyScene::startLocalGame() {
    configureChessScene();
    m_chessScene->clearNetworkMode();
    CardGFX::scenes().push(m_chessScene);
}

void LobbyScene::showAIDifficultyMenu() {
    m_state = LobbyState::AIDifficulty;
    m_statusBar.setLeft("vs AI");
    m_statusBar.setRight("ESC=Back");
    m_titleLabel.setVisible(false);

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Difficulty");
    m_menuModal.setMessage("Choose level:");

    m_menuModal.addButton("Easy", [this]() {
        m_selectedDifficulty = AIDifficulty::Easy;
        m_menuModal.hide();
        showAIColorMenu();
    });
    m_menuModal.addButton("Medium", [this]() {
        m_selectedDifficulty = AIDifficulty::Medium;
        m_menuModal.hide();
        showAIColorMenu();
    });
    m_menuModal.addButton("Hard", [this]() {
        m_selectedDifficulty = AIDifficulty::Hard;
        m_menuModal.hide();
        showAIColorMenu();
    });

    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::showAIColorMenu() {
    m_state = LobbyState::AIColor;

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Play as");
    m_menuModal.setMessage("Choose your color:");

    m_menuModal.addButton("White", [this]() {
        m_menuModal.hide();
        startAIGame(PieceColor::Black);
    });
    m_menuModal.addButton("Black", [this]() {
        m_menuModal.hide();
        startAIGame(PieceColor::White);
    });

    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::startAIGame(PieceColor aiColor) {
    configureChessScene();
    m_chessScene->setAIMode(m_selectedDifficulty, aiColor);
    CardGFX::scenes().push(m_chessScene);
}

void LobbyScene::startHosting() {
    m_state = LobbyState::Hosting;
    m_localColor = PieceColor::White;
    m_titleLabel.setVisible(false);

    auto& transport = EspNowTransport::instance();
    if (!transport.init()) {
        m_statusLabel.setText("ESP-NOW init failed!");
        showMenu();
        return;
    }

    m_gameId = (uint16_t)(esp_random() & 0xFFFF);
    m_lastBroadcast = 0;
    m_stateStartTime = millis();

    m_statusBar.setLeft("Hosting");
    m_statusBar.setRight("ESC=Back");
    m_statusLabel.setText("Waiting for opponent...");
}

void LobbyScene::startJoining() {
    m_state = LobbyState::Joining;
    m_localColor = PieceColor::Black;
    m_titleLabel.setVisible(false);

    auto& transport = EspNowTransport::instance();
    if (!transport.init()) {
        m_statusLabel.setText("ESP-NOW init failed!");
        showMenu();
        return;
    }

    m_stateStartTime = millis();
    m_statusBar.setLeft("Joining");
    m_statusBar.setRight("ESC=Back");
    m_statusLabel.setText("Searching for host...");
}

void LobbyScene::cancelPairing() {
    EspNowTransport::instance().shutdown();
    showMenu();
}

void LobbyScene::onPaired() {
    m_state = LobbyState::Paired;
    m_statusLabel.setText("Connected!");

    configureChessScene();
    m_chessScene->setNetworkMode(m_localColor);
    CardGFX::scenes().push(m_chessScene);
}

void LobbyScene::onTick(uint32_t /*dt_ms*/) {
    auto& transport = EspNowTransport::instance();
    uint32_t now = millis();

    if (m_state == LobbyState::Hosting) {
        // Broadcast discovery every 500ms
        if (now - m_lastBroadcast > 500) {
            m_lastBroadcast = now;
            DiscoveryMsg disc;
            disc.gameId = m_gameId;
            disc.variant = static_cast<uint8_t>(m_selectedVariant);
            disc.positionIndex = m_positionIndex;
            disc.timeControl = static_cast<uint8_t>(m_selectedTimeControl);
            transport.broadcast(
                reinterpret_cast<const uint8_t*>(&disc), sizeof(disc));
        }

        // Check for AcceptGame response
        uint8_t buf[32];
        uint8_t mac[6];
        while (transport.hasReceived()) {
            uint8_t len = transport.receive(buf, sizeof(buf), mac);
            if (len == 0) break;

            if (len >= sizeof(AcceptGameMsg) &&
                static_cast<NetMsgType>(buf[0]) == NetMsgType::AcceptGame) {
                AcceptGameMsg accept;
                memcpy(&accept, buf, sizeof(AcceptGameMsg));
                if (accept.gameId == m_gameId) {
                    transport.addPeer(mac);
                    GameStartMsg start;
                    start.yourColor = 1;
                    start.variant = static_cast<uint8_t>(m_selectedVariant);
                    start.positionIndex = m_positionIndex;
                    start.timeControl = static_cast<uint8_t>(m_selectedTimeControl);
                    transport.send(
                        reinterpret_cast<const uint8_t*>(&start), sizeof(start));
                    onPaired();
                    return;
                }
            }
        }

        if (now - m_stateStartTime > 60000) {
            m_statusLabel.setText("Timed out.");
            cancelPairing();
        }

    } else if (m_state == LobbyState::Joining) {
        uint8_t buf[32];
        uint8_t mac[6];
        while (transport.hasReceived()) {
            uint8_t len = transport.receive(buf, sizeof(buf), mac);
            if (len == 0) break;

            if (len >= sizeof(DiscoveryMsg) &&
                static_cast<NetMsgType>(buf[0]) == NetMsgType::Discovery) {
                DiscoveryMsg disc;
                memcpy(&disc, buf, sizeof(DiscoveryMsg));

                if (disc.version == NET_PROTOCOL_VERSION) {
                    m_gameId = disc.gameId;
                    m_selectedVariant = static_cast<ChessVariant>(disc.variant);
                    m_positionIndex = disc.positionIndex;
                    m_selectedTimeControl = static_cast<TimeControl>(disc.timeControl);
                    memcpy(m_peerMac, mac, 6);
                    transport.addPeer(mac);

                    AcceptGameMsg accept;
                    accept.gameId = m_gameId;
                    transport.send(
                        reinterpret_cast<const uint8_t*>(&accept), sizeof(accept));

                    m_statusLabel.setText("Connecting...");
                }
            } else if (len >= sizeof(GameStartMsg) &&
                       static_cast<NetMsgType>(buf[0]) == NetMsgType::GameStart) {
                GameStartMsg start;
                memcpy(&start, buf, sizeof(GameStartMsg));

                m_localColor = (start.yourColor == 0)
                              ? PieceColor::White : PieceColor::Black;
                m_selectedVariant = static_cast<ChessVariant>(start.variant);
                m_positionIndex = start.positionIndex;
                m_selectedTimeControl = static_cast<TimeControl>(start.timeControl);
                onPaired();
                return;
            }
        }

        if (now - m_stateStartTime > 60000) {
            m_statusLabel.setText("Timed out.");
            cancelPairing();
        }
    }
}

// ── Puzzle Menu ──────────────────────────────────────────────────────

void LobbyScene::showPuzzleMenu() {
    m_state = LobbyState::PuzzleCategory;
    m_statusBar.setLeft("Puzzles");
    m_statusBar.setRight("ESC=Back");
    m_titleLabel.setVisible(false);

    PuzzleProgress progress;
    PuzzleStorage::loadProgress(progress);

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Puzzles");

    char msgBuf[32];
    uint16_t total = puzzleCount();
    uint16_t done = PuzzleStorage::completedCount(progress, total);
    snprintf(msgBuf, sizeof(msgBuf), "Solved: %d/%d", done, total);
    m_menuModal.setMessage(msgBuf);

    // Next unsolved
    m_menuModal.addButton("Next", [this]() {
        m_menuModal.hide();
        PuzzleProgress prog;
        PuzzleStorage::loadProgress(prog);
        uint16_t total = puzzleCount();
        for (uint16_t i = 0; i < total; i++) {
            if (!PuzzleStorage::isPuzzleCompleted(prog, (uint8_t)i)) {
                startPuzzle((uint8_t)i);
                return;
            }
        }
        // All solved — start from 0
        if (total > 0) startPuzzle(0);
    });

    // Categories
    char m1Buf[16], m2Buf[16], tBuf[16];
    snprintf(m1Buf, sizeof(m1Buf), "Mate1 (%d)", puzzleCountByType(PuzzleType::MateIn1));
    snprintf(m2Buf, sizeof(m2Buf), "Mate2 (%d)", puzzleCountByType(PuzzleType::MateIn2));
    snprintf(tBuf, sizeof(tBuf), "Tactic (%d)", puzzleCountByType(PuzzleType::Tactic));

    m_menuModal.addButton(m1Buf, [this]() {
        m_menuModal.hide();
        uint16_t idx = puzzleIndexByType(PuzzleType::MateIn1, 0);
        if (idx != 0xFFFF) startPuzzle((uint8_t)idx);
    });
    m_menuModal.addButton(m2Buf, [this]() {
        m_menuModal.hide();
        uint16_t idx = puzzleIndexByType(PuzzleType::MateIn2, 0);
        if (idx != 0xFFFF) startPuzzle((uint8_t)idx);
    });
    m_menuModal.addButton(tBuf, [this]() {
        m_menuModal.hide();
        uint16_t idx = puzzleIndexByType(PuzzleType::Tactic, 0);
        if (idx != 0xFFFF) startPuzzle((uint8_t)idx);
    });

    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::startPuzzle(uint8_t index) {
    m_chessScene->setPuzzleMode(index);
    CardGFX::scenes().push(m_chessScene);
}

// ── Input ────────────────────────────────────────────────────────────

bool LobbyScene::onInput(const InputEvent& event) {
    if (!event.isDown()) return false;

    if (event.key == Key::ESCAPE) {
        if (m_state == LobbyState::Hosting || m_state == LobbyState::Joining) {
            cancelPairing();
            return true;
        }
        if (m_state == LobbyState::VariantSelect) {
            m_menuModal.hide();
            showMenu();
            return true;
        }
        if (m_state == LobbyState::TimeSelect) {
            m_menuModal.hide();
            showVariantMenu();
            return true;
        }
        if (m_state == LobbyState::AIDifficulty) {
            m_menuModal.hide();
            showTimeControlMenu();
            return true;
        }
        if (m_state == LobbyState::AIColor) {
            m_menuModal.hide();
            showAIDifficultyMenu();
            return true;
        }
        if (m_state == LobbyState::PuzzleCategory) {
            m_menuModal.hide();
            showMenu();
            return true;
        }
    }

    return false;
}
