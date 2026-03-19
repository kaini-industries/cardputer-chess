#include "lobby_scene.h"
#include "chess_scene.h"
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
    // Hide any visible modals
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

    m_menuModal.addButton("Local", [this]() {
        m_menuModal.hide();
        startLocalGame();
    });
    m_menuModal.addButton("vs AI", [this]() {
        m_menuModal.hide();
        showAIDifficultyMenu();
    });
    m_menuModal.addButton("Host", [this]() {
        m_menuModal.hide();
        startHosting();
    });
    m_menuModal.addButton("Join", [this]() {
        m_menuModal.hide();
        startJoining();
    });

    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::startLocalGame() {
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
        startAIGame(PieceColor::Black); // AI plays Black
    });
    m_menuModal.addButton("Black", [this]() {
        m_menuModal.hide();
        startAIGame(PieceColor::White); // AI plays White
    });

    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::startAIGame(PieceColor aiColor) {
    m_chessScene->setAIMode(m_selectedDifficulty, aiColor);
    CardGFX::scenes().push(m_chessScene);
}

void LobbyScene::startHosting() {
    m_state = LobbyState::Hosting;
    m_localColor = PieceColor::White;
    m_titleLabel.setVisible(false);

    // Initialize ESP-NOW
    auto& transport = EspNowTransport::instance();
    if (!transport.init()) {
        m_statusLabel.setText("ESP-NOW init failed!");
        showMenu();
        return;
    }

    // Generate random game ID
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

    // Initialize ESP-NOW
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

    // Configure chess scene for network play and push it
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
                    // Peer accepted — add them and send GameStart
                    transport.addPeer(mac);
                    GameStartMsg start;
                    start.yourColor = 1; // Tell joiner they are Black
                    transport.send(
                        reinterpret_cast<const uint8_t*>(&start), sizeof(start));
                    onPaired();
                    return;
                }
            }
        }

        // Timeout after 60 seconds
        if (now - m_stateStartTime > 60000) {
            m_statusLabel.setText("Timed out.");
            cancelPairing();
        }

    } else if (m_state == LobbyState::Joining) {
        // Listen for Discovery broadcasts
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
                    // Found a host — add peer and send accept
                    m_gameId = disc.gameId;
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
                onPaired();
                return;
            }
        }

        // Timeout after 60 seconds
        if (now - m_stateStartTime > 60000) {
            m_statusLabel.setText("Timed out.");
            cancelPairing();
        }
    }
}

bool LobbyScene::onInput(const InputEvent& event) {
    if (!event.isDown()) return false;

    // ESC cancels pairing or goes back in AI menus
    if (event.key == Key::ESCAPE) {
        if (m_state == LobbyState::Hosting || m_state == LobbyState::Joining) {
            cancelPairing();
            return true;
        }
        if (m_state == LobbyState::AIDifficulty) {
            m_menuModal.hide();
            showMenu();
            return true;
        }
        if (m_state == LobbyState::AIColor) {
            m_menuModal.hide();
            showAIDifficultyMenu();
            return true;
        }
    }

    return false;
}
