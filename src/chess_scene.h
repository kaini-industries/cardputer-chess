#ifndef CHESS_SCENE_H
#define CHESS_SCENE_H

#include <cardgfx.h>
#include "chess_board.h"
#include "chess_rules.h"

using namespace CardGFX;

// =====================================================================
// ChessScene: pass-and-play chess game UI.
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
    static constexpr uint8_t MAX_HISTORY = 64;
    MoveRecord m_history[MAX_HISTORY];
    uint8_t    m_historyCount = 0;

    // Pending promotion
    Move m_pendingPromotion;

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

    // Cell renderer -- draws pieces and highlights on the board
    static void renderCell(Canvas& canvas, uint8_t col, uint8_t row,
                           int16_t cx, int16_t cy, uint8_t cellW, uint8_t cellH,
                           Grid::CellState state, const Theme& theme, void* ctx);
};

#endif // CHESS_SCENE_H
