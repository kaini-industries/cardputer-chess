#ifndef LOBBY_SCENE_H
#define LOBBY_SCENE_H

#include <cardgfx.h>
#include "chess_types.h"
#include "chess_ai.h"
#include "chess_net_protocol.h"
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
        Hosting,        // Broadcasting, waiting for joiner
        Joining,        // Listening for host broadcasts
        Paired,         // Connected, transitioning to game
        PuzzleCategory  // Choosing puzzle category
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

    // ── Chess960 State ──────────────────────────────────────────
    uint16_t m_positionIndex = 518;

    // ── Pairing State ────────────────────────────────────────────
    uint16_t m_gameId = 0;
    uint32_t m_lastBroadcast = 0;
    uint32_t m_stateStartTime = 0;
    uint8_t  m_peerMac[6] = {};
    PieceColor m_localColor = PieceColor::White;

    // ── Widgets ──────────────────────────────────────────────────
    StatusBar m_statusBar;
    Label     m_titleLabel;
    Label     m_statusLabel;
    Modal     m_menuModal;

    // ── Methods ──────────────────────────────────────────────────
    void showMenu();
    void showVariantMenu();
    void onVariantSelected();
    void showTimeControlMenu();
    void onTimeSelected();
    void showAIDifficultyMenu();
    void showAIColorMenu();
    void startAIGame(PieceColor aiColor);
    void startHosting();
    void startJoining();
    void onPaired();
    void cancelPairing();
    void startLocalGame();
    void configureChessScene();
    void showPuzzleMenu();
    void startPuzzle(uint8_t index);
};

#endif // LOBBY_SCENE_H
