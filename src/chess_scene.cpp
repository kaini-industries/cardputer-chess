#include "chess_scene.h"
#include "chess_rules.h"
#include "esp_now_transport.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

// ── SAN Formatting ───────────────────────────────────────────────────
// Must be called BEFORE the move is executed on the board.

static void moveToSAN(char* buf, uint8_t bufLen, const Move& move,
                      ChessBoard& board, bool isCapture) {
    if (bufLen < 12) { buf[0] = '\0'; return; }
    uint8_t i = 0;

    // Castling
    if (move.isCastle) {
        if (move.to.col > move.from.col) {
            strncpy(buf, "O-O", bufLen);
        } else {
            strncpy(buf, "O-O-O", bufLen);
        }
        i = strlen(buf);
        // Check/checkmate suffix after castling
        MoveRecord rec = board.makeMove(move);
        PieceColor side = board.sideToMove();
        if (ChessRules::isInCheck(board, side)) {
            MoveList legal;
            ChessRules::generateLegal(board, legal);
            buf[i++] = (legal.count == 0) ? '#' : '+';
        }
        board.unmakeMove(rec);
        buf[i] = '\0';
        return;
    }

    Piece piece = board.at(move.from.col, move.from.row);

    // Piece letter (omit for pawns)
    if (piece.type != PieceType::Pawn) {
        buf[i++] = piece.typeChar();

        // Disambiguation: check if another piece of the same type
        // can also reach the same destination square
        MoveList allMoves;
        ChessRules::generateLegal(board, allMoves);
        bool sameFile = false, sameRank = false, ambiguous = false;
        for (uint8_t m = 0; m < allMoves.count; m++) {
            const Move& other = allMoves.moves[m];
            if (other.to == move.to && other.from != move.from) {
                Piece otherPiece = board.at(other.from.col, other.from.row);
                if (otherPiece.type == piece.type) {
                    ambiguous = true;
                    if (other.from.col == move.from.col) sameFile = true;
                    if (other.from.row == move.from.row) sameRank = true;
                }
            }
        }
        if (ambiguous) {
            if (!sameFile) {
                buf[i++] = move.from.file();
            } else if (!sameRank) {
                buf[i++] = move.from.rank();
            } else {
                buf[i++] = move.from.file();
                buf[i++] = move.from.rank();
            }
        }
    } else if (isCapture) {
        // Pawn captures: prefix with origin file
        buf[i++] = move.from.file();
    }

    // Capture marker
    if (isCapture) {
        buf[i++] = 'x';
    }

    // Destination square
    buf[i++] = move.to.file();
    buf[i++] = move.to.rank();

    // Promotion
    if (move.promotion != PieceType::None) {
        buf[i++] = '=';
        const char chars[] = " PNBRQK";
        buf[i++] = chars[static_cast<uint8_t>(move.promotion)];
    }

    buf[i] = '\0';

    // Check/checkmate suffix
    MoveRecord rec = board.makeMove(move);
    PieceColor side = board.sideToMove();
    if (ChessRules::isInCheck(board, side)) {
        MoveList legal;
        ChessRules::generateLegal(board, legal);
        buf[i++] = (legal.count == 0) ? '#' : '+';
        buf[i] = '\0';
    }
    board.unmakeMove(rec);
}

// =====================================================================
// ChessScene Implementation
// =====================================================================

ChessScene::ChessScene() : Scene("chess") {}

void ChessScene::setup() {
    // ── Status Bar ────────────────────────────────────────────────
    m_statusBar.setBounds({0, 0, SCREEN_W, 12});
    m_statusBar.setDrawSeparator(true);
    addWidget(&m_statusBar);

    // ── Board Grid ────────────────────────────────────────────────
    m_boardGrid.setGridSize(8, 8);
    m_boardGrid.setCellSize(15, 15);
    m_boardGrid.setBounds({0, 12, 120, 120});
    m_boardGrid.setDrawBorder(false);
    m_boardGrid.setDrawGrid(false);
    m_boardGrid.setCellRenderer(renderCell);
    m_boardGrid.setContext(this);
    m_boardGrid.setOnAction([this](uint8_t col, uint8_t row) {
        onCellAction(col, row);
    });
    addWidget(&m_boardGrid, true); // focusable

    // ── Moves Label ───────────────────────────────────────────────
    m_movesLabel.setText("Moves");
    m_movesLabel.setBounds({120, 12, 120, 10});
    m_movesLabel.setAlign(Label::Align::Center);
    m_movesLabel.setDrawBg(true);
    addWidget(&m_movesLabel);

    // ── Move History List ─────────────────────────────────────────
    m_moveList.setBounds({120, 22, 120, 101});
    m_moveList.setAutoScroll(true);
    addWidget(&m_moveList); // not focusable -- display only

    // ── Hint Bar ──────────────────────────────────────────────────
    m_hintBar.setText("[U]ndo [N]ew");
    m_hintBar.setBounds({120, 123, 120, 12});
    m_hintBar.setAlign(Label::Align::Center);
    m_hintBar.setDrawBg(true);
    addWidget(&m_hintBar);

    // ── Promotion Modal ───────────────────────────────────────────
    m_promotionModal.setBounds({0, 0, SCREEN_W, SCREEN_H});
    m_promotionModal.setTitle("Promote to:");
    m_promotionModal.setVisible(false);
    addWidget(&m_promotionModal, true);

    // ── Game Over Modal ───────────────────────────────────────────
    m_gameOverModal.setBounds({0, 0, SCREEN_W, SCREEN_H});
    m_gameOverModal.setVisible(false);
    addWidget(&m_gameOverModal, true);

    // Start a new game
    newGame();
}

void ChessScene::onEnter() {
    // Focus the board grid
    focusChain().focusWidget(&m_boardGrid);
}

void ChessScene::onTick(uint32_t /*dt_ms*/) {
    if (m_netMode == NetworkMode::Online) {
        pollNetwork();
    }

    // AI turn: two-phase approach so "Thinking..." renders before search blocks.
    // Phase 1: set thinking flag → status bar updates → frame renders.
    // Phase 2 (next tick): run search → execute move.
    if (m_aiDifficulty != AIDifficulty::None &&
        m_uiState == UIState::SelectPiece &&
        m_board.sideToMove() == m_aiColor) {

        if (!m_aiThinking) {
            // Phase 1: mark thinking, let the frame render "Thinking..."
            m_aiThinking = true;
            updateStatusBar();
        } else {
            // Phase 2: run the search
            Move aiMove = ChessAI::findBestMove(m_board, m_aiDifficulty);
            m_aiThinking = false;

            // Validate move before executing (defense-in-depth)
            MoveList legal;
            ChessRules::generateLegal(m_board, legal);
            if (!legal.contains(aiMove)) {
                // Should never happen — fall back to first legal move
                if (legal.count > 0) {
                    aiMove = legal.moves[0];
                } else {
                    return; // No legal moves — game should have ended
                }
            }
            executeMove(aiMove);
        }
    }

    // Keep cursor coordinate in status bar current (skip when modal visible)
    if (!m_promotionModal.isVisible() && !m_gameOverModal.isVisible()) {
        updateStatusBar();
    }
}

bool ChessScene::onInput(const InputEvent& event) {
    if (!event.isDown()) return false;

    // If a modal is visible, let it handle input
    if (m_promotionModal.isVisible() || m_gameOverModal.isVisible()) {
        return false;
    }

    switch (event.key) {
    case 'u':
    case 'U':
        if (m_netMode != NetworkMode::Online) {
            undoLastMove();
            return true;
        }
        break;

    case 'n':
    case 'N':
        if (m_netMode == NetworkMode::Online) break; // Disabled in Online mode
        if (m_aiThinking) break; // Disabled while AI is thinking
        // New game -- show confirmation modal
        m_gameOverModal.setTitle("New Game?");
        m_gameOverModal.setMessage("Start a new game?");
        m_gameOverModal.clearButtons();
        m_gameOverModal.addButton("Yes", [this]() {
            m_gameOverModal.hide();
            if (m_aiDifficulty != AIDifficulty::None) clearAIMode();
            CardGFX::scenes().pop(); // Back to lobby
        });
        m_gameOverModal.addButton("No", [this]() {
            m_gameOverModal.hide();
            focusChain().focusWidget(&m_boardGrid);
        });
        m_gameOverModal.show();
        focusChain().focusWidget(&m_gameOverModal);
        return true;

    case 'r':
    case 'R':
        if (m_netMode == NetworkMode::Online && m_uiState != UIState::GameOver) {
            // Resign confirmation
            m_gameOverModal.setTitle("Resign?");
            m_gameOverModal.setMessage("Give up this game?");
            m_gameOverModal.clearButtons();
            m_gameOverModal.addButton("Yes", [this]() {
                m_gameOverModal.hide();
                // Send resign to opponent
                ResignMsg msg;
                EspNowTransport::instance().send(
                    reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
                // Show result
                const char* winner = (m_localColor == PieceColor::White)
                                    ? "Black wins!" : "White wins!";
                showGameOverModal("Resigned", winner);
            });
            m_gameOverModal.addButton("No", [this]() {
                m_gameOverModal.hide();
                focusChain().focusWidget(&m_boardGrid);
            });
            m_gameOverModal.show();
            focusChain().focusWidget(&m_gameOverModal);
            return true;
        }
        break;

    case Key::ESCAPE:
        if (m_uiState == UIState::ShowMoves) {
            deselectPiece();
            return true;
        }
        break;

    }

    return false;
}

// ── Game Logic ────────────────────────────────────────────────────

void ChessScene::newGame() {
    m_board.reset();
    m_uiState = UIState::SelectPiece;
    m_selectedSquare = NO_SQUARE;
    m_lastFrom = NO_SQUARE;
    m_lastTo = NO_SQUARE;
    m_historyCount = 0;
    m_legalMoves.clear();

    m_moveList.clearItems();
    m_boardGrid.clearAllFlags();
    // Start cursor on own king (e1 for White, e8 for Black)
    m_boardGrid.setCursor(toGridCol(4), toGridRow(0));
    updateStatusBar();
    m_boardGrid.markDirty();

    // Update hint bar for game mode
    if (m_netMode == NetworkMode::Online) {
        m_hintBar.setText("[R]esign");
    } else {
        m_hintBar.setText("[U]ndo [N]ew");
    }
}

void ChessScene::onCellAction(uint8_t gridCol, uint8_t gridRow) {
    if (m_uiState == UIState::GameOver || m_uiState == UIState::PromotionPending) {
        return;
    }

    // Convert grid coordinates to board coordinates (respects board flipping)
    uint8_t boardCol = toBoardCol(gridCol);
    uint8_t boardRow = toBoardRow(gridRow);

    if (m_uiState == UIState::SelectPiece) {
        selectPiece(boardCol, boardRow);
    } else if (m_uiState == UIState::ShowMoves) {
        // Check if clicking on a valid move destination
        bool isValidDest = false;
        for (uint8_t i = 0; i < m_legalMoves.count; i++) {
            if (m_legalMoves.moves[i].to.col == boardCol &&
                m_legalMoves.moves[i].to.row == boardRow) {
                isValidDest = true;
                break;
            }
        }

        if (isValidDest) {
            tryMove(boardCol, boardRow);
        } else if (boardCol == m_selectedSquare.col && boardRow == m_selectedSquare.row) {
            // Clicked the same piece — deselect
            deselectPiece();
        } else {
            // Try to select a different own piece
            Piece p = m_board.at(boardCol, boardRow);
            if (!p.empty() && p.color == m_board.sideToMove()) {
                deselectPiece();
                selectPiece(boardCol, boardRow);
            } else {
                deselectPiece();
            }
        }
    }
}

void ChessScene::selectPiece(uint8_t col, uint8_t boardRow) {
    Piece p = m_board.at(col, boardRow);
    if (p.empty() || p.color != m_board.sideToMove()) return;

    // In Online mode, can only move your own color
    if (m_netMode == NetworkMode::Online && p.color != m_localColor) return;

    // In AI mode, can't move the AI's pieces
    if (m_aiDifficulty != AIDifficulty::None && p.color == m_aiColor) return;

    m_selectedSquare = makeSquare(col, boardRow);

    // Get legal moves for this piece
    m_legalMoves.clear();
    ChessRules::getLegalMovesFrom(m_board, col, boardRow, m_legalMoves);

    if (m_legalMoves.count == 0) {
        // No legal moves for this piece — don't select it
        m_selectedSquare = NO_SQUARE;
        return;
    }

    m_uiState = UIState::ShowMoves;
    updateBoardHighlights();
}

void ChessScene::deselectPiece() {
    m_selectedSquare = NO_SQUARE;
    m_legalMoves.clear();
    m_uiState = UIState::SelectPiece;
    updateBoardHighlights();
}

void ChessScene::tryMove(uint8_t col, uint8_t boardRow) {
    // Find the matching legal move(s)
    // There might be multiple if this is a promotion (4 variants)
    Move baseMove;
    bool isPromotion = false;
    bool found = false;

    for (uint8_t i = 0; i < m_legalMoves.count; i++) {
        if (m_legalMoves.moves[i].to.col == col &&
            m_legalMoves.moves[i].to.row == boardRow) {
            baseMove = m_legalMoves.moves[i];
            if (baseMove.promotion != PieceType::None) {
                isPromotion = true;
            }
            found = true;
            break;
        }
    }

    if (!found) return;

    if (isPromotion) {
        showPromotionModal(baseMove);
    } else {
        executeMove(baseMove);
    }
}

void ChessScene::executeMove(const Move& move) {
    // Format SAN before the move is applied (needs pre-move board state)
    bool isCapture = !m_board.at(move.to.col, move.to.row).empty() || move.isEnPassant;
    char sanBuf[12];
    moveToSAN(sanBuf, sizeof(sanBuf), move, m_board, isCapture);

    // Store history for undo
    if (m_historyCount < MAX_HISTORY) {
        m_history[m_historyCount] = m_board.makeMove(move);
        m_historyCount++;
    } else {
        m_board.makeMove(move); // Can't undo if history is full
    }

    // Track last move for highlighting
    m_lastFrom = move.from;
    m_lastTo = move.to;

    // Send move to remote player (only if this was a local move)
    if (m_netMode == NetworkMode::Online && !m_applyingRemoteMove) {
        sendMove(move);
    }

    // Update UI
    deselectPiece();
    addMoveToList(sanBuf);
    updateStatusBar();
    updateBoardHighlights();

    // Check for game end
    checkGameEnd();

    // Auto-rotate board in local pass-and-play mode (not AI mode)
    if (m_netMode == NetworkMode::Local && m_aiDifficulty == AIDifficulty::None) {
        m_boardFlipped = (m_board.sideToMove() == PieceColor::Black);
        m_boardGrid.setCursor(toGridCol(m_lastTo.col), toGridRow(m_lastTo.row));
        updateBoardHighlights();
    }
}

void ChessScene::undoLastMove() {
    if (m_netMode == NetworkMode::Online) return; // Disabled in network mode
    if (m_historyCount == 0) return;
    if (m_uiState == UIState::PromotionPending) return;
    if (m_aiThinking) return;

    // In AI mode, undo 2 moves (AI's move + human's move) to get back to human's turn
    uint8_t undoCount = 1;
    if (m_aiDifficulty != AIDifficulty::None && m_historyCount >= 2) {
        undoCount = 2;
    }

    for (uint8_t u = 0; u < undoCount && m_historyCount > 0; u++) {
        m_historyCount--;
        m_board.unmakeMove(m_history[m_historyCount]);
    }

    // Update last move markers
    if (m_historyCount > 0) {
        m_lastFrom = m_history[m_historyCount - 1].move.from;
        m_lastTo = m_history[m_historyCount - 1].move.to;
    } else {
        m_lastFrom = NO_SQUARE;
        m_lastTo = NO_SQUARE;
    }

    // Remove last entry from move list
    // We need to handle the paired display: if it was black's move, update the item;
    // if it was white's move, remove the item entirely
    if (m_moveList.itemCount() > 0) {
        // The current side to move after undo tells us what was undone
        if (m_board.sideToMove() == PieceColor::Black) {
            // We undid white's move — remove the whole line
            // Rebuild the list from scratch (simplest approach)
            rebuildMoveList();
        } else {
            // We undid black's move — revert line to just white's move
            rebuildMoveList();
        }
    }

    m_uiState = UIState::SelectPiece;
    deselectPiece();
    updateStatusBar();

    // Update flip for local pass-and-play mode after undo (not AI mode)
    if (m_aiDifficulty == AIDifficulty::None && m_netMode == NetworkMode::Local) {
        m_boardFlipped = (m_board.sideToMove() == PieceColor::Black);
    }
    updateBoardHighlights();
}

void ChessScene::updateStatusBar() {
    // Left: whose turn / online status / AI status
    if (m_aiThinking) {
        m_statusBar.setLeft("Thinking...");
    } else if (m_netMode == NetworkMode::Online) {
        const char* turn = (m_board.sideToMove() == m_localColor)
                          ? "Your turn" : "Waiting...";
        m_statusBar.setLeft(turn);
    } else if (m_aiDifficulty != AIDifficulty::None) {
        const char* turn = (m_board.sideToMove() == m_aiColor)
                          ? "AI turn" : "Your turn";
        m_statusBar.setLeft(turn);
    } else {
        const char* turn = (m_board.sideToMove() == PieceColor::White) ? "White" : "Black";
        m_statusBar.setLeft(turn);
    }

    // Center: move number
    char moveBuf[16];
    snprintf(moveBuf, sizeof(moveBuf), "Move %d", m_board.fullmoveNumber());
    m_statusBar.setCenter(moveBuf);

    // Right: check status or cursor coordinate
    if (m_uiState == UIState::GameOver) {
        // Already set by showGameOverModal
    } else if (ChessRules::isInCheck(m_board, m_board.sideToMove())) {
        m_statusBar.setRight("Check!");
    } else {
        // Show cursor board coordinate (e.g. "e4")
        uint8_t bc = toBoardCol(m_boardGrid.cursorCol());
        uint8_t br = toBoardRow(m_boardGrid.cursorRow());
        char posBuf[4];
        posBuf[0] = 'a' + bc;
        posBuf[1] = '1' + br;
        posBuf[2] = '\0';
        m_statusBar.setRight(posBuf);
    }
}

void ChessScene::updateBoardHighlights() {
    m_boardGrid.clearAllFlags();

    // Mark last move squares
    if (!isNoSquare(m_lastFrom)) {
        m_boardGrid.setMarked(toGridCol(m_lastFrom.col), toGridRow(m_lastFrom.row), true);
    }
    if (!isNoSquare(m_lastTo)) {
        m_boardGrid.setMarked(toGridCol(m_lastTo.col), toGridRow(m_lastTo.row), true);
    }

    // Show selected piece
    if (!isNoSquare(m_selectedSquare)) {
        m_boardGrid.setSelected(toGridCol(m_selectedSquare.col), toGridRow(m_selectedSquare.row), true);
    }

    // Show valid move destinations
    for (uint8_t i = 0; i < m_legalMoves.count; i++) {
        const Move& m = m_legalMoves.moves[i];
        // Only mark unique destination squares (promotion generates 4 moves to same square)
        m_boardGrid.setHighlighted(toGridCol(m.to.col), toGridRow(m.to.row), true);
    }

    m_boardGrid.markDirty();
}

void ChessScene::addMoveToList(const char* san) {
    // After the move is executed, sideToMove has already switched.
    // So if it's now black's turn, white just moved (start a new line).
    // If it's now white's turn, black just moved (append to existing line).
    if (m_board.sideToMove() == PieceColor::Black) {
        // White just moved — create new entry
        char line[32];
        snprintf(line, sizeof(line), "%d. %s", m_board.fullmoveNumber(), san);
        m_moveList.addItem(line);
    } else {
        // Black just moved — append to the last entry
        if (m_moveList.itemCount() > 0) {
            char line[32];
            const char* existing = m_moveList.getItem(m_moveList.itemCount() - 1);
            snprintf(line, sizeof(line), "%s %s", existing, san);
            m_moveList.setItem(m_moveList.itemCount() - 1, line);
        }
    }

    m_moveList.scrollToBottom();
}

void ChessScene::checkGameEnd() {
    if (ChessRules::isCheckmate(m_board)) {
        const char* winner = (m_board.sideToMove() == PieceColor::White)
                            ? "Black wins!" : "White wins!";
        m_statusBar.setRight("Mate!");
        showGameOverModal("Checkmate!", winner);
    } else if (ChessRules::isStalemate(m_board)) {
        m_statusBar.setRight("Draw");
        showGameOverModal("Stalemate!", "Game is a draw.");
    } else if (ChessRules::isDraw50Move(m_board)) {
        m_statusBar.setRight("Draw");
        showGameOverModal("50-Move Rule", "Game is a draw.");
    } else if (ChessRules::isInsufficientMaterial(m_board)) {
        m_statusBar.setRight("Draw");
        showGameOverModal("Insufficient", "Material for mate.");
    }
}

void ChessScene::showPromotionModal(const Move& baseMove) {
    m_pendingPromotion = baseMove;
    m_uiState = UIState::PromotionPending;

    auto finishPromotion = [this](PieceType pt) {
        m_pendingPromotion.promotion = pt;
        m_promotionModal.hide();
        m_uiState = UIState::SelectPiece;
        focusChain().focusWidget(&m_boardGrid);
        executeMove(m_pendingPromotion);
    };

    m_promotionModal.clearButtons();
    m_promotionModal.setTitle("Promote to:");
    m_promotionModal.setMessage("");

    m_promotionModal.addButton("Queen", [finishPromotion]() {
        finishPromotion(PieceType::Queen);
    });
    m_promotionModal.addButton("Knight", [finishPromotion]() {
        finishPromotion(PieceType::Knight);
    });
    m_promotionModal.addButton("Rook", [finishPromotion]() {
        finishPromotion(PieceType::Rook);
    });
    m_promotionModal.addButton("Bishop", [finishPromotion]() {
        finishPromotion(PieceType::Bishop);
    });

    m_promotionModal.show();
    focusChain().focusWidget(&m_promotionModal);
}

void ChessScene::showGameOverModal(const char* title, const char* message) {
    m_uiState = UIState::GameOver;

    m_gameOverModal.clearButtons();
    m_gameOverModal.setTitle(title);
    m_gameOverModal.setMessage(message);

    if (m_netMode == NetworkMode::Online) {
        m_gameOverModal.addButton("Lobby", [this]() {
            m_gameOverModal.hide();
            clearNetworkMode();
            CardGFX::scenes().pop();
        });
    } else {
        m_gameOverModal.addButton("Menu", [this]() {
            m_gameOverModal.hide();
            if (m_aiDifficulty != AIDifficulty::None) clearAIMode();
            CardGFX::scenes().pop(); // Back to lobby
        });
    }
    m_gameOverModal.addButton("View", [this]() {
        m_gameOverModal.hide();
        // Stay in GameOver state but let user view the board
        focusChain().focusWidget(&m_boardGrid);
    });

    m_gameOverModal.show();
    focusChain().focusWidget(&m_gameOverModal);
}

// ── Cell Renderer ─────────────────────────────────────────────────

void ChessScene::renderCell(Canvas& canvas, uint8_t col, uint8_t gridRow,
                             int16_t cx, int16_t cy, uint8_t cellW, uint8_t cellH,
                             Grid::CellState state, const Theme& theme, void* ctx) {
    ChessScene* self = static_cast<ChessScene*>(ctx);
    uint8_t boardCol = self->toBoardCol(col);
    uint8_t boardRow = self->toBoardRow(gridRow);

    // ── Cell background ───────────────────────────────────────────
    bool lightSquare = ((boardCol + boardRow) % 2 != 0);
    uint16_t cellBg = lightSquare ? theme.gridCellA : theme.gridCellB;

    if (state.marked)    cellBg = theme.accentMuted;
    if (state.highlight) cellBg = theme.gridHighlight;
    if (state.selected)  cellBg = theme.accentActive;

    canvas.fillRect(cx, cy, cellW, cellH, cellBg);

    // ── Valid move dot (on empty highlighted squares) ─────────────
    Piece piece = self->m_board.at(boardCol, boardRow);
    if (state.highlight && piece.empty() && !state.selected) {
        // Small dot in center
        int16_t dotCx = cx + cellW / 2;
        int16_t dotCy = cy + cellH / 2;
        canvas.fillCircle(dotCx, dotCy, 2, theme.bgPrimary);
    }

    // ── Piece rendering ───────────────────────────────────────────
    if (!piece.empty()) {
        char ch = piece.typeChar();

        // Center the character in the cell (scale 2 = 10x14 in a 15x15 cell)
        uint8_t scale = 2;
        uint16_t charW = FONT_CHAR_W * scale;
        uint16_t charH = FONT_CHAR_H * scale;
        int16_t px = cx + (cellW - charW) / 2;
        int16_t py = cy + (cellH - charH) / 2;
        // Clamp so outline (py-1) stays within cell
        if (py < cy + 1) py = cy + 1;

        // Draw outline for contrast
        uint16_t outlineColor;
        uint16_t fillColor;
        if (piece.color == PieceColor::White) {
            fillColor = HAL::rgb565(255, 255, 255);
            outlineColor = HAL::rgb565(0, 0, 0);
        } else {
            fillColor = HAL::rgb565(30, 30, 30);
            outlineColor = HAL::rgb565(200, 200, 200);
        }

        // Draw outline (4 directions)
        char text[2] = {ch, '\0'};
        canvas.drawText(px - 1, py, text, outlineColor, scale);
        canvas.drawText(px + 1, py, text, outlineColor, scale);
        canvas.drawText(px, py - 1, text, outlineColor, scale);
        canvas.drawText(px, py + 1, text, outlineColor, scale);

        // Draw the piece character
        canvas.drawText(px, py, text, fillColor, scale);
    }

    // ── Capture indicator (highlighted square with enemy piece) ───
    if (state.highlight && !piece.empty() && !state.selected) {
        // Draw corner triangles to indicate capture
        // Top-left corner
        for (int i = 0; i < 3; i++) {
            canvas.drawHLine(cx, cy + i, 3 - i, theme.error);
        }
        // Top-right corner
        for (int i = 0; i < 3; i++) {
            canvas.drawHLine(cx + cellW - 3 + i, cy + i, 3 - i, theme.error);
        }
        // Bottom-left corner
        for (int i = 0; i < 3; i++) {
            canvas.drawHLine(cx, cy + cellH - 1 - i, 3 - i, theme.error);
        }
        // Bottom-right corner
        for (int i = 0; i < 3; i++) {
            canvas.drawHLine(cx + cellW - 3 + i, cy + cellH - 1 - i, 3 - i, theme.error);
        }
    }

    // ── File/Rank labels (on empty edge cells only) ───────────────
    if (piece.empty()) {
        uint16_t labelColor = lightSquare ? theme.gridCellB : theme.gridCellA;

        // File labels (a-h) in bottom-right of bottom row
        if (gridRow == 7) {
            char buf[2] = {(char)('a' + boardCol), '\0'};
            canvas.drawText(cx + cellW - 6, cy + cellH - 8, buf, labelColor, 1);
        }

        // Rank labels (1-8) in top-left of left column
        if (col == 0) {
            char buf[2] = {(char)('1' + boardRow), '\0'};
            canvas.drawText(cx + 1, cy + 1, buf, labelColor, 1);
        }
    }

    // ── Cursor outline (only when no modal is covering the board) ─
    if (state.cursor &&
        !self->m_promotionModal.isVisible() &&
        !self->m_gameOverModal.isVisible()) {
        canvas.drawRect(cx, cy, cellW, cellH, theme.gridCursor);
        if (cellW > 4 && cellH > 4) {
            canvas.drawRect(cx + 1, cy + 1, cellW - 2, cellH - 2, theme.gridCursor);
        }
    }
}

// ── Helper: Rebuild Move List ─────────────────────────────────────
// Called after undo to reconstruct the display list from history

void ChessScene::rebuildMoveList() {
    m_moveList.clearItems();

    // Replay all moves to rebuild the list text
    ChessBoard tempBoard;
    tempBoard.reset();

    for (uint8_t i = 0; i < m_historyCount; i++) {
        const Move& move = m_history[i].move;
        bool isCapture = !m_history[i].captured.empty();
        char sanBuf[12];
        moveToSAN(sanBuf, sizeof(sanBuf), move, tempBoard, isCapture);

        tempBoard.makeMove(move);

        // Check if this was white's or black's move
        // After makeMove, side has switched. If now black's turn, white just moved.
        if (tempBoard.sideToMove() == PieceColor::Black) {
            // White just moved — new line
            char line[32];
            snprintf(line, sizeof(line), "%d. %s", tempBoard.fullmoveNumber(), sanBuf);
            m_moveList.addItem(line);
        } else {
            // Black just moved — append
            if (m_moveList.itemCount() > 0) {
                char line[32];
                const char* existing = m_moveList.getItem(m_moveList.itemCount() - 1);
                snprintf(line, sizeof(line), "%s %s", existing, sanBuf);
                m_moveList.setItem(m_moveList.itemCount() - 1, line);
            }
        }
    }

    if (m_moveList.itemCount() > 0) {
        m_moveList.scrollToBottom();
    }
}

// ── AI Mode ──────────────────────────────────────────────────────────

void ChessScene::setAIMode(AIDifficulty difficulty, PieceColor aiColor) {
    m_aiDifficulty = difficulty;
    m_aiColor = aiColor;
    m_aiThinking = false;
    // Human's pieces at the bottom
    PieceColor humanColor = opponent(aiColor);
    m_localColor = humanColor;
    m_boardFlipped = (humanColor == PieceColor::Black);
    newGame();
}

void ChessScene::clearAIMode() {
    m_aiDifficulty = AIDifficulty::None;
    m_aiThinking = false;
    m_boardFlipped = false;
    newGame();
}

// ── Network Mode ─────────────────────────────────────────────────────

void ChessScene::setNetworkMode(PieceColor localColor) {
    m_netMode = NetworkMode::Online;
    m_localColor = localColor;
    m_boardFlipped = (localColor == PieceColor::Black);
    m_awaitingAck = false;
    m_retryCount = 0;
    m_disconnectShown = false;
    newGame();
}

void ChessScene::clearNetworkMode() {
    m_netMode = NetworkMode::Local;
    m_localColor = PieceColor::White;
    m_boardFlipped = false;
    m_applyingRemoteMove = false;
    m_awaitingAck = false;
    m_disconnectShown = false;
    newGame();
}

void ChessScene::sendMove(const Move& move) {
    uint8_t seq = (uint8_t)(m_historyCount); // Use history count as sequence
    m_lastSentMove = moveToNetMsg(move, seq);
    m_lastSentSeq = seq;
    m_awaitingAck = true;
    m_retryCount = 0;
    m_lastSendTime = millis();

    EspNowTransport::instance().send(
        reinterpret_cast<const uint8_t*>(&m_lastSentMove), sizeof(m_lastSentMove));
}

void ChessScene::sendHeartbeat() {
    HeartbeatMsg hb;
    EspNowTransport::instance().send(
        reinterpret_cast<const uint8_t*>(&hb), sizeof(hb));
}

void ChessScene::pollNetwork() {
    auto& transport = EspNowTransport::instance();
    uint32_t now = millis();

    // Process received packets
    uint8_t buf[32];
    uint8_t mac[6];
    while (transport.hasReceived()) {
        uint8_t len = transport.receive(buf, sizeof(buf), mac);
        if (len == 0) break;

        // Dismiss disconnect modal if we get any packet
        if (m_disconnectShown) {
            m_disconnectShown = false;
            if (m_gameOverModal.isVisible()) {
                m_gameOverModal.hide();
                focusChain().focusWidget(&m_boardGrid);
            }
        }

        auto msgType = static_cast<NetMsgType>(buf[0]);

        switch (msgType) {
        case NetMsgType::MoveMsg:
            if (len >= sizeof(MoveNetMsg)) {
                MoveNetMsg moveMsg;
                memcpy(&moveMsg, buf, sizeof(MoveNetMsg));
                onRemoteMoveReceived(moveMsg);
            }
            break;

        case NetMsgType::MoveAck:
            if (len >= sizeof(MoveAckMsg)) {
                MoveAckMsg ack;
                memcpy(&ack, buf, sizeof(MoveAckMsg));
                if (m_awaitingAck && ack.seq == m_lastSentSeq) {
                    m_awaitingAck = false;
                }
            }
            break;

        case NetMsgType::Heartbeat:
            // Just the receive timestamp update is enough
            break;

        case NetMsgType::Resign: {
            const char* winner = (m_localColor == PieceColor::White)
                                ? "White wins!" : "Black wins!";
            showGameOverModal("Opponent Resigned", winner);
            break;
        }

        default:
            break;
        }
    }

    // Retransmit unacked moves
    if (m_awaitingAck && (now - m_lastSendTime) > 200) {
        m_retryCount++;
        if (m_retryCount > 25) { // ~5 seconds
            m_awaitingAck = false;
            onConnectionLost();
        } else {
            m_lastSendTime = now;
            transport.send(
                reinterpret_cast<const uint8_t*>(&m_lastSentMove),
                sizeof(m_lastSentMove));
        }
    }

    // Send heartbeat every second when idle
    if (!m_awaitingAck && (now - m_lastSendTime) > 1000) {
        m_lastSendTime = now;
        sendHeartbeat();
    }

    // Check for connection timeout (3 seconds without any packet)
    if (!m_disconnectShown && transport.msSinceLastReceive() > 3000 &&
        m_uiState != UIState::GameOver) {
        onConnectionLost();
    }
}

void ChessScene::onRemoteMoveReceived(const MoveNetMsg& msg) {
    // Send ack immediately
    MoveAckMsg ack;
    ack.seq = msg.seq;
    EspNowTransport::instance().send(
        reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));

    // Deduplicate: if we've already applied this move, skip
    if (msg.seq < m_historyCount) return;

    // Convert to Move and validate
    Move move = netMsgToMove(msg);

    MoveList legal;
    ChessRules::generateLegal(m_board, legal);
    if (!legal.contains(move)) {
        return; // Invalid move — ignore (shouldn't happen)
    }

    // Apply the remote move
    m_applyingRemoteMove = true;
    executeMove(move);
    m_applyingRemoteMove = false;
}

void ChessScene::onConnectionLost() {
    if (m_disconnectShown) return;
    m_disconnectShown = true;

    m_gameOverModal.clearButtons();
    m_gameOverModal.setTitle("Disconnected");
    m_gameOverModal.setMessage("Lost connection");

    m_gameOverModal.addButton("Wait", [this]() {
        m_gameOverModal.hide();
        m_disconnectShown = false;
        focusChain().focusWidget(&m_boardGrid);
    });
    m_gameOverModal.addButton("Quit", [this]() {
        m_gameOverModal.hide();
        clearNetworkMode();
        CardGFX::scenes().pop();
    });

    m_gameOverModal.show();
    focusChain().focusWidget(&m_gameOverModal);
}
