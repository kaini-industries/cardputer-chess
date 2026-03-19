#ifndef CHESS_SCENE_H
#define CHESS_SCENE_H

#include <cardgfx.h>
#include "chess_board.h"
#include "chess_rules.h"
#include "chess_ai.h"
#include "chess_net_protocol.h"

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
        GameOver           // Game ended
    };

    UIState m_uiState = UIState::SelectPiece;

    // ── Chess Engine State ────────────────────────────────────────
    ChessBoard m_board;

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

    // Network ack/retransmit state
    bool     m_awaitingAck = false;
    uint8_t  m_lastSentSeq = 0;
    uint32_t m_lastSendTime = 0;
    uint8_t  m_retryCount = 0;
    MoveNetMsg m_lastSentMove;    // For retransmission
    bool     m_disconnectShown = false;

    // ── Widgets ───────────────────────────────────────────────────
    StatusBar m_statusBar;
    Grid      m_boardGrid;
    Label     m_movesLabel;
    List      m_moveList;
    Label     m_hintBar;
    Modal     m_promotionModal;
    Modal     m_gameOverModal;

    // ── Methods ───────────────────────────────────────────────────
    void newGame();
    void onCellAction(uint8_t gridCol, uint8_t gridRow);
    void selectPiece(uint8_t col, uint8_t boardRow);
    void deselectPiece();
    void tryMove(uint8_t col, uint8_t boardRow);
    void executeMove(const Move& move);
    void undoLastMove();
    void updateStatusBar();
    void updateBoardHighlights();
    void addMoveToList(const char* san);
    void checkGameEnd();
    void showPromotionModal(const Move& baseMove);
    void showGameOverModal(const char* title, const char* message);
    void rebuildMoveList();

    // Network methods
    void pollNetwork();
    void sendMove(const Move& move);
    void onRemoteMoveReceived(const MoveNetMsg& msg);
    void onConnectionLost();
    void sendHeartbeat();

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
    // Called by LobbyScene to configure AI mode before pushing
    void setAIMode(AIDifficulty difficulty, PieceColor aiColor);
    void clearAIMode();

    // Called by LobbyScene to configure network mode before pushing
    void setNetworkMode(PieceColor localColor);
    void clearNetworkMode();

    // Cell renderer -- draws pieces and highlights on the board
    static void renderCell(Canvas& canvas, uint8_t col, uint8_t row,
                           int16_t cx, int16_t cy, uint8_t cellW, uint8_t cellH,
                           Grid::CellState state, const Theme& theme, void* ctx);
};

#endif // CHESS_SCENE_H
