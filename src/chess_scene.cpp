#include "chess_scene.h"
#include "chess_rules.h"
#include "chess_sprites.h"
#include "esp_now_transport.h"
#include "puzzle_data.h"
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

void ChessScene::onTick(uint32_t dt_ms) {
    // ── Move animation tick ──────────────────────────────────────
    if (m_moveAnim.active) {
        m_moveAnim.elapsed += dt_ms;
        if (m_moveAnim.elapsed >= MoveAnim::DURATION_MS) {
            m_moveAnim.active = false;
            // Apply deferred board flip (local pass-and-play)
            if (m_moveAnim.pendingFlip) {
                m_moveAnim.pendingFlip = false;
                m_boardFlipped = (m_board.sideToMove() == PieceColor::Black);
                m_boardGrid.setCursor(toGridCol(m_lastTo.col), toGridRow(m_lastTo.row));
                updateBoardHighlights();
            }
        }
        m_boardGrid.markDirty();  // Force redraw every frame during animation
        if (m_gameOverModal.isVisible()) {
            m_gameOverModal.markDirty();
        }
    }

    // ── Timer countdown ──────────────────────────────────────
    if (m_timeControl != TimeControl::None && m_timerRunning &&
        m_uiState != UIState::GameOver && m_uiState != UIState::PromotionPending &&
        m_uiState != UIState::Reviewing && !m_moveAnim.active) {

        uint32_t& activeTime = (m_board.sideToMove() == PieceColor::White)
                               ? m_timeWhiteMs : m_timeBlackMs;

        if (activeTime <= dt_ms) {
            activeTime = 0;
            const char* winner = (m_board.sideToMove() == PieceColor::White)
                                ? "Black wins!" : "White wins!";
            showGameOverModal("Time's Up!", winner);
        } else {
            activeTime -= dt_ms;
        }
    }

    if (m_netMode == NetworkMode::Online) {
        pollNetwork();
    }

    // ── Puzzle auto-play opponent response ──────────────────
    if (m_puzzleMode && m_puzzleAutoPlayPending && !m_moveAnim.active) {
        m_puzzleAutoPlayDelay -= (int32_t)dt_ms;
        if (m_puzzleAutoPlayDelay <= 0) {
            m_puzzleAutoPlayPending = false;
            if (m_puzzleSolutionStep < m_puzzleSolutionLen) {
                Move responseMove = m_puzzleSolution[m_puzzleSolutionStep];
                // Find the full legal move with flags
                MoveList legal;
                ChessRules::generateLegal(m_board, legal);
                bool matched = false;
                for (uint8_t i = 0; i < legal.count; i++) {
                    if (legal.moves[i].from == responseMove.from &&
                        legal.moves[i].to == responseMove.to) {
                        responseMove = legal.moves[i];
                        matched = true;
                        break;
                    }
                }
                if (matched) {
                    executeMove(responseMove);
                }
            }
        }
    }

    // AI turn: two-phase approach so "Thinking..." renders before search blocks.
    // Phase 1: set thinking flag → status bar updates → frame renders.
    // Phase 2 (next tick): run search → execute move.
    // Skip while move animation is playing.
    if (m_aiDifficulty != AIDifficulty::None &&
        !m_moveAnim.active &&
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

    // Block input during move animation
    if (m_moveAnim.active) return true;

    // Review mode navigation
    if (m_uiState == UIState::Reviewing) {
        if (event.key == Key::ESCAPE) {
            exitReviewMode();
            return true;
        }
        if (event.key == ',' || event.key == '<') {
            if (m_reviewIndex > 0) reviewGoTo(m_reviewIndex - 1);
            return true;
        }
        if (event.key == '.' || event.key == '>') {
            if (m_reviewIndex < m_historyCount) reviewGoTo(m_reviewIndex + 1);
            return true;
        }
        return true; // Block all other input in review mode
    }

    // If a modal is visible, let it handle input
    if (m_promotionModal.isVisible() || m_gameOverModal.isVisible()) {
        return false;
    }

    // Puzzle mode keys
    if (m_puzzleMode) {
        if (event.key == 'h' || event.key == 'H') {
            // Hint: highlight solution source, then destination
            if (m_puzzleSolutionStep < m_puzzleSolutionLen) {
                const Move& sol = m_puzzleSolution[m_puzzleSolutionStep];
                if (m_puzzleHintLevel == 0) {
                    m_boardGrid.clearAllFlags();
                    updateBoardHighlights();
                    m_boardGrid.setHighlighted(toGridCol(sol.from.col), toGridRow(sol.from.row), true);
                    m_boardGrid.markDirty();
                    m_puzzleHintLevel = 1;
                } else {
                    m_boardGrid.setHighlighted(toGridCol(sol.to.col), toGridRow(sol.to.row), true);
                    m_boardGrid.markDirty();
                    m_puzzleHintLevel = 2;
                }
            }
            return true;
        }
        if (event.key == 's' || event.key == 'S') {
            // Skip to next puzzle
            uint8_t next = m_puzzleIndex + 1;
            if (next < puzzleCount()) {
                setPuzzleMode(next);
            } else {
                clearPuzzleMode();
                CardGFX::scenes().pop();
            }
            return true;
        }
        if (event.key == Key::ESCAPE) {
            if (m_uiState == UIState::ShowMoves) {
                deselectPiece();
                return true;
            }
            clearPuzzleMode();
            CardGFX::scenes().pop();
            return true;
        }
        // Block undo/new in puzzle mode — fall through to cell action only
    }

    switch (event.key) {
    case 't':
    case 'T':
        m_useSprites = !m_useSprites;
        m_boardGrid.markDirty();
        return true;

    case 'b':
    case 'B':
        m_bwBoard = !m_bwBoard;
        m_boardGrid.markDirty();
        return true;

    case 'v':
    case 'V':
        if (m_historyCount > 0 && !m_aiThinking && !m_puzzleMode) {
            enterReviewMode();
            return true;
        }
        break;

    case 'u':
    case 'U':
        if (m_puzzleMode) break;
        if (m_netMode != NetworkMode::Online) {
            undoLastMove();
            return true;
        }
        break;

    case 'n':
    case 'N':
        if (m_puzzleMode) break;
        if (m_netMode == NetworkMode::Online) break; // Disabled in Online mode
        if (m_aiThinking) break; // Disabled while AI is thinking
        // New game -- show confirmation modal
        m_gameOverModal.setTitle("New Game?");
        m_gameOverModal.setMessage("Start a new game?");
        m_gameOverModal.clearButtons();
        m_gameOverModal.addButton("Yes", [this]() {
            m_gameOverModal.hide();
            ChessStorage::clearSave();
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
    // Clear puzzle state (in case we're coming from puzzle mode)
    m_puzzleMode = false;
    m_puzzleAutoPlayPending = false;
    m_puzzleHintLevel = 0;

    // Cancel any in-progress animation
    m_moveAnim.active = false;
    m_moveAnim.pendingFlip = false;

    // Stop timer (setTimeControl will re-init if needed)
    m_timerRunning = false;

    m_board.setVariant(m_variant);
    m_board.setPositionIndex(m_positionIndex);
    m_board.reset();
    m_uiState = UIState::SelectPiece;
    m_selectedSquare = NO_SQUARE;
    m_lastFrom = NO_SQUARE;
    m_lastTo = NO_SQUARE;

    m_historyCount = 0;
    m_historyOverflow = false;
    m_legalMoves.clear();

    m_moveList.clearItems();
    m_movesLabel.setText("Moves");
    m_boardGrid.clearAllFlags();
    // Start cursor on the human's king
    uint8_t startRow = 0;
    if (m_aiDifficulty != AIDifficulty::None && m_aiColor == PieceColor::White) {
        startRow = 7;
    }
    m_boardGrid.setCursor(toGridCol(m_board.initKingCol()), toGridRow(startRow));
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

    // Set up move animation
    m_moveAnim.piece = m_board.at(move.from.col, move.from.row);
    // For promotions, show the promoted piece sliding
    if (move.promotion != PieceType::None) {
        m_moveAnim.piece.type = move.promotion;
    }
    m_moveAnim.isCapture = isCapture;
    m_moveAnim.fromPx = toGridCol(move.from.col) * 15;
    m_moveAnim.fromPy = toGridRow(move.from.row) * 15;
    m_moveAnim.toPx = toGridCol(move.to.col) * 15;
    m_moveAnim.toPy = toGridRow(move.to.row) * 15;
    m_moveAnim.elapsed = 0;
    m_moveAnim.active = true;

    // Store history for undo
    if (m_historyCount < MAX_HISTORY) {
        m_history[m_historyCount] = m_board.makeMove(move);
        m_historyCount++;
    } else {
        m_board.makeMove(move);
        m_historyOverflow = true; // Can no longer undo safely
    }

    // Track last move for highlighting
    m_lastFrom = move.from;
    m_lastTo = move.to;

    // Start timer after first move
    if (!m_timerRunning && m_timeControl != TimeControl::None) {
        m_timerRunning = true;
    }
    // Timer: add increment to the player who just moved
    if (m_timeControl != TimeControl::None && m_timerRunning) {
        auto params = getTimeControlParams(m_timeControl);
        PieceColor movedSide = opponent(m_board.sideToMove());
        uint32_t& movedTime = (movedSide == PieceColor::White)
                              ? m_timeWhiteMs : m_timeBlackMs;
        movedTime += params.incrementMs;
    }

    // Send move to remote player (only if this was a local move)
    if (m_netMode == NetworkMode::Online && !m_applyingRemoteMove) {
        sendMove(move);
    }

    // Update UI
    deselectPiece();
    addMoveToList(sanBuf);
    updateStatusBar();
    updateBoardHighlights();

    // Puzzle mode: validate move against solution
    if (m_puzzleMode && m_puzzleSolutionStep < m_puzzleSolutionLen) {
        const Move& expected = m_puzzleSolution[m_puzzleSolutionStep];
        if (move.from == expected.from && move.to == expected.to &&
            (expected.promotion == PieceType::None || move.promotion == expected.promotion)) {
            // Correct move
            m_puzzleSolutionStep++;
            m_puzzleHintLevel = 0;

            if (m_puzzleSolutionStep >= m_puzzleSolutionLen) {
                // Puzzle solved!
                PuzzleStorage::markPuzzleCompleted(m_puzzleProgress, m_puzzleIndex);
                PuzzleStorage::saveProgress(m_puzzleProgress);

                m_gameOverModal.clearButtons();
                m_gameOverModal.setTitle("Correct!");
                char msg[32];
                snprintf(msg, sizeof(msg), "Puzzle %d solved", m_puzzleIndex + 1);
                m_gameOverModal.setMessage(msg);
                m_gameOverModal.addButton("Next", [this]() {
                    m_gameOverModal.hide();
                    uint8_t next = m_puzzleIndex + 1;
                    if (next < puzzleCount()) {
                        setPuzzleMode(next);
                        focusChain().focusWidget(&m_boardGrid);
                    } else {
                        clearPuzzleMode();
                        CardGFX::scenes().pop();
                    }
                });
                m_gameOverModal.addButton("Menu", [this]() {
                    m_gameOverModal.hide();
                    clearPuzzleMode();
                    CardGFX::scenes().pop();
                });
                m_gameOverModal.show();
                focusChain().focusWidget(&m_gameOverModal);
                m_uiState = UIState::GameOver;
            } else if (m_puzzleSolutionStep < m_puzzleSolutionLen && (m_puzzleSolutionStep % 2 == 1)) {
                // Auto-play opponent response (odd steps) after delay
                m_puzzleAutoPlayPending = true;
                m_puzzleAutoPlayDelay = 500;
            }
            return; // Skip normal game-end check and save
        } else {
            // Wrong move — cancel animation first, then undo
            m_moveAnim.active = false;
            if (m_historyCount > 0) {
                m_historyCount--;
                m_board.unmakeMove(m_history[m_historyCount]);
            }
            // Restore last-move highlights from history
            if (m_historyCount > 0) {
                m_lastFrom = m_history[m_historyCount - 1].move.from;
                m_lastTo = m_history[m_historyCount - 1].move.to;
            } else {
                m_lastFrom = NO_SQUARE;
                m_lastTo = NO_SQUARE;
            }
        
            rebuildMoveList();
            m_puzzleHintLevel = 0;
            m_statusBar.setRight("Try again");
            m_uiState = UIState::SelectPiece;
            deselectPiece();
            updateBoardHighlights();
            return;
        }
    }

    // Check for game end
    checkGameEnd();

    // Save after each move (checkGameEnd clears save on game over)
    if (m_uiState != UIState::GameOver) {
        saveGameState();
    }

    // Delay board flip until animation completes (flip would break animation coords)
    if (m_netMode == NetworkMode::Local && m_aiDifficulty == AIDifficulty::None && !m_puzzleMode) {
        m_moveAnim.pendingFlip = true;
    }
}

void ChessScene::undoLastMove() {
    if (m_netMode == NetworkMode::Online) return; // Disabled in network mode
    if (m_historyCount == 0) return;
    if (m_historyOverflow) return;
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

    // Re-save state after undo
    saveGameState();
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

    // Center: timer display OR move number + variant indicator
    char moveBuf[32];
    if (m_timeControl != TimeControl::None) {
        // Show both clocks
        char wBuf[8], bBuf[8];
        formatTime(wBuf, sizeof(wBuf), m_timeWhiteMs);
        formatTime(bBuf, sizeof(bBuf), m_timeBlackMs);
        bool whiteTurn = (m_board.sideToMove() == PieceColor::White);
        snprintf(moveBuf, sizeof(moveBuf), "%s%s %s%s",
                 whiteTurn ? ">" : " ", wBuf,
                 whiteTurn ? " " : ">", bBuf);
    } else {
        const char* varTag = "";
        if (m_variant == ChessVariant::Chess960) varTag = " [960]";
        uint8_t fiftyClock = m_board.halfmoveClock() / 2;
        if (fiftyClock > 0) {
            snprintf(moveBuf, sizeof(moveBuf), "Move %d%s 50m:%d",
                     m_board.fullmoveNumber(), varTag, fiftyClock);
        } else {
            snprintf(moveBuf, sizeof(moveBuf), "Move %d%s",
                     m_board.fullmoveNumber(), varTag);
        }
    }
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
    m_promotionModal.setEscapeCallback([](){});  // Must choose a piece
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
    ChessStorage::clearSave();

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
    m_gameOverModal.addButton("Review", [this]() {
        m_gameOverModal.hide();
        enterReviewMode();
    });

    m_gameOverModal.show();
    focusChain().focusWidget(&m_gameOverModal);
}

// ── Sprite helper ─────────────────────────────────────────────────

static const uint16_t* spriteForPiece(const Piece& p) {
    uint8_t idx = static_cast<uint8_t>(p.type);
    if (idx == 0 || idx > 6) return nullptr;
    return (p.color == PieceColor::White) ? SPRITES_WHITE[idx] : SPRITES_BLACK[idx];
}

// ── Cell Renderer ─────────────────────────────────────────────────

void ChessScene::renderCell(Canvas& canvas, uint8_t col, uint8_t gridRow,
                             int16_t cx, int16_t cy, uint8_t cellW, uint8_t cellH,
                             Grid::CellState state, const Theme& theme, void* ctx) {
    ChessScene* self = static_cast<ChessScene*>(ctx);
    uint8_t boardCol = self->toBoardCol(col);
    uint8_t boardRow = self->toBoardRow(gridRow);

    // ── Animation state for this cell ────────────────────────────
    const auto& anim = self->m_moveAnim;
    bool isAnimFrom = anim.active && cx == anim.fromPx && cy == anim.fromPy;
    bool isAnimTo   = anim.active && cx == anim.toPx   && cy == anim.toPy;

    // ── Cell background ───────────────────────────────────────────
    bool lightSquare = ((boardCol + boardRow) % 2 != 0);
    uint16_t cellA = self->m_bwBoard ? HAL::rgb565(220, 220, 220) : theme.gridCellA;
    uint16_t cellB = self->m_bwBoard ? HAL::rgb565(80,  80,  80)  : theme.gridCellB;
    uint16_t cellBg = lightSquare ? cellA : cellB;

    if (state.marked)    cellBg = theme.accentMuted;
    if (state.highlight) cellBg = theme.gridHighlight;
    if (state.selected)  cellBg = theme.accentActive;

    // Capture flash
    if (anim.active && anim.isCapture && isAnimTo) {
        float t = (float)anim.elapsed / MoveAnim::DURATION_MS;
        if (t < 0.3f) {
            cellBg = theme.error;
        }
    }

    canvas.fillRect(cx, cy, cellW, cellH, cellBg);

    // ── Valid move dot (on empty highlighted squares) ─────────────
    Piece piece = self->m_board.at(boardCol, boardRow);
    if (state.highlight && piece.empty() && !state.selected) {
        // Small dot in center
        int16_t dotCx = cx + cellW / 2;
        int16_t dotCy = cy + cellH / 2;
        canvas.fillCircle(dotCx, dotCy, 2, theme.bgPrimary);
    }

    // ── Piece rendering (skip if this cell is part of slide animation) ──
    bool skipPiece = isAnimFrom || isAnimTo;
    if (!piece.empty() && !skipPiece) {
        if (self->m_useSprites) {
            const uint16_t* sprite = spriteForPiece(piece);
            if (sprite) {
                canvas.drawBitmap565(cx, cy, sprite,
                                     CHESS_SPRITE_W, CHESS_SPRITE_H,
                                     CHESS_SPRITE_TRANSPARENT);
            }
        } else {
            char ch = piece.typeChar();
            uint8_t scale = 2;
            uint16_t charW = FONT_CHAR_W * scale;
            uint16_t charH = FONT_CHAR_H * scale;
            int16_t px = cx + (cellW - charW) / 2;
            int16_t py = cy + (cellH - charH) / 2;
            if (py < cy + 1) py = cy + 1;

            uint16_t fillColor, outlineColor;
            if (piece.color == PieceColor::White) {
                fillColor = HAL::rgb565(255, 255, 255);
                outlineColor = HAL::rgb565(0, 0, 0);
            } else {
                fillColor = HAL::rgb565(30, 30, 30);
                outlineColor = HAL::rgb565(200, 200, 200);
            }

            char text[2] = {ch, '\0'};
            canvas.drawText(px - 1, py, text, outlineColor, scale);
            canvas.drawText(px + 1, py, text, outlineColor, scale);
            canvas.drawText(px, py - 1, text, outlineColor, scale);
            canvas.drawText(px, py + 1, text, outlineColor, scale);
            canvas.drawText(px, py, text, fillColor, scale);
        }
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
        uint16_t labelColor = lightSquare ? cellB : cellA;

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

    // ── Sliding piece overlay (drawn on last cell so it's on top) ─
    if (col == 7 && gridRow == 7 && anim.active) {
        float t = (float)anim.elapsed / MoveAnim::DURATION_MS;
        if (t > 1.0f) t = 1.0f;
        // Ease-out quadratic: decelerates into destination
        float eased = 1.0f - (1.0f - t) * (1.0f - t);

        int16_t ax = anim.fromPx + (int16_t)((anim.toPx - anim.fromPx) * eased);
        int16_t ay = anim.fromPy + (int16_t)((anim.toPy - anim.fromPy) * eased);

        // Draw the sliding piece at interpolated position
        if (self->m_useSprites) {
            const uint16_t* asprite = spriteForPiece(anim.piece);
            if (asprite) {
                canvas.drawBitmap565(ax, ay, asprite,
                                     CHESS_SPRITE_W, CHESS_SPRITE_H,
                                     CHESS_SPRITE_TRANSPARENT);
            }
        } else {
            Piece ap = anim.piece;
            char ach = ap.typeChar();
            uint8_t ascale = 2;
            uint16_t acharW = FONT_CHAR_W * ascale;
            uint16_t acharH = FONT_CHAR_H * ascale;
            int16_t apx = ax + (cellW - acharW) / 2;
            int16_t apy = ay + (cellH - acharH) / 2;
            if (apy < ay + 1) apy = ay + 1;

            uint16_t aFill, aOutline;
            if (ap.color == PieceColor::White) {
                aFill = HAL::rgb565(255, 255, 255);
                aOutline = HAL::rgb565(0, 0, 0);
            } else {
                aFill = HAL::rgb565(30, 30, 30);
                aOutline = HAL::rgb565(200, 200, 200);
            }

            char atxt[2] = {ach, '\0'};
            canvas.drawText(apx - 1, apy, atxt, aOutline, ascale);
            canvas.drawText(apx + 1, apy, atxt, aOutline, ascale);
            canvas.drawText(apx, apy - 1, atxt, aOutline, ascale);
            canvas.drawText(apx, apy + 1, atxt, aOutline, ascale);
            canvas.drawText(apx, apy, atxt, aFill, ascale);
        }
    }
}

// ── Helper: Rebuild Move List ─────────────────────────────────────
// Called after undo to reconstruct the display list from history

void ChessScene::rebuildMoveList() {
    m_moveList.clearItems();

    // Replay all moves to rebuild the list text
    ChessBoard tempBoard;
    tempBoard.setVariant(m_variant);
    tempBoard.setPositionIndex(m_positionIndex);
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

// ── Persistence ──────────────────────────────────────────────────────

void ChessScene::saveGameState() {
    // Don't save network games (connection can't survive power cycle)
    if (m_netMode == NetworkMode::Online) return;

    ChessStorage::saveGame(m_board, m_history, m_historyCount,
                           m_historyOverflow,
                           m_aiDifficulty, m_aiColor,
                           m_localColor, m_boardFlipped,
                           m_variant, m_positionIndex,
                           m_timeControl, m_timeWhiteMs,
                           m_timeBlackMs, m_timerRunning);
}

bool ChessScene::loadSavedGame() {
    AIDifficulty aiDiff;
    PieceColor aiCol, localCol;
    bool flipped;
    uint8_t histCount;
    bool histOverflow;
    ChessVariant variant;
    uint16_t posIndex;
    TimeControl tc;
    uint32_t twMs, tbMs;
    bool timerRun;

    if (!ChessStorage::loadGame(m_board, m_history, histCount, histOverflow,
                                aiDiff, aiCol, localCol, flipped, variant,
                                posIndex, tc, twMs, tbMs, timerRun)) {
        return false;
    }

    // Restore mode state
    m_variant = variant;
    m_positionIndex = posIndex;
    m_board.setVariant(variant);
    m_board.setPositionIndex(posIndex);
    m_timeControl = tc;
    m_timeWhiteMs = twMs;
    m_timeBlackMs = tbMs;
    m_timerRunning = timerRun;
    m_aiDifficulty = aiDiff;
    m_aiColor = aiCol;
    m_aiThinking = false;
    m_localColor = localCol;
    m_boardFlipped = flipped;
    m_netMode = NetworkMode::Local;
    m_puzzleMode = false;
    m_puzzleAutoPlayPending = false;
    m_moveAnim.active = false;
    m_moveAnim.pendingFlip = false;

    // For local pass-and-play, recalculate flip from sideToMove
    // (saveGameState runs before the pending animation flip completes)
    if (m_aiDifficulty == AIDifficulty::None) {
        m_boardFlipped = (m_board.sideToMove() == PieceColor::Black);
    }
    m_historyCount = histCount;
    m_historyOverflow = histOverflow;

    // Restore UI state
    m_uiState = UIState::SelectPiece;
    m_selectedSquare = NO_SQUARE;
    m_legalMoves.clear();

    // Restore last-move markers from final history entry
    if (m_historyCount > 0) {
        m_lastFrom = m_history[m_historyCount - 1].move.from;
        m_lastTo = m_history[m_historyCount - 1].move.to;
    } else {
        m_lastFrom = NO_SQUARE;
        m_lastTo = NO_SQUARE;
    }

    // Rebuild the move list display
    rebuildMoveList();
    updateBoardHighlights();
    updateStatusBar();

    // Update hint bar for mode
    m_hintBar.setText("[U]ndo [N]ew");

    // Position cursor on the last move destination or king
    if (!isNoSquare(m_lastTo)) {
        m_boardGrid.setCursor(toGridCol(m_lastTo.col), toGridRow(m_lastTo.row));
    } else {
        Square king = m_board.findKing(m_board.sideToMove());
        if (!isNoSquare(king)) {
            m_boardGrid.setCursor(toGridCol(king.col), toGridRow(king.row));
        }
    }

    m_boardGrid.markDirty();
    return true;
}

// ── Review Mode ──────────────────────────────────────────────────────

void ChessScene::enterReviewMode() {
    m_preReviewState = m_uiState;
    m_uiState = UIState::Reviewing;
    m_reviewIndex = m_historyCount;
    m_hintBar.setText("[</>] [ESC]");
    m_movesLabel.setText("Review");
    if (m_historyCount > 0) {
        m_moveList.setSelected((m_reviewIndex - 1) / 2);
    }
    focusChain().focusWidget(&m_boardGrid);
    updateStatusBar();
}

void ChessScene::exitReviewMode() {
    // Restore board to final position
    reviewGoTo(m_historyCount);
    m_uiState = m_preReviewState;
    m_movesLabel.setText("Moves");

    if (m_preReviewState == UIState::GameOver) {
        // Re-show game-over modal
        checkGameEnd();
    } else {
        m_hintBar.setText(m_netMode == NetworkMode::Online ? "[R]esign" : "[U]ndo [N]ew");
        focusChain().focusWidget(&m_boardGrid);
    }
    updateStatusBar();
}

void ChessScene::reviewGoTo(uint8_t index) {
    m_reviewIndex = index;

    // Replay from start to index
    ChessBoard tempBoard;
    tempBoard.setVariant(m_variant);
    tempBoard.setPositionIndex(m_positionIndex);
    tempBoard.reset();

    for (uint8_t i = 0; i < index; i++) {
        tempBoard.makeMove(m_history[i].move);
    }

    // Copy state to display board (preserve variant/960 config)
    for (uint8_t r = 0; r < 8; r++) {
        for (uint8_t c = 0; c < 8; c++) {
            m_board.set(c, r, tempBoard.at(c, r));
        }
    }
    m_board.setSideToMove(tempBoard.sideToMove());
    m_board.setCastleRights(tempBoard.castleRights());
    m_board.setEnPassantTarget(tempBoard.enPassantTarget());
    m_board.setHalfmoveClock(tempBoard.halfmoveClock());
    m_board.setFullmoveNumber(tempBoard.fullmoveNumber());

    // Update last-move markers
    if (index > 0) {
        m_lastFrom = m_history[index - 1].move.from;
        m_lastTo = m_history[index - 1].move.to;
    } else {
        m_lastFrom = NO_SQUARE;
        m_lastTo = NO_SQUARE;
    }

    updateBoardHighlights();

    // Update status bar with review position and eval
    char leftBuf[24];
    snprintf(leftBuf, sizeof(leftBuf), "Review %d/%d", index, m_historyCount);
    m_statusBar.setLeft(leftBuf);

    int16_t eval = ChessAI::evaluate(m_board);
    // Convert to white-relative
    if (m_board.sideToMove() == PieceColor::Black) eval = -eval;
    char evalBuf[12];
    if (eval > 9000) {
        snprintf(evalBuf, sizeof(evalBuf), "M%d", (10001 - eval + 1) / 2);
    } else if (eval < -9000) {
        snprintf(evalBuf, sizeof(evalBuf), "-M%d", (10001 + eval + 1) / 2);
    } else {
        snprintf(evalBuf, sizeof(evalBuf), "%s%d.%02d",
                 eval >= 0 ? "+" : "", eval / 100,
                 (eval >= 0 ? eval : -eval) % 100);
    }
    m_statusBar.setRight(evalBuf);

    // Highlight current move in list
    if (index > 0) {
        m_moveList.setSelected((index - 1) / 2);
    }

    m_boardGrid.markDirty();
}

// ── Timer Helpers ────────────────────────────────────────────────────

void ChessScene::formatTime(char* buf, uint8_t bufLen, uint32_t ms) {
    uint32_t totalSec = ms / 1000;
    if (totalSec >= 60) {
        uint32_t min = totalSec / 60;
        uint32_t sec = totalSec % 60;
        snprintf(buf, bufLen, "%d:%02d", (int)min, (int)sec);
    } else {
        uint32_t tenths = (ms % 1000) / 100;
        snprintf(buf, bufLen, "%d.%d", (int)totalSec, (int)tenths);
    }
}

// ── Variant ──────────────────────────────────────────────────────────

void ChessScene::setVariant(ChessVariant v) {
    m_variant = v;
    m_board.setVariant(v);
}

void ChessScene::setPositionIndex(uint16_t idx) {
    m_positionIndex = idx;
    m_board.setPositionIndex(idx);
}

void ChessScene::setTimeControl(TimeControl tc) {
    m_timeControl = tc;
    if (tc != TimeControl::None) {
        auto params = getTimeControlParams(tc);
        m_timeWhiteMs = params.initialMs;
        m_timeBlackMs = params.initialMs;
    } else {
        m_timeWhiteMs = 0;
        m_timeBlackMs = 0;
    }
    m_timerRunning = false;
}

// ── Puzzle Mode ──────────────────────────────────────────────────────

// External function from puzzle_data.cpp
extern void loadPuzzleIntoBoard(uint16_t index, ChessBoard& board, Move* solution,
                                uint8_t& solutionLen, PuzzleType& type, uint8_t& rating);

void ChessScene::setPuzzleMode(uint8_t puzzleIndex) {
    m_puzzleMode = true;
    m_puzzleIndex = puzzleIndex;
    m_puzzleSolutionStep = 0;
    m_puzzleHintLevel = 0;
    m_puzzleAutoPlayPending = false;

    // Load puzzle
    PuzzleType ptype;
    uint8_t prating;
    loadPuzzleIntoBoard(puzzleIndex, m_board, m_puzzleSolution,
                        m_puzzleSolutionLen, ptype, prating);

    // Load progress
    PuzzleStorage::loadProgress(m_puzzleProgress);

    // Set up UI
    m_uiState = UIState::SelectPiece;
    m_selectedSquare = NO_SQUARE;
    m_lastFrom = NO_SQUARE;
    m_lastTo = NO_SQUARE;
    m_historyCount = 0;
    m_historyOverflow = false;
    m_legalMoves.clear();
    m_moveList.clearItems();
    m_boardGrid.clearAllFlags();
    m_moveAnim.active = false;
    m_timerRunning = false;
    m_timeControl = TimeControl::None;

    // Flip board if Black to move
    m_boardFlipped = (m_board.sideToMove() == PieceColor::Black);

    // Position cursor on center
    Square king = m_board.findKing(m_board.sideToMove());
    if (!isNoSquare(king)) {
        m_boardGrid.setCursor(toGridCol(king.col), toGridRow(king.row));
    }

    // Status bar
    const char* typeStr = "Puzzle";
    if (ptype == PuzzleType::MateIn1) typeStr = "Mate in 1";
    else if (ptype == PuzzleType::MateIn2) typeStr = "Mate in 2";
    else if (ptype == PuzzleType::Tactic) typeStr = "Tactic";
    m_statusBar.setLeft(typeStr);

    char centerBuf[24];
    snprintf(centerBuf, sizeof(centerBuf), "#%d  R:%d", puzzleIndex + 1, prating);
    m_statusBar.setCenter(centerBuf);
    m_statusBar.setRight("");

    m_hintBar.setText("[H]int [S]kip");
    m_movesLabel.setText("Puzzle");
    m_boardGrid.markDirty();
}

void ChessScene::clearPuzzleMode() {
    m_puzzleMode = false;
    m_puzzleAutoPlayPending = false;
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
    m_lastSentSeq = 0;
    m_lastSendTime = 0;
    m_retryCount = 0;
    m_disconnectGraceUntil = 0;
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
        m_uiState != UIState::GameOver && now >= m_disconnectGraceUntil) {
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
        m_disconnectGraceUntil = millis() + 5000; // 5s grace before re-checking
        // Resume retransmission of pending move if one was lost
        if (m_lastSentSeq >= m_historyCount - 1 && m_historyCount > 0) {
            m_awaitingAck = true;
            m_retryCount = 0;
            m_lastSendTime = millis();
        }
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
