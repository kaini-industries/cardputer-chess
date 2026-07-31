#ifndef LOBBY_SCENE_H
#define LOBBY_SCENE_H

#include <cardgfx.h>
#include "chess_types.h"
#include "chess_ai.h"
#include "chess_net_protocol.h"
#include "game_records.h"
#include "profile_storage.h"
#include "puzzle_data.h"
#include "puzzle_storage.h"

using namespace CardGFX;

class ChessScene; // Forward declaration

// =====================================================================
// LobbyScene: Pre-game screen for local/online mode selection,
// ESP-NOW discovery, and pairing.
// =====================================================================

class LobbyScene : public Scene {
public:
    LobbyScene();
    void setup(ChessScene* chessScene);
    // Profile integration point. Names are sanitized to at most 12 printable
    // ASCII characters before they are advertised over ESP-NOW.
    void setLocalPlayerName(const char* name);

    // Scene lifecycle
    void onEnter() override;
    void onExit() override;
    void onTick(uint32_t dt_ms) override;
    bool onInput(const InputEvent& event) override;

private:
    // ── State Machine ────────────────────────────────────────────
    enum class LobbyState : uint8_t {
        Menu,           // Showing mode selection
        VariantSelect,  // Choosing variant
        TimeSelect,     // Choosing time control
        AIDifficulty,   // Choosing AI difficulty
        AIColor,        // Choosing player color vs AI
        LocalWhiteSelect,
        LocalBlackSelect,
        Profiles,
        ProfileActions,
        ProfileDeleteConfirm,
        ProfileNameEdit,
        History,
        HistoryDetail,
        Hosting,        // Broadcasting, waiting for joiner
        Joining,        // Listening for host broadcasts
        Paired,             // Connected, transitioning to game
        WaitingForStartAck, // Host sent GameStart, waiting for ack
        PuzzleCategory      // Choosing puzzle category
    };

    LobbyState m_state = LobbyState::Menu;
    ChessScene* m_chessScene = nullptr;

    // ── Variant / Mode State ──────────────────────────────────────
    ChessVariant m_selectedVariant = ChessVariant::Standard;
    enum class PendingMode : uint8_t { Local, AI, Host };
    PendingMode m_pendingMode = PendingMode::Local;

    // ── Time Control State ────────────────────────────────────────
    TimeControl m_selectedTimeControl = TimeControl::None;

    // ── AI State ────────────────────────────────────────────────
    AIDifficulty m_selectedDifficulty = AIDifficulty::None;

    // ── Profiles / Participants ──────────────────────────────────
    static constexpr uint8_t GUEST_PROFILE_INDEX = 0xFF;
    static constexpr uint8_t HISTORY_PAGE_SIZE = 4;
    enum class NameEditMode : uint8_t { NewProfile, RenameProfile };

    ProfileData m_profiles;
    bool m_storageReady = false;
    bool m_resumeAvailable = false;
    const char* m_storageIssue = "Storage unavailable. No data changed.";
    uint8_t m_selectedProfileIndex = 0;
    uint8_t m_localWhiteProfileIndex = GUEST_PROFILE_INDEX;
    uint8_t m_historyPage = 0;
    NameEditMode m_nameEditMode = NameEditMode::NewProfile;

    // ── Chess960 State ──────────────────────────────────────────
    uint16_t m_positionIndex = 518;

    // ── Pairing State ────────────────────────────────────────────
    uint16_t m_gameId = 0;
    uint32_t m_lastBroadcast = 0;
    uint32_t m_stateStartTime = 0;
    uint8_t  m_peerMac[6] = {};
    PieceColor m_localColor = PieceColor::White;
    char m_localPlayerName[NET_DISPLAY_NAME_BYTES] = {};
    char m_opponentName[NET_DISPLAY_NAME_BYTES] = {};
    bool m_reackGameStart = false;

    // ── Game-Start Ack Retry State ───────────────────────────────
    uint8_t      m_startAckRetries = 0;
    uint32_t     m_lastStartSendTime = 0;
    uint32_t     m_sessionId = 0;
    GameStartMsg m_pendingStart;

    // ── Accept Retry State (joiner side) ─────────────────────────
    uint16_t      m_acceptRetries = 0;
    uint32_t      m_lastAcceptSendTime = 0;
    AcceptGameMsg m_pendingAccept;

    // ── Host Discovery (joiner side) ─────────────────────────────
    // Six hosts plus Back leaves room for the host-count/status message on the
    // 240x135 display. Eight buttons fit, but would silently hide that text.
    static constexpr uint8_t MAX_HOSTS = 6;
    struct DiscoveredHost {
        uint8_t mac[6];
        uint16_t gameId;
        ChessVariant variant;
        uint16_t positionIndex;
        TimeControl timeControl;
        char displayName[NET_DISPLAY_NAME_BYTES];
        uint32_t lastSeen;
    };
    DiscoveredHost m_hosts[MAX_HOSTS] = {};
    uint8_t m_hostCount = 0;
    bool    m_hostListDirty = false;
    bool    m_connecting = false;  // True after selecting a host, waiting for GameStart

    // ── Widgets ──────────────────────────────────────────────────
    StatusBar m_statusBar;
    Label     m_titleLabel;
    Label     m_statusLabel;
    Modal     m_menuModal;
    TextInput m_nameInput;

    // ── Methods ──────────────────────────────────────────────────
    void showMenu();
    void showVariantMenu();
    void onVariantSelected();
    void showTimeControlMenu();
    void onTimeSelected();
    void showAIDifficultyMenu();
    void showAIColorMenu();
    void startAIGame(PieceColor aiColor);
    void showLocalWhiteMenu();
    void showLocalBlackMenu();
    void startLocalGame(uint8_t blackProfileIndex);
    bool refreshPersistentState();
    bool requirePersistentState();
    void showStorageUnavailable();
    void markStorageUnavailable(const char* message);
    bool commitPendingResult(const GameSummary& pending);
    bool clearRecoveredGameState(const GameSummary& pending);
    void syncActiveProfileName();
    void showProfilesMenu();
    void showProfileActions(uint8_t profileIndex);
    void showDeleteProfileConfirm();
    void activateSelectedProfile();
    void deleteSelectedProfile();
    void beginProfileNameEdit(NameEditMode mode);
    void finishProfileNameEdit(const char* name);
    void cancelProfileNameEdit();
    void showHistoryMenu();
    void showHistoryDetail(uint8_t historyIndex);
    uint32_t createGameId() const;
    ActiveGameParticipants makeLocalParticipants(
        uint8_t blackProfileIndex) const;
    ActiveGameParticipants makeAIParticipants(PieceColor aiColor) const;
    void startHosting();
    void startJoining();
    void connectToHost(uint8_t hostIdx);
    void sendPendingAccept();
    void rebuildHostList();
    void onPaired();
    void cancelPairing();
    void configureChessScene();
    void ensureLocalPlayerName();
    void showTransportError(const char* action);
    void showPuzzleMenu();
    void startPuzzle(uint8_t index);
};

#endif // LOBBY_SCENE_H
