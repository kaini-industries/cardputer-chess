#ifndef CHESS_SCENE_H
#define CHESS_SCENE_H

#include <cardgfx.h>
#include "chess_board.h"
#include "chess_rules.h"
#include "chess_ai.h"
#include "chess_clock.h"
#include "chess_net_protocol.h"
#include "chess_storage.h"
#include "cursor_navigation.h"
#include "game_records.h"
#include "puzzle_data.h"
#include "puzzle_storage.h"

using namespace CardGFX;

// =====================================================================
// ChessScene: chess game UI with local and online multiplayer.
//
// Layout (240x135):
//   StatusBar     (0,0)   240x12  -- turn indicator, move number, status
//   Board Grid    (0,12)  120x120 -- 8x8 @ 15px cells
//   Moves Label   (120,12) 120x10 -- "Moves" header
//   Move List     (120,22) 120x101 -- scrollable move history
//   Hint Bar      (120,123) 120x12 -- keyboard shortcuts
// =====================================================================

class ChessScene : public Scene {
public:
    ChessScene();
    void setup();

    // Scene lifecycle
    void onEnter() override;
    void onTick(uint32_t dt_ms) override;
    bool onInput(const InputEvent& event) override;

private:
    // ── UI State Machine ──────────────────────────────────────────
    enum class UIState : uint8_t {
        SelectPiece,       // Cursor free, no piece selected
        ShowMoves,         // Piece selected, valid moves highlighted
        PromotionPending,  // Waiting for promotion choice
        GameOver,          // Game ended
        Reviewing,         // Stepping through move history
        ExitConfirm        // Confirming return to the lobby
    };

    UIState m_uiState = UIState::SelectPiece;
    UIState m_preExitState = UIState::SelectPiece;
    bool m_leavingToMenu = false;

    // ── Display Options ──────────────────────────────────────────
    bool m_useSprites = true;   // true=pixel art, false=text letters
    bool m_bwBoard = false;     // true=B&W squares, false=theme colors

    // ── Chess Engine State ────────────────────────────────────────
    ChessBoard m_board;
    // Display-only historical projection. Networking, persistence, and
    // terminal validation always retain m_board as the authoritative state.
    ChessBoard m_reviewBoard;

    // Selection
    Square   m_selectedSquare = NO_SQUARE;
    MoveList m_legalMoves;  // Legal moves for the selected piece

    // Last move (for marked squares)
    Square m_lastFrom = NO_SQUARE;
    Square m_lastTo   = NO_SQUARE;

    // Move history (for undo)
    static constexpr uint8_t MAX_HISTORY = 250;
    MoveRecord m_history[MAX_HISTORY];
    uint8_t    m_historyCount = 0;
    bool       m_historyOverflow = false;

    // Pending promotion
    Move m_pendingPromotion;

    // ── Move Animation ────────────────────────────────────────
    struct MoveAnim {
        bool active = false;
        uint32_t elapsed = 0;
        static constexpr uint32_t DURATION_MS = 250;
        Piece piece;
        bool isCapture = false;
        bool pendingFlip = false;  // Deferred board flip (local pass-and-play)
        int16_t fromPx, fromPy;   // Pre-computed grid pixel positions
        int16_t toPx, toPy;
    };
    MoveAnim m_moveAnim;

    // ── Game Variant ──────────────────────────────────────────
    ChessVariant m_variant = ChessVariant::Standard;
    uint16_t m_positionIndex = 518; // Chess960 position index

    // ── Timer State ──────────────────────────────────────────
    TimeControl m_timeControl = TimeControl::None;
    ChessClock m_clock;
    bool m_clockPausedForReview = false;
    bool m_clockPausedForExit = false;
    GameOutcome m_resultOutcome = GameOutcome::None;
    TerminationReason m_termination = TerminationReason::None;
    bool m_resultRecorded = false;
    bool m_terminalSummaryReady = false;
    bool m_terminalJournaled = false;
    GameSummary m_terminalSummary;
    uint32_t m_lastResultSaveAttempt = 0;
    uint32_t m_terminalDrainUntil = 0;
    ActiveGameParticipants m_participants;
    bool m_gameSaveDirty = false;
    uint32_t m_lastGameSaveAttempt = 0;

    // ── Review State ─────────────────────────────────────────
    uint8_t m_reviewIndex = 0;
    UIState m_preReviewState = UIState::GameOver;

    // ── Puzzle Mode ───────────────────────────────────────────
    bool m_puzzleMode = false;
    uint8_t m_puzzleIndex = 0;
    PuzzleType m_puzzleType = PuzzleType::Tactic;
    uint8_t m_puzzleSolutionStep = 0;
    uint8_t m_puzzleSolutionLen = 0;
    Move m_puzzleSolution[MAX_PUZZLE_SOLUTION_MOVES];
    PuzzleProgress m_puzzleProgress;
    bool m_puzzleAutoPlayPending = false;
    int32_t m_puzzleAutoPlayDelay = 0;
    uint8_t m_puzzleHintLevel = 0;

    // ── AI Mode ────────────────────────────────────────────────
    AIDifficulty m_aiDifficulty = AIDifficulty::None;
    PieceColor   m_aiColor = PieceColor::Black;
    bool         m_aiThinking = false;

    // ── Network Mode ────────────────────────────────────────────
    enum class NetworkMode : uint8_t { Local, Online };
    NetworkMode m_netMode = NetworkMode::Local;
    PieceColor  m_localColor = PieceColor::White;
    bool        m_boardFlipped = false;
    bool        m_applyingRemoteMove = false;

    // Session identity and peer metadata.
    uint16_t m_networkGameId = 0;
    uint32_t m_sessionId = 0;
    uint8_t  m_opponentMac[6] = {};
    char     m_opponentName[NET_DISPLAY_NAME_BYTES] = {};
    bool     m_reackGameStart = false;

    // Reliable move stream. Sequences are per-sender and independent of the
    // bounded local review history.
    bool       m_awaitingAck = false;
    uint16_t   m_nextMoveSequence = 1;
    uint16_t   m_lastSentSeq = 0;
    uint16_t   m_expectedRemoteSequence = 1;
    uint16_t   m_lastAppliedRemoteSequence = 0;
    uint32_t   m_lastRemotePreHash = 0;
    uint32_t   m_lastRemotePostHash = 0;
    uint32_t   m_lastMoveSendTime = 0;
    uint32_t   m_lastHeartbeatSendTime = 0;
    uint8_t    m_moveRetryCount = 0;
    MoveNetMsg m_lastSentMove;
    uint32_t   m_lastSentPostHash = 0;
    uint16_t   m_positionEpoch = 0;
    bool       m_missingRemoteMove = false;
    uint16_t   m_missingRemoteEpoch = 0;
    uint32_t   m_missingRemoteMoveSince = 0;

    // Reliable non-move controls (draws, time gifts, terminal results).
    bool          m_controlAwaitingAck = false;
    uint16_t      m_nextControlEventId = 1;
    uint16_t      m_expectedRemoteControlEventId = 1;
    uint16_t      m_lastRemoteControlEventId = 0;
    ControlNetMsg m_lastRemoteControl;
    NetAckStatus  m_lastRemoteControlStatus = NetAckStatus::Rejected;
    uint16_t      m_localDrawOfferEventId = 0;
    uint32_t      m_localDrawOfferBoardHash = 0;
    uint16_t      m_remoteDrawOfferEventId = 0;
    uint16_t      m_lastLocalDrawAcceptEventId = 0;
    bool          m_finishDrawOnAck = false;
    bool          m_clockPausedForDrawAccept = false;
    ControlNetMsg m_pendingControl;
    uint32_t      m_lastControlSendTime = 0;
    uint8_t       m_controlRetryCount = 0;
    bool          m_hasQueuedControl = false;
    ControlNetMsg m_queuedControl;

    // Time gifts commit remotely before they update the giver's mirror.
    bool       m_timeGiftPending = false;
    PieceColor m_timeGiftRecipient = PieceColor::White;
    bool       m_deferredLocalTimeout = false;
    PieceColor m_deferredFlaggedSide = PieceColor::White;

    // A received move carries the mover's authoritative post-increment clock.
    bool     m_remoteClockPending = false;
    uint32_t m_remoteMoverRemainingMs = 0;

    bool     m_disconnectShown = false;
    uint32_t m_disconnectGraceUntil = 0; // Grace period after "Wait" click
    // Updated only after a packet passes this game's session/header checks.
    // Raw frames from the same MAC (for example a new lobby broadcast) must
    // not keep an abandoned game looking connected.
    uint32_t m_lastValidPeerPacketTime = 0;

    // ── Widgets ───────────────────────────────────────────────────
    StatusBar m_statusBar;
    Grid      m_boardGrid;
    Label     m_movesLabel;
    List      m_moveList;
    Label     m_hintBar;
    Modal     m_promotionModal;
    Modal     m_gameOverModal;
    Modal     m_exitModal;
    Modal     m_actionModal;

    // ── Methods ───────────────────────────────────────────────────
    void newGame();
    void onCellAction(uint8_t gridCol, uint8_t gridRow);
    void selectPiece(uint8_t col, uint8_t boardRow);
    void deselectPiece();
    void tryMove(uint8_t col, uint8_t boardRow);
    void executeMove(const Move& move);
    void undoLastMove();
    void updateStatusBar();
    void updateHintBar();
    void updateMovesLabel();
    void updateBoardHighlights();
    void addMoveToList(const char* san);
    void checkGameEnd(bool notifyPeer = true,
                      uint32_t terminalTimestamp = 0);
    void showPromotionModal(const Move& baseMove);
    void showGameOverModal(const char* title, const char* message);
    void finishGame(GameOutcome outcome, TerminationReason termination,
                    const char* title, const char* message,
                    bool notifyPeer = true,
                    uint32_t terminalTimestamp = 0);
    bool recordCompletedGame();
    void captureTerminalSummary();
    void finishDeferredTimeoutIfReady();
    void showHelpModal();
    void closeActionModal();
    void cycleLegalMove();
    void requestExitToMenu();
    void cancelExitToMenu();
    void leaveToMenu(bool discardUnsavedResult = false);
    void leaveOnlineGame(bool force = false,
                         bool discardUnsavedResult = false);
    void showUnsavedResultModal(bool online);
    bool navigateBoardCursor(uint8_t key, uint8_t currentCol, uint8_t currentRow,
                             uint8_t& nextCol, uint8_t& nextRow);
    void rebuildMoveList();

    // Review mode
    void enterReviewMode();
    void exitReviewMode();
    void reviewGoTo(uint8_t index);

    // Timer helpers
    static void formatTime(char* buf, uint8_t bufLen, uint32_t ms);

    // Persistence
    bool saveGameState();

    // Network methods
    void pollNetwork();
    void sendMove(const Move& move, uint32_t preBoardHash,
                  uint32_t postBoardHash);
    void onRemoteMoveReceived(const MoveNetMsg& msg);
    void onConnectionLost(const char* message = "Lost connection");
    void noteValidPeerPacket(uint32_t now);
    void restoreGameInputFocus();
    void sendHeartbeat();
    void sendGameStartAck();
    void sendMoveAck(const MoveNetMsg& msg, NetAckStatus status,
                     uint32_t boardHash);
    bool sendControl(NetControlType control, uint8_t arg0 = 0,
                     uint8_t arg1 = 0, uint16_t relatedEventId = 0,
                     uint32_t valueMs = 0, bool replacePending = false);
    void sendQueuedControl(bool reusePendingEventId = false);
    void sendControlAck(const ControlNetMsg& msg, NetAckStatus status);
    void onControlReceived(const ControlNetMsg& msg);
    void offerDraw();
    void giveOpponentTime();
    void confirmResign();
    void sendGameEnd(GameOutcome outcome, TerminationReason termination,
                     uint16_t relatedEventId = 0);
    ControlNetMsg buildControl(NetControlType control, uint8_t arg0,
                               uint8_t arg1, uint16_t relatedEventId,
                               uint32_t valueMs) const;
    bool localMoveBlockedByControl() const;

    // Coordinate helpers (board flipping for Black perspective)
    uint8_t toGridRow(uint8_t boardRow) const {
        return m_boardFlipped ? boardRow : (7 - boardRow);
    }
    uint8_t toGridCol(uint8_t boardCol) const {
        return m_boardFlipped ? (7 - boardCol) : boardCol;
    }
    uint8_t toBoardRow(uint8_t gridRow) const {
        return m_boardFlipped ? gridRow : (7 - gridRow);
    }
    uint8_t toBoardCol(uint8_t gridCol) const {
        return m_boardFlipped ? (7 - gridCol) : gridCol;
    }

public:
    // Called by LobbyScene to set the game variant
    void setVariant(ChessVariant v);

    // Called by LobbyScene to set Chess960 position
    void setPositionIndex(uint16_t idx);

    // Called by LobbyScene to set time control
    void setTimeControl(TimeControl tc);

    // Called by LobbyScene before a local or AI game begins.
    void setParticipants(const ActiveGameParticipants& participants);

    // Called by LobbyScene to configure AI mode before pushing
    void setAIMode(AIDifficulty difficulty, PieceColor aiColor);
    void clearAIMode();

    // Called by LobbyScene to configure network mode before pushing
    void setNetworkMode(PieceColor localColor, uint32_t sessionId,
                        uint16_t gameId, const uint8_t opponentMac[6],
                        const char* localName, const char* opponentName,
                        bool reackGameStart);
    void clearNetworkMode();

    // Called by LobbyScene to resume a saved game
    bool loadSavedGame();

    // Called by LobbyScene to start puzzle mode
    void setPuzzleMode(uint8_t puzzleIndex);
    void clearPuzzleMode();

    // Cell renderer -- draws pieces and highlights on the board
    static void renderCell(Canvas& canvas, uint8_t col, uint8_t row,
                           int16_t cx, int16_t cy, uint8_t cellW, uint8_t cellH,
                           Grid::CellState state, const Theme& theme, void* ctx);
};

#endif // CHESS_SCENE_H
