#include "lobby_scene.h"
#include "chess_scene.h"
#include "chess_storage.h"
#include "chess960.h"
#include "esp_now_transport.h"
#include <Arduino.h>
#include <esp_random.h>
#include <cstdio>
#include <cstring>

// ── Name helpers for host list labels ────────────────────────────────

static const char* variantShortName(ChessVariant v) {
    switch (v) {
        case ChessVariant::Chess960: return "960";
        default:                     return "Std";
    }
}

static const char* timeControlShortName(TimeControl tc) {
    switch (tc) {
        case TimeControl::Bullet1: return "1+0";
        case TimeControl::Blitz3:  return "3+2";
        case TimeControl::Blitz5:  return "5+3";
        case TimeControl::Rapid10: return "10+0";
        default:                   return "None";
    }
}

static const char* gameModeName(GameMode mode) {
    switch (mode) {
        case GameMode::AI: return "AI";
        case GameMode::Online: return "Online";
        default: return "Local";
    }
}

static char gameModeLetter(GameMode mode) {
    switch (mode) {
        case GameMode::AI: return 'A';
        case GameMode::Online: return 'N';
        default: return 'L';
    }
}

static void formatMacSuffix(const uint8_t mac[6], char* out) {
    snprintf(out, 6, "%02X:%02X", mac[4], mac[5]);
}

static bool sameMac(const uint8_t lhs[6], const uint8_t rhs[6]) {
    return lhs != nullptr && rhs != nullptr && memcmp(lhs, rhs, 6) == 0;
}

static bool isValidRemoteMac(const uint8_t mac[6], const uint8_t ownMac[6]) {
    if (mac == nullptr || ownMac == nullptr || (mac[0] & 0x01) != 0 ||
        sameMac(mac, ownMac)) {
        return false;
    }

    bool anyNonZero = false;
    for (uint8_t i = 0; i < 6; ++i) anyNonZero |= mac[i] != 0;
    return anyNonZero;
}

static uint16_t randomGameId() {
    uint16_t id = 0;
    while (id == 0) id = static_cast<uint16_t>(esp_random() & 0xFFFFU);
    return id;
}

static uint32_t randomSessionId() {
    uint32_t id = 0;
    while (id == 0) id = esp_random();
    return id;
}

static void copyRemoteNameOrDefault(
    char destination[NET_DISPLAY_NAME_BYTES],
    const char source[NET_DISPLAY_NAME_BYTES],
    const uint8_t mac[6]) {
    if (source[0] != '\0') {
        copyNetDisplayName(destination, source);
        return;
    }

    char generated[NET_DISPLAY_NAME_BYTES];
    snprintf(generated, sizeof(generated), "Card-%02X%02X", mac[4], mac[5]);
    copyNetDisplayName(destination, generated);
}

// =====================================================================
// LobbyScene Implementation
// =====================================================================

LobbyScene::LobbyScene() : Scene("lobby") {}

void LobbyScene::setLocalPlayerName(const char* name) {
    copyNetDisplayName(m_localPlayerName, name);
}

void LobbyScene::markStorageUnavailable(const char* message) {
    m_storageReady = false;
    m_storageIssue = message ? message
                             : "Storage unavailable. No data changed.";
}

bool LobbyScene::clearRecoveredGameState(const GameSummary& pending) {
    // Online games never own the local Resume slot or its legacy participant
    // sidecar. Do not let an accidental random-ID collision erase an unrelated
    // local game while recovering an online result.
    if (pending.mode != GameMode::Online) {
        const ChessStorage::LoadResult savedGame = ChessStorage::probe();
        if (savedGame.status == ChessStorage::LoadStatus::IoError) {
            markStorageUnavailable("Storage unavailable. Result kept for Retry.");
            return false;
        }
        if (savedGame.status == ChessStorage::LoadStatus::Loaded &&
            savedGame.gameId == pending.gameId &&
            !ChessStorage::clearIfGameId(pending.gameId)) {
            markStorageUnavailable("Could not close saved game. Result kept.");
            return false;
        }

        ActiveGameParticipants active;
        const ProfileStorage::LoadStatus activeStatus =
            ProfileStorage::loadActiveGameStatus(active);
        if (activeStatus == ProfileStorage::LoadStatus::IoError) {
            markStorageUnavailable("Storage unavailable. Result kept for Retry.");
            return false;
        }
        if (activeStatus == ProfileStorage::LoadStatus::Loaded && active.valid &&
            active.gameId == pending.gameId) {
            ActiveGameParticipants expected;
            expected.valid = true;
            expected.gameId = pending.gameId;
            expected.mode = pending.mode;
            expected.whiteProfileId = pending.whiteProfileId;
            expected.blackProfileId = pending.blackProfileId;
            std::memcpy(expected.whiteName, pending.whiteName,
                        PLAYER_NAME_MAX + 1);
            std::memcpy(expected.blackName, pending.blackName,
                        PLAYER_NAME_MAX + 1);
            if (!ProfileStorage::clearActiveGame(expected)) {
                markStorageUnavailable(
                    "Game metadata differs. Result kept for Retry.");
                return false;
            }
        }
    }
    return true;
}

bool LobbyScene::commitPendingResult(const GameSummary& pending) {
    const ProfileStorage::SummaryMatch match =
        ProfileStorage::findSummary(m_profiles, pending);
    if (match == ProfileStorage::SummaryMatch::Conflict) {
        markStorageUnavailable(
            "Result conflicts with history. Data was preserved.");
        return false;
    }

    if (match == ProfileStorage::SummaryMatch::Missing) {
        ProfileData updated = m_profiles;
        if (!GameRecords::recordCompletedGame(updated, pending)) {
            markStorageUnavailable(
                "Pending result is invalid. Data was preserved.");
            return false;
        }
        if (!ProfileStorage::save(updated)) {
            markStorageUnavailable(
                "Could not save result. It is safe to Retry.");
            return false;
        }
        m_profiles = updated;
    }

    // The exact result is durable at this point. Cleanup is idempotent and is
    // deliberately completed before the journal is removed.
    if (!clearRecoveredGameState(pending)) return false;
    if (!ProfileStorage::clearPendingResult(pending)) {
        markStorageUnavailable(
            "Could not finish recovery. Result kept for Retry.");
        return false;
    }
    return true;
}

bool LobbyScene::refreshPersistentState() {
    m_storageReady = false;
    m_resumeAvailable = false;

    // Read the journal first. A confirmed missing profile may then be created,
    // but corrupt, future-version, and unreadable profile bytes are untouched.
    GameSummary pending;
    const ProfileStorage::LoadStatus pendingStatus =
        ProfileStorage::loadPendingResult(pending);
    switch (pendingStatus) {
        case ProfileStorage::LoadStatus::Loaded:
        case ProfileStorage::LoadStatus::Missing:
            break;
        case ProfileStorage::LoadStatus::Corrupt:
            markStorageUnavailable(
                "Result journal is corrupt. Data was preserved.");
            return false;
        case ProfileStorage::LoadStatus::UnsupportedVersion:
            markStorageUnavailable(
                "Result needs newer firmware. Data was preserved.");
            return false;
        default:
            markStorageUnavailable(
                "Result recovery unavailable. No data was changed.");
            return false;
    }

    // With no recovery work to commit, validate the resume slot before a
    // first-run profile is created. This keeps every unavailable/corrupt-save
    // path read-only.
    if (pendingStatus == ProfileStorage::LoadStatus::Missing) {
        const ChessStorage::LoadResult savedGame = ChessStorage::probe();
        switch (savedGame.status) {
            case ChessStorage::LoadStatus::Loaded:
            case ChessStorage::LoadStatus::Missing:
                break;
            case ChessStorage::LoadStatus::Corrupt:
                markStorageUnavailable(
                    "Resume unavailable: save corrupt. Data preserved.");
                return false;
            case ChessStorage::LoadStatus::UnsupportedVersion:
                markStorageUnavailable(
                    "Resume needs newer firmware. Data was preserved.");
                return false;
            default:
                markStorageUnavailable(
                    "Saved-game storage unavailable. No data changed.");
                return false;
        }
    }

    ProfileData loadedProfiles;
    const ProfileStorage::LoadStatus profileStatus =
        ProfileStorage::loadOrCreate(loadedProfiles);
    switch (profileStatus) {
        case ProfileStorage::LoadStatus::Loaded:
        case ProfileStorage::LoadStatus::Created:
            m_profiles = loadedProfiles;
            break;
        case ProfileStorage::LoadStatus::Corrupt:
            markStorageUnavailable(
                "Profiles are corrupt. Stored data was preserved.");
            return false;
        case ProfileStorage::LoadStatus::UnsupportedVersion:
            markStorageUnavailable(
                "Profiles need newer firmware. Data was preserved.");
            return false;
        default:
            markStorageUnavailable(
                "Profile storage unavailable. No data was changed.");
            return false;
    }

    syncActiveProfileName();

    if (pendingStatus == ProfileStorage::LoadStatus::Loaded &&
        !commitPendingResult(pending)) {
        return false;
    }

    const ChessStorage::LoadResult savedGame = ChessStorage::probe();
    switch (savedGame.status) {
        case ChessStorage::LoadStatus::Loaded:
            m_resumeAvailable = true;
            m_storageReady = true;
            m_storageIssue = "";
            return true;
        case ChessStorage::LoadStatus::Missing:
            m_storageReady = true;
            m_storageIssue = "";
            return true;
        case ChessStorage::LoadStatus::Corrupt:
            markStorageUnavailable(
                "Resume unavailable: save corrupt. Data preserved.");
            return false;
        case ChessStorage::LoadStatus::UnsupportedVersion:
            markStorageUnavailable(
                "Resume needs newer firmware. Data was preserved.");
            return false;
        default:
            markStorageUnavailable(
                "Saved-game storage unavailable. No data changed.");
            return false;
    }
}

bool LobbyScene::requirePersistentState() {
    if (refreshPersistentState()) return true;
    showStorageUnavailable();
    return false;
}

void LobbyScene::showStorageUnavailable() {
    m_state = LobbyState::Menu;
    m_nameInput.setVisible(false);
    m_titleLabel.setVisible(false);
    m_statusBar.setLeft("Storage");
    m_statusBar.setCenter("");
    m_statusBar.setRight("Retry");
    m_statusLabel.setText("");

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Storage Unavailable");
    m_menuModal.setMessage(m_storageIssue);
    // Lobby is the root scene. Escape must not pop the only scene and leave an
    // empty stack, so it deliberately keeps this recoverable Retry dialog open.
    m_menuModal.setEscapeCallback([]() {});
    m_menuModal.addButton("Retry", [this]() {
        m_menuModal.hide();
        showMenu();
    });
    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::syncActiveProfileName() {
    const PlayerProfile* active = GameRecords::activeProfile(m_profiles);
    setLocalPlayerName(active ? active->name : "Player");
}

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
    m_menuModal.setSpaceActivates(false);
    m_menuModal.setVisible(false);
    addWidget(&m_menuModal, true);

    // Added after the modal so it remains visible when the modal is hidden for
    // profile-name editing.
    m_nameInput.setBounds({24, 72, 192, 24});
    m_nameInput.setMaxLength(PLAYER_NAME_MAX);
    m_nameInput.setPlaceholder("Profile name");
    m_nameInput.setOnSubmit([this](const char* name) {
        finishProfileNameEdit(name);
    });
    m_nameInput.setVisible(false);
    addWidget(&m_nameInput, true);
}

void LobbyScene::onEnter() {
    m_state = LobbyState::Menu;
    m_nameInput.setVisible(false);
    showMenu();
}

void LobbyScene::onExit() {
    if (m_state == LobbyState::Hosting || m_state == LobbyState::Joining ||
        m_state == LobbyState::WaitingForStartAck) {
        EspNowTransport::instance().shutdown();
    }
    m_menuModal.hide();
    m_nameInput.setVisible(false);
}

void LobbyScene::showMenu() {
    // Every route back to the menu rechecks the journal and storage records;
    // callers never rely on a stale success from an earlier screen.
    if (!refreshPersistentState()) {
        showStorageUnavailable();
        return;
    }
    m_state = LobbyState::Menu;
    m_statusLabel.setText("");
    m_statusBar.setLeft("Chess");
    const PlayerProfile* active = GameRecords::activeProfile(m_profiles);
    m_statusBar.setCenter(active ? active->name : "Player");
    m_statusBar.setRight("v" FIRMWARE_VERSION);
    m_titleLabel.setText("Wireless Chess");
    m_titleLabel.setVisible(true);
    m_nameInput.setVisible(false);

    m_menuModal.clearButtons();
    m_menuModal.setEscapeCallback([](){});  // No back at top level
    char titleBuf[32];
    snprintf(titleBuf, sizeof(titleBuf), "Chess v%s", FIRMWARE_VERSION);
    m_menuModal.setTitle(titleBuf);
    char menuMessage[32];
    snprintf(menuMessage, sizeof(menuMessage), "Active: %s",
             active ? active->name : "Player");
    m_menuModal.setMessage(menuMessage);

    if (m_resumeAvailable) {
        m_menuModal.addButton("Resume", [this]() {
            m_menuModal.hide();
            if (!requirePersistentState()) return;
            if (m_chessScene->loadSavedGame()) {
                CardGFX::scenes().push(m_chessScene);
            } else {
                markStorageUnavailable(
                    "Resume unavailable. Saved data was preserved.");
                showStorageUnavailable();
            }
        });
    }

    m_menuModal.addButton("Local", [this]() {
        m_menuModal.hide();
        m_pendingMode = PendingMode::Local;
        showVariantMenu();
    });
    m_menuModal.addButton("vs AI", [this]() {
        m_menuModal.hide();
        m_pendingMode = PendingMode::AI;
        showVariantMenu();
    });
    m_menuModal.addButton("Host", [this]() {
        m_menuModal.hide();
        m_pendingMode = PendingMode::Host;
        showVariantMenu();
    });
    m_menuModal.addButton("Join", [this]() {
        m_menuModal.hide();
        startJoining();
    });
    m_menuModal.addButton("Profiles", [this]() {
        m_menuModal.hide();
        showProfilesMenu();
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
    m_statusBar.setCenter("");
    m_statusBar.setRight("Esc=Back");
    m_titleLabel.setVisible(false);

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Variant");
    m_menuModal.setMessage("Choose variant:");
    m_menuModal.setEscapeCallback([this]() {
        m_menuModal.hide();
        showMenu();
    });

    m_menuModal.addButton("Standard", [this]() {
        m_selectedVariant = ChessVariant::Standard;
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
    m_menuModal.addButton("Back", [this]() {
        m_menuModal.hide();
        showMenu();
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
    m_statusBar.setCenter("");
    m_statusBar.setRight("Esc=Back");

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Time Control");
    m_menuModal.setMessage("Choose clock:");
    m_menuModal.setEscapeCallback([this]() {
        m_menuModal.hide();
        showVariantMenu();
    });

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
    m_menuModal.addButton("Back", [this]() {
        m_menuModal.hide();
        showVariantMenu();
    });

    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::onTimeSelected() {
    if (m_pendingMode == PendingMode::Local) {
        showLocalWhiteMenu();
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

void LobbyScene::ensureLocalPlayerName() {
    if (m_localPlayerName[0] != '\0') return;

    const uint8_t* mac = EspNowTransport::instance().ownMac();
    char generated[NET_DISPLAY_NAME_BYTES];
    snprintf(generated, sizeof(generated), "Card-%02X%02X", mac[4], mac[5]);
    copyNetDisplayName(m_localPlayerName, generated);
}

void LobbyScene::showTransportError(const char* action) {
    auto& transport = EspNowTransport::instance();
    char message[64];
    snprintf(message, sizeof(message), "%s failed (%s:%ld)",
             action,
             transport.lastErrorName(),
             static_cast<long>(transport.lastEspError()));
    m_statusLabel.setText(message);
    if (m_menuModal.isVisible()) m_menuModal.setMessage(message);
}

uint32_t LobbyScene::createGameId() const {
    for (uint8_t attempt = 0; attempt < 32; ++attempt) {
        const uint32_t id = esp_random();
        if (id != 0 && !GameRecords::hasRecordedGame(m_profiles, id)) {
            return id;
        }
    }

    uint32_t id = 1;
    while (GameRecords::hasRecordedGame(m_profiles, id)) {
        ++id;
        if (id == 0) id = 1;
    }
    return id;
}

ActiveGameParticipants LobbyScene::makeLocalParticipants(
        uint8_t blackProfileIndex) const {
    ActiveGameParticipants participants;
    participants.valid = true;
    participants.gameId = createGameId();
    participants.mode = GameMode::Local;

    if (m_localWhiteProfileIndex < m_profiles.profileCount) {
        const PlayerProfile& white =
            m_profiles.profiles[m_localWhiteProfileIndex];
        participants.whiteProfileId = white.id;
        strncpy(participants.whiteName, white.name, PLAYER_NAME_MAX);
    } else {
        strncpy(participants.whiteName, "Guest", PLAYER_NAME_MAX);
    }

    if (blackProfileIndex < m_profiles.profileCount) {
        const PlayerProfile& black = m_profiles.profiles[blackProfileIndex];
        participants.blackProfileId = black.id;
        strncpy(participants.blackName, black.name, PLAYER_NAME_MAX);
    } else {
        strncpy(participants.blackName, "Guest", PLAYER_NAME_MAX);
    }
    participants.whiteName[PLAYER_NAME_MAX] = '\0';
    participants.blackName[PLAYER_NAME_MAX] = '\0';
    return participants;
}

ActiveGameParticipants LobbyScene::makeAIParticipants(
        PieceColor aiColor) const {
    ActiveGameParticipants participants;
    participants.valid = true;
    participants.gameId = createGameId();
    participants.mode = GameMode::AI;

    const PlayerProfile* active = GameRecords::activeProfile(m_profiles);
    const char* playerName = active ? active->name : "Player";
    const PieceColor humanColor = opponent(aiColor);
    if (humanColor == PieceColor::White) {
        participants.whiteProfileId = active ? active->id : 0;
        strncpy(participants.whiteName, playerName, PLAYER_NAME_MAX);
        strncpy(participants.blackName, "Computer", PLAYER_NAME_MAX);
    } else {
        strncpy(participants.whiteName, "Computer", PLAYER_NAME_MAX);
        participants.blackProfileId = active ? active->id : 0;
        strncpy(participants.blackName, playerName, PLAYER_NAME_MAX);
    }
    participants.whiteName[PLAYER_NAME_MAX] = '\0';
    participants.blackName[PLAYER_NAME_MAX] = '\0';
    return participants;
}

void LobbyScene::showLocalWhiteMenu() {
    m_state = LobbyState::LocalWhiteSelect;
    m_statusBar.setLeft("Local Game");
    m_statusBar.setCenter("");
    m_statusBar.setRight("Esc=Back");
    m_titleLabel.setVisible(false);

    m_menuModal.clearButtons();
    m_menuModal.setTitle("White Player");
    m_menuModal.setMessage("Choose White:");
    m_menuModal.setEscapeCallback([this]() {
        m_menuModal.hide();
        showTimeControlMenu();
    });

    // Put the active profile first for the common single-player setup.
    for (uint8_t pass = 0; pass < 2; ++pass) {
        for (uint8_t i = 0; i < m_profiles.profileCount; ++i) {
            const bool isActive = i == m_profiles.activeIndex;
            if ((pass == 0) != isActive) continue;
            char label[24];
            snprintf(label, sizeof(label), "%c %s",
                     isActive ? '*' : ' ', m_profiles.profiles[i].name);
            const uint8_t index = i;
            m_menuModal.addButton(label, [this, index]() {
                m_localWhiteProfileIndex = index;
                m_menuModal.hide();
                showLocalBlackMenu();
            });
        }
    }
    m_menuModal.addButton("Guest", [this]() {
        m_localWhiteProfileIndex = GUEST_PROFILE_INDEX;
        m_menuModal.hide();
        showLocalBlackMenu();
    });
    m_menuModal.addButton("Back", [this]() {
        m_menuModal.hide();
        showTimeControlMenu();
    });
    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::showLocalBlackMenu() {
    m_state = LobbyState::LocalBlackSelect;
    m_statusBar.setLeft("Local Game");
    m_statusBar.setCenter("");
    m_statusBar.setRight("Esc=Back");

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Black Player");
    m_menuModal.setMessage(m_localWhiteProfileIndex == GUEST_PROFILE_INDEX
        ? "Choose a profile:" : "Choose Black:");
    m_menuModal.setEscapeCallback([this]() {
        m_menuModal.hide();
        showLocalWhiteMenu();
    });

    for (uint8_t pass = 0; pass < 2; ++pass) {
        for (uint8_t i = 0; i < m_profiles.profileCount; ++i) {
            if (i == m_localWhiteProfileIndex) continue;
            const bool isActive = i == m_profiles.activeIndex;
            if ((pass == 0) != isActive) continue;
            char label[24];
            snprintf(label, sizeof(label), "%c %s",
                     isActive ? '*' : ' ', m_profiles.profiles[i].name);
            const uint8_t index = i;
            m_menuModal.addButton(label, [this, index]() {
                m_menuModal.hide();
                startLocalGame(index);
            });
        }
    }
    // At least one local profile must participate so resumable metadata can be
    // validated; Guest vs Guest would have no stable profile identity.
    if (m_localWhiteProfileIndex != GUEST_PROFILE_INDEX) {
        m_menuModal.addButton("Guest", [this]() {
            m_menuModal.hide();
            startLocalGame(GUEST_PROFILE_INDEX);
        });
    }
    m_menuModal.addButton("Back", [this]() {
        m_menuModal.hide();
        showLocalWhiteMenu();
    });
    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::startLocalGame(uint8_t blackProfileIndex) {
    if (!requirePersistentState()) return;
    if (m_resumeAvailable) {
        auto keepSave = [this]() {
            m_menuModal.hide();
            showLocalBlackMenu();
        };
        m_menuModal.clearButtons();
        m_menuModal.setTitle("Replace Saved Game?");
        m_menuModal.setMessage("Resume will be removed only if you replace it.");
        m_menuModal.setEscapeCallback(keepSave);
        m_menuModal.addButton("Replace", [this, blackProfileIndex]() {
            m_menuModal.hide();
            if (!ChessStorage::clearSave()) {
                markStorageUnavailable(
                    "Could not replace saved game. Data was preserved.");
                showStorageUnavailable();
                return;
            }
            startLocalGame(blackProfileIndex);
        });
        m_menuModal.addButton("Keep Save", keepSave);
        m_menuModal.show();
        focusChain().focusWidget(&m_menuModal);
        return;
    }

    configureChessScene();
    m_chessScene->clearNetworkMode();
    m_chessScene->setParticipants(
        makeLocalParticipants(blackProfileIndex));
    CardGFX::scenes().push(m_chessScene);
}

void LobbyScene::showAIDifficultyMenu() {
    m_state = LobbyState::AIDifficulty;
    m_statusBar.setLeft("vs AI");
    m_statusBar.setCenter("");
    m_statusBar.setRight("Esc=Back");
    m_titleLabel.setVisible(false);

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Difficulty");
    m_menuModal.setMessage("Choose level:");
    m_menuModal.setEscapeCallback([this]() {
        m_menuModal.hide();
        showTimeControlMenu();
    });

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
    m_menuModal.addButton("Back", [this]() {
        m_menuModal.hide();
        showTimeControlMenu();
    });

    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::showAIColorMenu() {
    m_state = LobbyState::AIColor;
    m_statusBar.setLeft("vs AI");
    m_statusBar.setCenter("");
    m_statusBar.setRight("Esc=Back");

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Play as");
    m_menuModal.setMessage("Choose your color:");
    m_menuModal.setEscapeCallback([this]() {
        m_menuModal.hide();
        showAIDifficultyMenu();
    });

    m_menuModal.addButton("White", [this]() {
        m_menuModal.hide();
        startAIGame(PieceColor::Black);
    });
    m_menuModal.addButton("Black", [this]() {
        m_menuModal.hide();
        startAIGame(PieceColor::White);
    });
    m_menuModal.addButton("Back", [this]() {
        m_menuModal.hide();
        showAIDifficultyMenu();
    });

    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::startAIGame(PieceColor aiColor) {
    if (!requirePersistentState()) return;
    if (m_resumeAvailable) {
        auto keepSave = [this]() {
            m_menuModal.hide();
            showAIColorMenu();
        };
        m_menuModal.clearButtons();
        m_menuModal.setTitle("Replace Saved Game?");
        m_menuModal.setMessage("Resume will be removed only if you replace it.");
        m_menuModal.setEscapeCallback(keepSave);
        m_menuModal.addButton("Replace", [this, aiColor]() {
            m_menuModal.hide();
            if (!ChessStorage::clearSave()) {
                markStorageUnavailable(
                    "Could not replace saved game. Data was preserved.");
                showStorageUnavailable();
                return;
            }
            startAIGame(aiColor);
        });
        m_menuModal.addButton("Keep Save", keepSave);
        m_menuModal.show();
        focusChain().focusWidget(&m_menuModal);
        return;
    }

    configureChessScene();
    m_chessScene->clearNetworkMode();
    m_chessScene->setParticipants(makeAIParticipants(aiColor));
    m_chessScene->setAIMode(m_selectedDifficulty, aiColor);
    CardGFX::scenes().push(m_chessScene);
}

// ── Profiles / History ───────────────────────────────────────────────

void LobbyScene::showProfilesMenu() {
    m_state = LobbyState::Profiles;
    m_nameInput.setVisible(false);
    m_titleLabel.setVisible(false);
    m_statusBar.setLeft("Profiles");
    m_statusBar.setCenter("");
    m_statusBar.setRight("Esc=Back");

    const PlayerProfile* active = GameRecords::activeProfile(m_profiles);
    char message[32];
    snprintf(message, sizeof(message), "Active: %s",
             active ? active->name : "Player");

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Profiles");
    m_menuModal.setMessage(message);
    m_menuModal.setEscapeCallback([this]() {
        m_menuModal.hide();
        showMenu();
    });

    for (uint8_t i = 0; i < m_profiles.profileCount; ++i) {
        char label[24];
        snprintf(label, sizeof(label), "%c %s",
                 i == m_profiles.activeIndex ? '*' : ' ',
                 m_profiles.profiles[i].name);
        const uint8_t index = i;
        m_menuModal.addButton(label, [this, index]() {
            m_menuModal.hide();
            showProfileActions(index);
        });
    }
    if (m_profiles.profileCount < MAX_PLAYER_PROFILES) {
        m_menuModal.addButton("New Profile", [this]() {
            m_menuModal.hide();
            beginProfileNameEdit(NameEditMode::NewProfile);
        });
    }

    char historyLabel[24];
    snprintf(historyLabel, sizeof(historyLabel), "History (%u)",
             static_cast<unsigned>(m_profiles.historyCount));
    m_menuModal.addButton(historyLabel, [this]() {
        m_historyPage = 0;
        m_menuModal.hide();
        showHistoryMenu();
    });
    m_menuModal.addButton("Back", [this]() {
        m_menuModal.hide();
        showMenu();
    });
    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::showProfileActions(uint8_t profileIndex) {
    if (profileIndex >= m_profiles.profileCount) {
        showProfilesMenu();
        return;
    }
    m_selectedProfileIndex = profileIndex;
    m_state = LobbyState::ProfileActions;
    const PlayerProfile& profile = m_profiles.profiles[profileIndex];

    char message[64];
    snprintf(message, sizeof(message), "Games %lu  W%lu L%lu D%lu",
             static_cast<unsigned long>(profile.games),
             static_cast<unsigned long>(profile.wins),
             static_cast<unsigned long>(profile.losses),
             static_cast<unsigned long>(profile.draws));

    m_menuModal.clearButtons();
    m_menuModal.setTitle(profile.name);
    m_menuModal.setMessage(message);
    m_menuModal.setEscapeCallback([this]() {
        m_menuModal.hide();
        showProfilesMenu();
    });
    if (profileIndex != m_profiles.activeIndex) {
        m_menuModal.addButton("Use Profile", [this]() {
            m_menuModal.hide();
            activateSelectedProfile();
        });
    }
    m_menuModal.addButton("Rename", [this]() {
        m_menuModal.hide();
        beginProfileNameEdit(NameEditMode::RenameProfile);
    });
    // Deleting any profile while Resume exists can orphan the participant ID
    // stored with that game and lose its eventual statistics update.
    if (m_profiles.profileCount > 1 && !m_resumeAvailable) {
        m_menuModal.addButton("Delete", [this]() {
            m_menuModal.hide();
            showDeleteProfileConfirm();
        });
    }
    m_menuModal.addButton("Back", [this]() {
        m_menuModal.hide();
        showProfilesMenu();
    });
    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::activateSelectedProfile() {
    if (m_selectedProfileIndex >= m_profiles.profileCount) {
        showProfilesMenu();
        return;
    }
    const ProfileData previous = m_profiles;
    if (!GameRecords::setActiveProfile(m_profiles, m_selectedProfileIndex) ||
        !ProfileStorage::save(m_profiles)) {
        m_profiles = previous;
        showProfileActions(m_selectedProfileIndex);
        m_menuModal.setMessage("Could not save profile.");
        return;
    }
    syncActiveProfileName();
    showProfilesMenu();
}

void LobbyScene::showDeleteProfileConfirm() {
    if (m_selectedProfileIndex >= m_profiles.profileCount ||
        m_profiles.profileCount <= 1) {
        showProfilesMenu();
        return;
    }
    m_state = LobbyState::ProfileDeleteConfirm;
    char message[48];
    snprintf(message, sizeof(message), "Delete %s? History stays.",
             m_profiles.profiles[m_selectedProfileIndex].name);

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Delete Profile");
    m_menuModal.setMessage(message);
    m_menuModal.setEscapeCallback([this]() {
        m_menuModal.hide();
        showProfileActions(m_selectedProfileIndex);
    });
    m_menuModal.addButton("Delete", [this]() {
        m_menuModal.hide();
        deleteSelectedProfile();
    });
    m_menuModal.addButton("Cancel", [this]() {
        m_menuModal.hide();
        showProfileActions(m_selectedProfileIndex);
    });
    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::deleteSelectedProfile() {
    if (m_selectedProfileIndex >= m_profiles.profileCount ||
        m_profiles.profileCount <= 1) {
        showProfilesMenu();
        return;
    }
    const ProfileData previous = m_profiles;
    if (!GameRecords::deleteProfile(m_profiles, m_selectedProfileIndex) ||
        !ProfileStorage::save(m_profiles)) {
        m_profiles = previous;
        showProfileActions(m_selectedProfileIndex);
        m_menuModal.setMessage("Could not delete profile.");
        return;
    }
    syncActiveProfileName();
    m_selectedProfileIndex = 0;
    showProfilesMenu();
}

void LobbyScene::beginProfileNameEdit(NameEditMode mode) {
    if (mode == NameEditMode::RenameProfile &&
        m_selectedProfileIndex >= m_profiles.profileCount) {
        showProfilesMenu();
        return;
    }
    if (mode == NameEditMode::NewProfile &&
        m_profiles.profileCount >= MAX_PLAYER_PROFILES) {
        showProfilesMenu();
        return;
    }

    m_nameEditMode = mode;
    m_state = LobbyState::ProfileNameEdit;
    m_menuModal.hide();
    m_statusBar.setLeft("Profiles");
    m_statusBar.setCenter("");
    m_statusBar.setRight("Esc=Cancel");
    m_titleLabel.setText(mode == NameEditMode::NewProfile
        ? "New Profile" : "Rename Profile");
    m_titleLabel.setVisible(true);
    m_statusLabel.setText("Enter name (12 chars)");

    if (mode == NameEditMode::RenameProfile) {
        m_nameInput.setText(
            m_profiles.profiles[m_selectedProfileIndex].name);
    } else {
        char suggested[PLAYER_NAME_MAX + 1];
        snprintf(suggested, sizeof(suggested), "Player %u",
                 static_cast<unsigned>(m_profiles.profileCount + 1));
        m_nameInput.setText(suggested);
    }
    m_nameInput.setVisible(true);
    focusChain().focusWidget(&m_nameInput);
}

void LobbyScene::finishProfileNameEdit(const char* name) {
    if (m_state != LobbyState::ProfileNameEdit) return;

    char clean[PLAYER_NAME_MAX + 1];
    if (!GameRecords::sanitizeName(name, clean)) {
        m_statusLabel.setText("Name cannot be blank.");
        return;
    }

    const ProfileData previous = m_profiles;
    bool changed = false;
    if (m_nameEditMode == NameEditMode::NewProfile) {
        changed = GameRecords::addProfile(
            m_profiles, ProfileStorage::createUniqueProfileId(m_profiles),
            clean);
    } else {
        changed = GameRecords::renameProfile(
            m_profiles, m_selectedProfileIndex, clean);
    }

    if (!changed || !ProfileStorage::save(m_profiles)) {
        m_profiles = previous;
        m_statusLabel.setText("Could not save profile.");
        return;
    }

    syncActiveProfileName();
    m_nameInput.setVisible(false);
    m_statusLabel.setText("");
    showProfilesMenu();
}

void LobbyScene::cancelProfileNameEdit() {
    if (m_state != LobbyState::ProfileNameEdit) return;
    m_nameInput.setVisible(false);
    m_statusLabel.setText("");
    if (m_nameEditMode == NameEditMode::RenameProfile) {
        showProfileActions(m_selectedProfileIndex);
    } else {
        showProfilesMenu();
    }
}

void LobbyScene::showHistoryMenu() {
    m_state = LobbyState::History;
    m_titleLabel.setVisible(false);
    m_statusBar.setLeft("History");
    m_statusBar.setCenter("");
    m_statusBar.setRight("Esc=Back");

    const uint8_t pageCount = m_profiles.historyCount == 0 ? 1
        : static_cast<uint8_t>(
            (m_profiles.historyCount + HISTORY_PAGE_SIZE - 1) /
            HISTORY_PAGE_SIZE);
    if (m_historyPage >= pageCount) m_historyPage = pageCount - 1;
    const uint8_t first = m_historyPage * HISTORY_PAGE_SIZE;

    char title[24];
    snprintf(title, sizeof(title), "History %u/%u",
             static_cast<unsigned>(m_historyPage + 1),
             static_cast<unsigned>(pageCount));
    m_menuModal.clearButtons();
    m_menuModal.setTitle(title);
    m_menuModal.setMessage(m_profiles.historyCount == 0
        ? "No completed games." : "Newest first:");
    m_menuModal.setEscapeCallback([this]() {
        m_menuModal.hide();
        showProfilesMenu();
    });

    for (uint8_t offset = 0; offset < HISTORY_PAGE_SIZE; ++offset) {
        const uint8_t index = first + offset;
        if (index >= m_profiles.historyCount) break;
        const GameSummary& summary = m_profiles.history[index];
        char label[24];
        snprintf(label, sizeof(label), "%u %c %.5s-%.5s %s",
                 static_cast<unsigned>(index + 1),
                 gameModeLetter(summary.mode),
                 summary.whiteName, summary.blackName,
                 GameRecords::outcomeShortName(summary.outcome));
        m_menuModal.addButton(label, [this, index]() {
            m_menuModal.hide();
            showHistoryDetail(index);
        });
    }
    if (m_historyPage > 0) {
        m_menuModal.addButton("Previous", [this]() {
            m_menuModal.hide();
            --m_historyPage;
            showHistoryMenu();
        });
    }
    if (first + HISTORY_PAGE_SIZE < m_profiles.historyCount) {
        m_menuModal.addButton("Next", [this]() {
            m_menuModal.hide();
            ++m_historyPage;
            showHistoryMenu();
        });
    }
    m_menuModal.addButton("Back", [this]() {
        m_menuModal.hide();
        showProfilesMenu();
    });
    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::showHistoryDetail(uint8_t historyIndex) {
    if (historyIndex >= m_profiles.historyCount) {
        showHistoryMenu();
        return;
    }
    m_state = LobbyState::HistoryDetail;
    const GameSummary& summary = m_profiles.history[historyIndex];
    char title[24];
    snprintf(title, sizeof(title), "Game %u",
             static_cast<unsigned>(historyIndex + 1));
    char message[80];
    snprintf(message, sizeof(message),
             "%s vs %s\n%s, %s\n%s %s %s, %u ply",
             summary.whiteName, summary.blackName,
             GameRecords::outcomeShortName(summary.outcome),
             GameRecords::terminationShortName(summary.termination),
             gameModeName(summary.mode), variantShortName(summary.variant),
             timeControlShortName(summary.timeControl),
             static_cast<unsigned>(summary.plyCount));

    m_menuModal.clearButtons();
    m_menuModal.setTitle(title);
    m_menuModal.setMessage(message);
    m_menuModal.setEscapeCallback([this]() {
        m_menuModal.hide();
        showHistoryMenu();
    });
    m_menuModal.addButton("Back", [this]() {
        m_menuModal.hide();
        showHistoryMenu();
    });
    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::startHosting() {
    if (!requirePersistentState()) return;
    m_state = LobbyState::Hosting;
    m_localColor = PieceColor::White;
    m_titleLabel.setVisible(false);

    auto& transport = EspNowTransport::instance();
    if (!transport.init()) {
        showMenu();
        showTransportError("ESP-NOW init");
        return;
    }

    ensureLocalPlayerName();
    m_gameId = randomGameId();
    m_sessionId = 0;
    memset(m_peerMac, 0, sizeof(m_peerMac));
    memset(m_opponentName, 0, sizeof(m_opponentName));
    m_reackGameStart = false;
    m_lastBroadcast = 0;
    m_stateStartTime = millis();

    char hostLabel[16];
    char macStr[6];
    formatMacSuffix(transport.ownMac(), macStr);
    snprintf(hostLabel, sizeof(hostLabel), "Hosting [%s]", macStr);
    m_statusBar.setLeft(hostLabel);
    m_statusBar.setCenter("");
    m_statusBar.setRight("ESC=Back");
    m_statusLabel.setText("Waiting for opponent...");
}

void LobbyScene::startJoining() {
    if (!requirePersistentState()) return;
    m_state = LobbyState::Joining;
    m_localColor = PieceColor::Black;
    m_titleLabel.setVisible(false);

    auto& transport = EspNowTransport::instance();
    if (!transport.init()) {
        showMenu();
        showTransportError("ESP-NOW init");
        return;
    }

    ensureLocalPlayerName();
    m_gameId = 0;
    m_sessionId = 0;
    memset(m_peerMac, 0, sizeof(m_peerMac));
    memset(m_opponentName, 0, sizeof(m_opponentName));
    m_reackGameStart = false;
    m_hostCount = 0;
    m_hostListDirty = false;
    m_connecting = false;
    m_stateStartTime = millis();
    m_statusBar.setLeft("Join");
    m_statusBar.setCenter("");
    m_statusBar.setRight("ESC=Back");
    m_statusLabel.setText("Searching for host...");
}

void LobbyScene::connectToHost(uint8_t hostIdx) {
    if (hostIdx >= m_hostCount) return;
    auto& host = m_hosts[hostIdx];
    auto& transport = EspNowTransport::instance();

    m_gameId = host.gameId;
    m_selectedVariant = host.variant;
    m_positionIndex = host.positionIndex;
    m_selectedTimeControl = host.timeControl;
    memcpy(m_peerMac, host.mac, 6);
    copyRemoteNameOrDefault(m_opponentName, host.displayName, host.mac);
    if (!transport.addPeer(host.mac)) {
        showTransportError("Peer setup");
        return;
    }

    m_pendingAccept = AcceptGameMsg();
    m_pendingAccept.header.gameId = m_gameId;
    copyNetDisplayName(m_pendingAccept.displayName, m_localPlayerName);
    m_acceptRetries = 0;
    m_lastAcceptSendTime = 0;

    m_menuModal.hide();
    m_connecting = true;
    m_stateStartTime = millis();
    m_statusLabel.setText("Connecting...");
    sendPendingAccept();
}

void LobbyScene::sendPendingAccept() {
    auto& transport = EspNowTransport::instance();
    m_lastAcceptSendTime = millis();
    ++m_acceptRetries;
    if (!transport.send(reinterpret_cast<const uint8_t*>(&m_pendingAccept),
                        sizeof(m_pendingAccept))) {
        showTransportError("Join request");
    } else {
        m_statusLabel.setText("Connecting...");
    }
}

void LobbyScene::rebuildHostList() {
    m_menuModal.clearButtons();
    m_menuModal.setTitle("Join Game");
    m_menuModal.setEscapeCallback([this]() {
        m_menuModal.hide();
        cancelPairing();
    });

    if (m_hostCount == 0) {
        m_menuModal.setMessage("Searching for hosts...");
    } else {
        char msgBuf[24];
        snprintf(msgBuf, sizeof(msgBuf), "%d host%s found",
                 m_hostCount, m_hostCount == 1 ? "" : "s");
        m_menuModal.setMessage(msgBuf);
    }

    for (uint8_t i = 0; i < m_hostCount; i++) {
        char label[40];
        char macStr[6];
        formatMacSuffix(m_hosts[i].mac, macStr);
        const char* playerName = m_hosts[i].displayName[0] != '\0'
                               ? m_hosts[i].displayName : "Player";
        // Modal button labels hold 23 visible characters. Keep a useful name,
        // MAC suffix, variant, and clock within that limit.
        snprintf(label, sizeof(label), "%.7s %s %s %s",
                 playerName, macStr,
                 variantShortName(m_hosts[i].variant),
                 timeControlShortName(m_hosts[i].timeControl));
        uint8_t idx = i; // capture by value
        m_menuModal.addButton(label, [this, idx]() {
            connectToHost(idx);
        });
    }

    m_menuModal.addButton("Back", [this]() {
        m_menuModal.hide();
        cancelPairing();
    });

    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
    m_hostListDirty = false;
}

void LobbyScene::cancelPairing() {
    EspNowTransport::instance().shutdown();
    showMenu();
}

void LobbyScene::onPaired() {
    if (!requirePersistentState()) {
        EspNowTransport::instance().shutdown();
        return;
    }
    m_state = LobbyState::Paired;
    m_statusLabel.setText("Connected!");

    configureChessScene();
    m_chessScene->setNetworkMode(m_localColor, m_sessionId, m_gameId,
                                 m_peerMac, m_localPlayerName, m_opponentName,
                                 m_reackGameStart);
    CardGFX::scenes().push(m_chessScene);
}

void LobbyScene::onTick(uint32_t /*dt_ms*/) {
    auto& transport = EspNowTransport::instance();
    const uint32_t now = millis();

    if (m_state == LobbyState::Hosting) {
        // Broadcast discovery every 500ms
        if (now - m_lastBroadcast > 500) {
            m_lastBroadcast = now;
            DiscoveryMsg disc;
            disc.header.gameId = m_gameId;
            disc.variant = static_cast<uint8_t>(m_selectedVariant);
            disc.positionIndex = m_positionIndex;
            disc.timeControl = static_cast<uint8_t>(m_selectedTimeControl);
            copyNetDisplayName(disc.displayName, m_localPlayerName);
            if (!transport.broadcast(
                    reinterpret_cast<const uint8_t*>(&disc), sizeof(disc))) {
                showTransportError("Discovery");
            } else {
                m_statusLabel.setText("Waiting for opponent...");
            }
        }

        // Check for AcceptGame response
        uint8_t buf[NET_PACKET_MAX_SIZE];
        uint8_t mac[6];
        while (transport.hasReceived()) {
            const uint8_t len = transport.receive(buf, sizeof(buf), mac);
            if (len == 0) break;

            if (len != sizeof(AcceptGameMsg) ||
                static_cast<NetMsgType>(buf[0]) != NetMsgType::AcceptGame ||
                !isValidRemoteMac(mac, transport.ownMac())) {
                continue;
            }

            AcceptGameMsg accept;
            memcpy(&accept, buf, sizeof(accept));
            if (!isValidPairingHeader(accept.header, NetMsgType::AcceptGame,
                                      m_gameId) ||
                !isValidNetDisplayName(accept.displayName)) {
                continue;
            }

            if (!transport.addPeer(mac)) {
                showTransportError("Peer setup");
                continue;
            }

            memcpy(m_peerMac, mac, sizeof(m_peerMac));
            copyRemoteNameOrDefault(m_opponentName, accept.displayName, mac);
            m_sessionId = randomSessionId();
            m_reackGameStart = false;

            m_pendingStart = GameStartMsg();
            m_pendingStart.header.gameId = m_gameId;
            m_pendingStart.header.sessionId = m_sessionId;
            m_pendingStart.yourColor =
                static_cast<uint8_t>(PieceColor::Black);
            m_pendingStart.variant =
                static_cast<uint8_t>(m_selectedVariant);
            m_pendingStart.positionIndex = m_positionIndex;
            m_pendingStart.timeControl =
                static_cast<uint8_t>(m_selectedTimeControl);

            if (!transport.send(
                    reinterpret_cast<const uint8_t*>(&m_pendingStart),
                    sizeof(m_pendingStart))) {
                showTransportError("Game start");
            } else {
                m_statusLabel.setText("Starting game...");
            }
            m_state = LobbyState::WaitingForStartAck;
            m_startAckRetries = 0;
            m_lastStartSendTime = millis();
            return;
        }

        if (now - m_stateStartTime > 60000) {
            cancelPairing();
            m_statusLabel.setText("Hosting timed out.");
            m_menuModal.setMessage("Hosting timed out.");
        }

    } else if (m_state == LobbyState::WaitingForStartAck) {
        // Host is waiting for joiner to acknowledge GameStart
        uint8_t buf[NET_PACKET_MAX_SIZE];
        uint8_t mac[6];
        while (transport.hasReceived()) {
            const uint8_t len = transport.receive(buf, sizeof(buf), mac);
            if (len == 0) break;

            if (!transport.isPeerMac(mac)) continue;

            const NetMsgType type = static_cast<NetMsgType>(buf[0]);
            if (len == sizeof(GameStartAckMsg) &&
                type == NetMsgType::GameStartAck) {
                GameStartAckMsg ack;
                memcpy(&ack, buf, sizeof(ack));
                if (isValidGameHeader(ack.header, NetMsgType::GameStartAck,
                                      m_gameId, m_sessionId)) {
                    onPaired();
                    return;
                }
            } else if (len == sizeof(AcceptGameMsg) &&
                       type == NetMsgType::AcceptGame) {
                // A retried AcceptGame means the joiner did not receive our
                // first start packet. Send it again immediately.
                AcceptGameMsg accept;
                memcpy(&accept, buf, sizeof(accept));
                if (isValidPairingHeader(accept.header,
                                         NetMsgType::AcceptGame, m_gameId) &&
                    isValidNetDisplayName(accept.displayName)) {
                    transport.send(
                        reinterpret_cast<const uint8_t*>(&m_pendingStart),
                        sizeof(m_pendingStart));
                    m_lastStartSendTime = now;
                }
            }
        }

        // Retransmit every 200ms, up to 25 retries (5 seconds total)
        if (now - m_lastStartSendTime > 200) {
            if (m_startAckRetries < 25) {
                if (!transport.send(
                        reinterpret_cast<const uint8_t*>(&m_pendingStart),
                        sizeof(m_pendingStart))) {
                    showTransportError("Game start");
                } else {
                    m_statusLabel.setText("Starting game...");
                }
                m_startAckRetries++;
                m_lastStartSendTime = now;
            } else {
                cancelPairing();
                m_statusLabel.setText("Game start timed out.");
                m_menuModal.setMessage("Game start timed out.");
                return;
            }
        }

    } else if (m_state == LobbyState::Joining) {
        uint8_t buf[NET_PACKET_MAX_SIZE];
        uint8_t mac[6];
        while (transport.hasReceived()) {
            const uint8_t len = transport.receive(buf, sizeof(buf), mac);
            if (len == 0) break;

            if (len == sizeof(DiscoveryMsg) &&
                static_cast<NetMsgType>(buf[0]) == NetMsgType::Discovery) {
                DiscoveryMsg disc;
                memcpy(&disc, buf, sizeof(disc));

                if (!isValidRemoteMac(mac, transport.ownMac()) ||
                    !isValidPairingHeader(disc.header,
                                          NetMsgType::Discovery) ||
                    !isValidChessVariantValue(disc.variant) ||
                    !isValidPositionIndex(disc.variant,
                                          disc.positionIndex) ||
                    !isValidTimeControlValue(disc.timeControl) ||
                    !isValidNetDisplayName(disc.displayName)) {
                    continue;
                }

                // A host is identified by both MAC and gameId. gameId alone
                // is deliberately small and can collide across devices.
                bool found = false;
                for (uint8_t i = 0; i < m_hostCount; i++) {
                    if (m_hosts[i].gameId == disc.header.gameId &&
                        sameMac(m_hosts[i].mac, mac)) {
                        auto& host = m_hosts[i];
                        char updatedName[NET_DISPLAY_NAME_BYTES];
                        copyRemoteNameOrDefault(updatedName,
                                                disc.displayName, mac);
                        const bool changed =
                            host.variant !=
                                static_cast<ChessVariant>(disc.variant) ||
                            host.positionIndex != disc.positionIndex ||
                            host.timeControl !=
                                static_cast<TimeControl>(disc.timeControl) ||
                            strcmp(host.displayName, updatedName) != 0;
                        host.lastSeen = now;
                        host.variant =
                            static_cast<ChessVariant>(disc.variant);
                        host.positionIndex = disc.positionIndex;
                        host.timeControl =
                            static_cast<TimeControl>(disc.timeControl);
                        copyNetDisplayName(host.displayName, updatedName);
                        if (changed) m_hostListDirty = true;
                        found = true;
                        break;
                    }
                }
                if (!found && m_hostCount < MAX_HOSTS) {
                    auto& host = m_hosts[m_hostCount];
                    memcpy(host.mac, mac, 6);
                    host.gameId = disc.header.gameId;
                    host.variant = static_cast<ChessVariant>(disc.variant);
                    host.positionIndex = disc.positionIndex;
                    host.timeControl =
                        static_cast<TimeControl>(disc.timeControl);
                    copyRemoteNameOrDefault(host.displayName,
                                            disc.displayName, mac);
                    host.lastSeen = now;
                    ++m_hostCount;
                    m_hostListDirty = true;
                }
            } else if (m_connecting && len == sizeof(GameStartMsg) &&
                       static_cast<NetMsgType>(buf[0]) == NetMsgType::GameStart) {
                GameStartMsg start;
                memcpy(&start, buf, sizeof(start));

                if (!transport.isPeerMac(mac) ||
                    !isValidGameHeader(start.header, NetMsgType::GameStart,
                                       m_gameId) ||
                    !isValidColorValue(start.yourColor) ||
                    !isValidChessVariantValue(start.variant) ||
                    !isValidPositionIndex(start.variant,
                                          start.positionIndex) ||
                    !isValidTimeControlValue(start.timeControl) ||
                    start.variant !=
                        static_cast<uint8_t>(m_selectedVariant) ||
                    start.positionIndex != m_positionIndex ||
                    start.timeControl !=
                        static_cast<uint8_t>(m_selectedTimeControl)) {
                    continue;
                }

                m_localColor = (start.yourColor == 0)
                              ? PieceColor::White : PieceColor::Black;
                m_sessionId = start.header.sessionId;
                m_reackGameStart = true;

                // Send acknowledgment back to host
                GameStartAckMsg ack;
                ack.header.gameId = m_gameId;
                ack.header.sessionId = m_sessionId;
                transport.send(
                    reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));

                m_menuModal.hide();
                onPaired();
                return;
            }
        }

        // AcceptGame is an application-level request and must be retried until
        // GameStart arrives; a successful radio callback alone is insufficient.
        if (m_connecting && now - m_lastAcceptSendTime > 250) {
            sendPendingAccept();
        }

        // Skip aging and modal rebuild while waiting for GameStart
        if (!m_connecting) {
            // Age out hosts not seen for 3 seconds
            for (uint8_t i = 0; i < m_hostCount; ) {
                if (now - m_hosts[i].lastSeen > 3000) {
                    // Shift remaining entries down
                    for (uint8_t j = i; j + 1 < m_hostCount; j++) {
                        m_hosts[j] = m_hosts[j + 1];
                    }
                    m_hostCount--;
                    m_hostListDirty = true;
                } else {
                    i++;
                }
            }

            // Rebuild modal when host list changes
            if (m_hostListDirty) {
                rebuildHostList();
            }

            // Show initial modal on first tick (no hosts yet)
            if (!m_menuModal.isVisible() && m_hostCount == 0) {
                rebuildHostList();
            }
        }

        if (now - m_stateStartTime > 60000) {
            m_menuModal.hide();
            cancelPairing();
            m_statusLabel.setText("Joining timed out.");
            m_menuModal.setMessage("Joining timed out.");
        }
    }
}

// ── Puzzle Menu ──────────────────────────────────────────────────────

void LobbyScene::showPuzzleMenu() {
    m_state = LobbyState::PuzzleCategory;
    m_statusBar.setLeft("Puzzles");
    m_statusBar.setCenter("");
    m_statusBar.setRight("Esc=Back");
    m_titleLabel.setVisible(false);

    PuzzleProgress progress;
    PuzzleStorage::loadProgress(progress);

    m_menuModal.clearButtons();
    m_menuModal.setTitle("Puzzles");
    m_menuModal.setEscapeCallback([this]() {
        m_menuModal.hide();
        showMenu();
    });

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

    // Categories — only show types that have puzzles
    uint16_t m1Count = puzzleCountByType(PuzzleType::MateIn1);
    uint16_t m2Count = puzzleCountByType(PuzzleType::MateIn2);
    uint16_t tCount  = puzzleCountByType(PuzzleType::Tactic);

    if (m1Count > 0) {
        char m1Buf[16];
        snprintf(m1Buf, sizeof(m1Buf), "Mate1 (%d)", m1Count);
        m_menuModal.addButton(m1Buf, [this]() {
            m_menuModal.hide();
            uint16_t idx = puzzleIndexByType(PuzzleType::MateIn1, 0);
            if (idx != 0xFFFF) startPuzzle((uint8_t)idx);
        });
    }
    if (m2Count > 0) {
        char m2Buf[16];
        snprintf(m2Buf, sizeof(m2Buf), "Mate2 (%d)", m2Count);
        m_menuModal.addButton(m2Buf, [this]() {
            m_menuModal.hide();
            uint16_t idx = puzzleIndexByType(PuzzleType::MateIn2, 0);
            if (idx != 0xFFFF) startPuzzle((uint8_t)idx);
        });
    }
    if (tCount > 0) {
        char tBuf[16];
        snprintf(tBuf, sizeof(tBuf), "Tactic (%d)", tCount);
        m_menuModal.addButton(tBuf, [this]() {
            m_menuModal.hide();
            uint16_t idx = puzzleIndexByType(PuzzleType::Tactic, 0);
            if (idx != 0xFFFF) startPuzzle((uint8_t)idx);
        });
    }
    m_menuModal.addButton("Back", [this]() {
        m_menuModal.hide();
        showMenu();
    });

    m_menuModal.show();
    focusChain().focusWidget(&m_menuModal);
}

void LobbyScene::startPuzzle(uint8_t index) {
    if (!requirePersistentState()) return;
    m_chessScene->setPuzzleMode(index);
    CardGFX::scenes().push(m_chessScene);
}

// ── Input ────────────────────────────────────────────────────────────

bool LobbyScene::onInput(const InputEvent& event) {
    if (!event.isDown()) return false;

    if (event.key == Key::ESCAPE) {
        if (m_state == LobbyState::ProfileNameEdit) {
            cancelProfileNameEdit();
            return true;
        }
        if (m_state == LobbyState::Hosting || m_state == LobbyState::Joining ||
            m_state == LobbyState::WaitingForStartAck) {
            cancelPairing();
            return true;
        }
    }

    return false;
}
