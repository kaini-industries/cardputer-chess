#include "chess_scene.h"
#include "chess_storage_codec.h"
#include "chess_rules.h"
#include "chess_sprites.h"
#include "chess_zobrist.h"
#include "esp_now_transport.h"
#include "profile_storage.h"
#include "puzzle_data.h"
#include "terminal_reliability.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <limits>

// ── SAN Formatting ───────────────────────────────────────────────────
// Must be called BEFORE the move is executed on the board.

static void moveToSAN(char* buf, uint8_t bufLen, const Move& move,
                      ChessBoard& board, bool isCapture) {
    if (bufLen < 12) { buf[0] = '\0'; return; }
    uint8_t i = 0;

    // Castling
    if (move.isCastle) {
        if (move.to.col == 6) {
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
    m_boardGrid.setSpaceActivates(false);
    m_boardGrid.setOnAction([this](uint8_t col, uint8_t row) {
        onCellAction(col, row);
    });
    m_boardGrid.setCursorNavigation(
        [this](uint8_t key, uint8_t currentCol, uint8_t currentRow,
               uint8_t& nextCol, uint8_t& nextRow) {
            return navigateBoardCursor(key, currentCol, currentRow, nextCol, nextRow);
        });
    addWidget(&m_boardGrid, true); // focusable

    // ── Moves Label ───────────────────────────────────────────────
    updateMovesLabel();
    m_movesLabel.setBounds({120, 12, 120, 10});
    m_movesLabel.setAlign(Label::Align::Center);
    m_movesLabel.setDrawBg(true);
    addWidget(&m_movesLabel);

    // ── Move History List ─────────────────────────────────────────
    m_moveList.setBounds({120, 22, 120, 101});
    m_moveList.setAutoScroll(true);
    addWidget(&m_moveList); // not focusable -- display only

    // ── Hint Bar ──────────────────────────────────────────────────
    m_hintBar.setText("[U]ndo [Esc]Menu");
    m_hintBar.setBounds({120, 123, 120, 12});
    m_hintBar.setAlign(Label::Align::Center);
    m_hintBar.setDrawBg(true);
    addWidget(&m_hintBar);

    // ── Promotion Modal ───────────────────────────────────────────
    m_promotionModal.setBounds({0, 0, SCREEN_W, SCREEN_H});
    m_promotionModal.setTitle("Promote to:");
    m_promotionModal.setSpaceActivates(false);
    m_promotionModal.setVisible(false);
    addWidget(&m_promotionModal, true);

    // ── Game Over Modal ───────────────────────────────────────────
    m_gameOverModal.setBounds({0, 0, SCREEN_W, SCREEN_H});
    m_gameOverModal.setSpaceActivates(false);
    m_gameOverModal.setVisible(false);
    addWidget(&m_gameOverModal, true);

    // ── Exit Confirmation Modal ───────────────────────────────────
    // Added last so it draws over every other game widget.
    m_exitModal.setBounds({0, 0, SCREEN_W, SCREEN_H});
    m_exitModal.setSpaceActivates(false);
    m_exitModal.setVisible(false);
    addWidget(&m_exitModal, true);

    m_actionModal.setBounds({0, 0, SCREEN_W, SCREEN_H});
    m_actionModal.setSpaceActivates(false);
    m_actionModal.setVisible(false);
    addWidget(&m_actionModal, true);

    // Start a new game
    newGame();
}

void ChessScene::onEnter() {
    m_leavingToMenu = false;
    if (m_clock.enabled() && !m_clock.hasStarted() &&
        m_uiState != UIState::GameOver) {
        m_clock.start(millis());
        saveGameState();
    }
    if (m_actionModal.isVisible()) {
        focusChain().focusWidget(&m_actionModal);
    } else if (m_exitModal.isVisible()) {
        focusChain().focusWidget(&m_exitModal);
    } else if (m_promotionModal.isVisible()) {
        focusChain().focusWidget(&m_promotionModal);
    } else if (m_gameOverModal.isVisible()) {
        focusChain().focusWidget(&m_gameOverModal);
    } else {
        focusChain().focusWidget(&m_boardGrid);
    }
    updateStatusBar();
}

void ChessScene::onTick(uint32_t dt_ms) {
    const bool reviewingFinishedGame =
        m_uiState == UIState::Reviewing &&
        m_preReviewState == UIState::GameOver;
    const bool presentingFinishedGame =
        m_uiState == UIState::GameOver || reviewingFinishedGame;

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
        if (m_exitModal.isVisible()) {
            m_exitModal.markDirty();
        }
        if (m_actionModal.isVisible()) {
            m_actionModal.markDirty();
        }
    }

    // ── Timer countdown ──────────────────────────────────────
    // Apply already-received moves, clock gifts, and terminal events before
    // evaluating a local flag for this frame.
    if (m_netMode == NetworkMode::Online) {
        pollNetwork();
    }

    if (m_clock.enabled() && m_clock.hasStarted() &&
        m_uiState != UIState::GameOver) {
        const uint32_t now = millis();
        m_clock.update(now);
        if (m_clock.hasFlaggedSide()) {
            const PieceColor flagged = m_clock.flaggedSide();
            // Each online player is authoritative for their own flag. A zero
            // projection for the opponent is corrected by a heartbeat or a
            // terminal control event rather than declared locally.
            if (m_netMode != NetworkMode::Online || flagged == m_localColor) {
                if (m_netMode == NetworkMode::Online && m_timeGiftPending) {
                    // A time gift is committed by the receiver before its ACK.
                    // Keep the local flag provisional until that transaction is
                    // resolved so the terminal clock snapshot cannot omit a
                    // gift that the opponent has already applied.
                    if (!m_deferredLocalTimeout) {
                        m_deferredLocalTimeout = true;
                        m_deferredFlaggedSide = flagged;
                        m_statusBar.setRight("Confirming +15 sec");
                    }
                } else {
                    finishTimeout(flagged);
                }
            }
        }
    }

    if (presentingFinishedGame && !m_resultRecorded &&
        !m_puzzleMode && m_resultOutcome != GameOutcome::None &&
        (millis() - m_lastResultSaveAttempt) >= 1000) {
        recordCompletedGame();
    }

    if (m_gameSaveDirty && m_netMode == NetworkMode::Local &&
        !presentingFinishedGame && m_resultOutcome == GameOutcome::None &&
        (millis() - m_lastGameSaveAttempt) >= 1000) {
        saveGameState();
    }

    // A running local or AI clock is not marked dirty by think time. Snapshot
    // it at most once every five seconds so a power loss cannot refund the
    // time spent since the last move. Online games stay unsaved.
    if (!m_puzzleMode && m_netMode == NetworkMode::Local &&
        m_clock.enabled() && m_clock.isRunning() &&
        !presentingFinishedGame && m_resultOutcome == GameOutcome::None &&
        (millis() - m_lastGameSaveAttempt) >= 5000) {
        saveGameState();
    }

    // ── Puzzle auto-play opponent response ──────────────────
    if (m_puzzleMode && m_puzzleAutoPlayPending && !m_moveAnim.active) {
        m_puzzleAutoPlayDelay -= (int32_t)dt_ms;
        if (m_puzzleAutoPlayDelay <= 0) {
            m_puzzleAutoPlayPending = false;
            if (m_uiState != UIState::GameOver &&
                m_puzzleSolutionStep < m_puzzleSolutionLen &&
                (m_puzzleSolutionStep & 1)) {
                Move responseMove = m_puzzleSolution[m_puzzleSolutionStep];
                // Find the full legal move with flags
                MoveList legal;
                ChessRules::generateLegal(m_board, legal);
                bool matched = false;
                for (uint8_t i = 0; i < legal.count; i++) {
                    if (legal.moves[i].from == responseMove.from &&
                        legal.moves[i].to == responseMove.to &&
                        legal.moves[i].promotion == responseMove.promotion) {
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
            // Phase 2: run the search. A zero cap means the difficulty budget,
            // so a flagged clock passes 1 ms instead of lifting the cap.
            uint32_t aiTimeCapMs = 0;
            if (m_clock.enabled()) {
                aiTimeCapMs = m_clock.remainingMsAt(m_board.sideToMove(), millis());
                if (aiTimeCapMs == 0) aiTimeCapMs = 1;
            }
            Move aiMove = ChessAI::findBestMove(m_board, m_aiDifficulty, aiTimeCapMs);
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
    if (m_uiState != UIState::Reviewing &&
        !m_promotionModal.isVisible() && !m_gameOverModal.isVisible() &&
        !m_exitModal.isVisible()) {
        updateStatusBar();
    }
}

bool ChessScene::onInput(const InputEvent& event) {
    if (!event.isDown()) return false;

    // Visible modals own input through the focus chain. Keeping this guard also
    // prevents a stale board focus from acting behind an overlay.
    if (m_actionModal.isVisible() || m_exitModal.isVisible() ||
        m_promotionModal.isVisible() || m_gameOverModal.isVisible()) {
        return false;
    }

    // Review mode navigation
    if (m_uiState == UIState::Reviewing) {
        if (event.key == Key::ESCAPE || event.key == Key::BACKSPACE ||
            event.key == Key::DEL) {
            exitReviewMode();
            return true;
        }
        if (event.key == '<') {
            if (m_reviewIndex > 0) reviewGoTo(m_reviewIndex - 1);
            return true;
        }
        if (event.key == '>') {
            if (m_reviewIndex < m_historyCount) {
                reviewGoTo(m_reviewIndex + 1);
            }
            return true;
        }
        if (event.key == 'n' || event.key == 'N') {
            if (m_preReviewState == UIState::GameOver) {
                if (m_netMode == NetworkMode::Online) {
                    leaveOnlineGame();
                } else {
                    leaveToMenu();
                }
            } else if (m_netMode == NetworkMode::Local) {
                requestExitToMenu();
            }
            return true;
        }
        // Directional review input is handled by the board Grid's navigation
        // callback before it can move the cursor.
        return true; // Block all other input in review mode
    }

    // Local and AI games expose an always-reachable leave-game path. Handle it
    // before the animation guard so a key press during the 250 ms animation is
    // not discarded. N bypasses selection cancellation; Escape cancels a
    // selected piece first and prompts from the idle board.
    if (!m_puzzleMode && m_netMode == NetworkMode::Local) {
        if (event.key == 'n' || event.key == 'N') {
            requestExitToMenu();
            return true;
        }
        if (event.key == Key::ESCAPE || event.key == Key::BACKSPACE ||
            event.key == Key::DEL) {
            if (m_uiState == UIState::ShowMoves) {
                if (!isNoSquare(m_selectedSquare)) {
                    m_boardGrid.setCursor(toGridCol(m_selectedSquare.col),
                                          toGridRow(m_selectedSquare.row));
                }
                deselectPiece();
            } else if (m_uiState == UIState::SelectPiece &&
                       event.key == Key::ESCAPE) {
                requestExitToMenu();
            }
            return true;
        }
    }

    // Block remaining game actions during move animation.
    if (m_moveAnim.active) return true;

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
        if (event.key == Key::ESCAPE || event.key == Key::BACKSPACE ||
            event.key == Key::DEL) {
            if (m_uiState == UIState::ShowMoves) {
                deselectPiece();
                return true;
            }
            if (event.key == Key::ESCAPE) {
                clearPuzzleMode();
                CardGFX::scenes().pop();
            }
            return true;
        }
        // Block undo/new in puzzle mode — fall through to cell action only
    }

    switch (event.key) {
    case 'b':
    case 'B':
        m_useSprites = !m_useSprites;
        m_boardGrid.markDirty();
        return true;

    case 't':
    case 'T':
        m_bwBoard = !m_bwBoard;
        m_boardGrid.markDirty();
        return true;

    case 'h':
    case 'H':
    case 'i':
    case 'I':
        showHelpModal();
        return true;

    case Key::SPACE:
        if (m_uiState == UIState::ShowMoves) {
            cycleLegalMove();
        }
        return true;

    case 'f':
    case 'F':
        if (!m_puzzleMode) {
            uint8_t boardC = toBoardCol(m_boardGrid.cursorCol());
            uint8_t boardR = toBoardRow(m_boardGrid.cursorRow());
            m_boardFlipped = !m_boardFlipped;
            m_boardGrid.setCursor(toGridCol(boardC), toGridRow(boardR));
            updateBoardHighlights();
            m_boardGrid.markDirty();
        }
        return true;

    case 'v':
    case 'V':
        // A live online review would replace m_board with a historical
        // position while heartbeat and control validation still depend on the
        // current network position. Online review remains available once the
        // game is over.
        if (m_historyCount > 0 && !m_aiThinking && !m_puzzleMode &&
            m_uiState == UIState::SelectPiece &&
            m_netMode != NetworkMode::Online &&
            m_timeControl == TimeControl::None) {
            enterReviewMode();
            return true;
        }
        break;

    case 'u':
    case 'U':
        if (m_puzzleMode) break;
        // Timed undo would otherwise retain an already-awarded increment and
        // could be repeated to manufacture clock time. Saved games also do
        // not retain a per-ply clock ledger, so keep undo strictly untimed.
        if (m_netMode != NetworkMode::Online &&
            m_timeControl == TimeControl::None) {
            undoLastMove();
            return true;
        }
        break;

    case 'r':
    case 'R':
        if (m_netMode == NetworkMode::Online && m_uiState != UIState::GameOver) {
            confirmResign();
            return true;
        }
        break;

    case 'd':
    case 'D':
        if (m_netMode == NetworkMode::Online &&
            m_uiState != UIState::GameOver) {
            offerDraw();
            return true;
        }
        break;

    case 'g':
    case 'G':
        if (m_netMode == NetworkMode::Online && m_clock.enabled() &&
            m_uiState != UIState::GameOver) {
            giveOpponentTime();
            return true;
        }
        break;

    case Key::ESCAPE:
    case Key::BACKSPACE:
    case Key::DEL:
        if (m_uiState == UIState::ShowMoves) {
            if (!isNoSquare(m_selectedSquare)) {
                m_boardGrid.setCursor(toGridCol(m_selectedSquare.col),
                                      toGridRow(m_selectedSquare.row));
            }
            deselectPiece();
            return true;
        }
        break;

    }

    return false;
}

// ── Game Logic ────────────────────────────────────────────────────

bool ChessScene::navigateBoardCursor(uint8_t key, uint8_t currentCol,
                                     uint8_t currentRow, uint8_t& nextCol,
                                     uint8_t& nextRow) {
    CursorDirection direction;
    switch (key) {
    case Key::UP:    direction = CursorDirection::Up; break;
    case Key::DOWN:  direction = CursorDirection::Down; break;
    case Key::LEFT:  direction = CursorDirection::Left; break;
    case Key::RIGHT: direction = CursorDirection::Right; break;
    default: return false;
    }

    // Review mode owns directional input. Consuming it here prevents the
    // focused Grid from moving its cursor before ChessScene can step history.
    if (m_uiState == UIState::Reviewing) {
        if (direction == CursorDirection::Left ||
            direction == CursorDirection::Up) {
            if (m_reviewIndex > 0) reviewGoTo(m_reviewIndex - 1);
        } else if (m_reviewIndex < m_historyCount) {
            reviewGoTo(m_reviewIndex + 1);
        }
        nextCol = currentCol;
        nextRow = currentRow;
        return true;
    }

    if (m_uiState != UIState::ShowMoves) return false;

    Square destinations[64];
    bool seen[8][8] = {};
    uint8_t destinationCount = 0;
    for (uint16_t i = 0; i < m_legalMoves.count; i++) {
        const Square boardDestination = m_legalMoves.moves[i].to;
        if (!boardDestination.valid()) continue;

        const uint8_t gridCol = toGridCol(boardDestination.col);
        const uint8_t gridRow = toGridRow(boardDestination.row);
        if (seen[gridRow][gridCol]) continue;

        seen[gridRow][gridCol] = true;
        destinations[destinationCount++] = makeSquare(gridCol, gridRow);
    }

    Square next = makeSquare(currentCol, currentRow);
    if (chooseCursorDestination(destinations, destinationCount,
                                makeSquare(currentCol, currentRow),
                                direction, next)) {
        nextCol = next.col;
        nextRow = next.row;
    }

    // Never fall back to one-cell movement while a piece is selected, even if
    // malformed state somehow produces no candidate destinations.
    return true;
}

void ChessScene::cycleLegalMove() {
    if (m_uiState != UIState::ShowMoves || m_legalMoves.count == 0) return;

    Square destinations[64];
    bool seen[8][8] = {};
    uint8_t destinationCount = 0;
    for (uint16_t i = 0; i < m_legalMoves.count; ++i) {
        const Square boardDestination = m_legalMoves.moves[i].to;
        if (!boardDestination.valid()) continue;
        const uint8_t gridCol = toGridCol(boardDestination.col);
        const uint8_t gridRow = toGridRow(boardDestination.row);
        if (seen[gridRow][gridCol]) continue;
        seen[gridRow][gridCol] = true;
        destinations[destinationCount++] = makeSquare(gridCol, gridRow);
    }

    Square next;
    if (cycleCursorDestination(destinations, destinationCount,
                               makeSquare(m_boardGrid.cursorCol(),
                                          m_boardGrid.cursorRow()),
                               next)) {
        m_boardGrid.setCursor(next.col, next.row);
        updateStatusBar();
    }
}

void ChessScene::newGame() {
    // Clear puzzle state (in case we're coming from puzzle mode)
    m_puzzleMode = false;
    m_puzzleAutoPlayPending = false;
    m_puzzleHintLevel = 0;

    // Cancel any in-progress animation
    m_moveAnim.active = false;
    m_moveAnim.pendingFlip = false;

    m_clock.configure(m_timeControl);
    m_clockPausedForReview = false;
    m_clockPausedForExit = false;
    m_resultOutcome = GameOutcome::None;
    m_termination = TerminationReason::None;
    m_resultRecorded = false;
    m_terminalSummaryReady = false;
    m_terminalJournaled = false;
    m_terminalSummary = GameSummary{};
    m_lastResultSaveAttempt = 0;
    m_terminalDrainUntil = 0;
    m_gameSaveDirty = false;
    m_lastGameSaveAttempt = 0;
    m_positionEpoch = 0;
    m_missingRemoteMove = false;
    m_missingRemoteEpoch = 0;
    m_missingRemoteMoveSince = 0;
    m_timeGiftPending = false;
    m_deferredLocalTimeout = false;

    m_board.setVariant(m_variant);
    m_board.setPositionIndex(m_positionIndex);
    m_board.reset();
    m_reviewBoard = m_board;
    m_uiState = UIState::SelectPiece;
    m_selectedSquare = NO_SQUARE;
    m_lastFrom = NO_SQUARE;
    m_lastTo = NO_SQUARE;

    m_historyCount = 0;
    m_historyOverflow = false;
    m_legalMoves.clear();

    m_moveList.clearItems();
    updateMovesLabel();
    m_boardGrid.clearAllFlags();
    // Start cursor on the human's king
    uint8_t startRow = 0;
    if (m_aiDifficulty != AIDifficulty::None && m_aiColor == PieceColor::White) {
        startRow = 7;
    }
    m_boardGrid.setCursor(toGridCol(m_board.initKingCol()), toGridRow(startRow));
    updateStatusBar();
    m_boardGrid.markDirty();

    updateHintBar();
}

void ChessScene::onCellAction(uint8_t gridCol, uint8_t gridRow) {
    // Grid handles Enter before Scene::onInput(), so guard actions here too.
    if (m_moveAnim.active || m_actionModal.isVisible() ||
        m_exitModal.isVisible() || m_gameOverModal.isVisible() ||
        m_uiState == UIState::GameOver || m_uiState == UIState::PromotionPending ||
        (m_puzzleMode && (m_puzzleAutoPlayPending || (m_puzzleSolutionStep & 1)))) {
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
    if (m_puzzleMode && (m_puzzleAutoPlayPending || (m_puzzleSolutionStep & 1))) return;
    if (localMoveBlockedByControl()) {
        m_statusBar.setRight("Action confirming");
        return;
    }

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
    updateHintBar();
}

void ChessScene::deselectPiece() {
    m_selectedSquare = NO_SQUARE;
    m_legalMoves.clear();
    m_uiState = UIState::SelectPiece;
    updateBoardHighlights();
    updateHintBar();
}

void ChessScene::tryMove(uint8_t col, uint8_t boardRow) {
    // Find the matching legal move(s)
    // There might be multiple if this is a promotion (4 variants)
    Move baseMove;
    bool isPromotion = false;
    bool found = false;
    Move castleMove;
    bool hasCastle = false, hasKingMove = false;

    for (uint8_t i = 0; i < m_legalMoves.count; i++) {
        if (m_legalMoves.moves[i].to.col == col &&
            m_legalMoves.moves[i].to.row == boardRow) {
            const Move& candidate = m_legalMoves.moves[i];
            if (!found || !candidate.isCastle) baseMove = candidate;
            if (candidate.isCastle) {
                castleMove = candidate;
                hasCastle = true;
            } else if (m_board.at(candidate.from.col, candidate.from.row).type == PieceType::King) {
                hasKingMove = true;
            }
            isPromotion = candidate.promotion != PieceType::None;
            found = true;
        }
    }

    if (!found) return;

    if (hasCastle && hasKingMove) {
        // Chess960 can give an ordinary king move and castling identical
        // coordinates. Preserve access to both with an explicit choice.
        m_actionModal.clearButtons();
        m_actionModal.setTitle("Choose move");
        m_actionModal.setMessage("Move king or castle?");
        m_actionModal.setEscapeCallback([this]() { closeActionModal(); });
        m_actionModal.addButton("King", [this, baseMove]() {
            closeActionModal();
            executeMove(baseMove);
        });
        m_actionModal.addButton("Castle", [this, castleMove]() {
            closeActionModal();
            executeMove(castleMove);
        });
        m_actionModal.addButton("Cancel", [this]() { closeActionModal(); });
        m_actionModal.show();
        focusChain().focusWidget(&m_actionModal);
    } else if (isPromotion) {
        showPromotionModal(baseMove);
    } else {
        executeMove(baseMove);
    }
}

void ChessScene::executeMove(const Move& move) {
    if (m_netMode == NetworkMode::Online && !m_applyingRemoteMove &&
        localMoveBlockedByControl()) {
        m_statusBar.setRight("Action confirming");
        return;
    }

    const PieceColor mover = m_board.sideToMove();
    const uint32_t moveTimestamp = millis();
    const uint32_t preBoardHash = ChessZobrist::hash(m_board);

    if (m_clock.enabled() && !m_remoteClockPending) {
        const ChessClockMoveResult clockResult =
            m_clock.commitMove(mover, moveTimestamp);
        if (clockResult != ChessClockMoveResult::Applied) {
            if (clockResult == ChessClockMoveResult::Flagged) {
                const PieceColor flagged = m_clock.flaggedSide();
                if (m_netMode == NetworkMode::Online &&
                    flagged != m_localColor) {
                    onConnectionLost("Clock out of sync");
                    return;
                }
                finishTimeout(flagged);
            }
            return;
        }
    }

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
    if (m_historyCount == MAX_HISTORY) {
        std::memmove(m_history, m_history + 1, (MAX_HISTORY - 1) * sizeof(MoveRecord));
        --m_historyCount;
        m_historyOverflow = true; // Full-game undo/review is no longer available.
    }
    m_history[m_historyCount++] = m_board.makeMove(move);
    ++m_positionEpoch;
    const uint32_t postBoardHash = ChessZobrist::hash(m_board);

    if (m_clock.enabled() && m_remoteClockPending) {
        ChessClockSnapshot remoteClock;
        remoteClock.timeControl = m_timeControl;
        remoteClock.whiteRemainingMs =
            m_clock.remainingMsAt(PieceColor::White, moveTimestamp);
        remoteClock.blackRemainingMs =
            m_clock.remainingMsAt(PieceColor::Black, moveTimestamp);
        if (mover == PieceColor::White) {
            remoteClock.whiteRemainingMs = m_remoteMoverRemainingMs;
        } else {
            remoteClock.blackRemainingMs = m_remoteMoverRemainingMs;
        }
        remoteClock.activeSide = m_board.sideToMove();
        remoteClock.started = true;
        remoteClock.paused = false;
        remoteClock.hasFlaggedSide = false;
        m_clock.loadSnapshot(remoteClock, moveTimestamp);
        m_remoteClockPending = false;
    }

    // Track last move for highlighting
    m_lastFrom = move.from;
    m_lastTo = move.to;

    // Send move to remote player (only if this was a local move)
    if (m_netMode == NetworkMode::Online && !m_applyingRemoteMove) {
        sendMove(move, preBoardHash, postBoardHash);
    }

    // Update UI
    deselectPiece();
    if (m_historyOverflow) rebuildMoveList();
    else addMoveToList(sanBuf);
    updateStatusBar();
    updateBoardHighlights();

    // Puzzle mode: validate the move against the stored line and, for mate
    // puzzles, the actual board outcome. executeMove() is only reached for a
    // legal move, so outcome-based acceptance also supports equivalent mating
    // moves and underpromotions.
    if (m_puzzleMode && m_puzzleSolutionStep < m_puzzleSolutionLen) {
        const Move& expected = m_puzzleSolution[m_puzzleSolutionStep];
        const bool matchesStoredMove =
            move.from == expected.from && move.to == expected.to &&
            (expected.promotion == PieceType::None || move.promotion == expected.promotion);
        const bool completesSequence =
            (uint8_t)(m_puzzleSolutionStep + 1) >= m_puzzleSolutionLen;
        const bool isPlayerStep = (m_puzzleSolutionStep % 2) == 0;

        bool correctMove = matchesStoredMove;
        if (m_puzzleType == PuzzleType::MateIn1) {
            // A mate-in-one is solved by any legal mating move, regardless of
            // whether its coordinates or promotion match the canonical hint.
            correctMove = ChessRules::isCheckmate(m_board);
        } else if (m_puzzleType == PuzzleType::MateIn2 && completesSequence) {
            // Keep the canonical first move and auto-played response, but the
            // player's final move must actually mate and may be any mating move.
            // Requiring a player step also prevents malformed even-length lines
            // from being completed by an auto-played coordinate match alone.
            correctMove = isPlayerStep && ChessRules::isCheckmate(m_board);
        }

        if (correctMove) {
            // Correct move
            m_puzzleSolutionStep++;
            m_puzzleHintLevel = 0;
            m_puzzleFeedbackUntil = 0;
            m_puzzleAutoPlayPending = false;

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
            m_puzzleFeedbackUntil = millis() + 1500;
            m_statusBar.setRight("Try again");
            m_uiState = UIState::SelectPiece;
            deselectPiece();
            updateBoardHighlights();
            return;
        }
    }

    // Check for game end
    checkGameEnd(!m_applyingRemoteMove, moveTimestamp);

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
    if (m_timeControl != TimeControl::None) return;
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
    if (m_puzzleMode) {
        const char* type = m_puzzleType == PuzzleType::MateIn1 ? "Mate in 1"
            : m_puzzleType == PuzzleType::MateIn2 ? "Mate in 2" : "Tactic";
        m_statusBar.setLeft(type);
        char label[24];
        snprintf(label, sizeof(label), "#%d  R:%d", m_puzzleIndex + 1, m_puzzleRating);
        m_statusBar.setCenter(label);
        if (m_uiState != UIState::GameOver) {
            if (m_puzzleFeedbackUntil != 0 &&
                static_cast<int32_t>(m_puzzleFeedbackUntil - millis()) > 0)
                m_statusBar.setRight("Try again");
            else m_statusBar.setRight(m_puzzleAutoPlayPending ? "Replying..." : "Your move");
        }
        return;
    }
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
        const uint32_t now = millis();
        formatTime(wBuf, sizeof(wBuf),
                   m_clock.remainingMsAt(PieceColor::White, now));
        formatTime(bBuf, sizeof(bBuf),
                   m_clock.remainingMsAt(PieceColor::Black, now));
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

void ChessScene::updateHintBar() {
    if (m_uiState == UIState::Reviewing) {
        m_hintBar.setText("Arrows:ply Esc:back");
    } else if (m_uiState == UIState::ShowMoves) {
        m_hintBar.setText("Spc:Next Esc/Del:X");
    } else if (m_puzzleMode) {
        m_hintBar.setText("H:Hint S:Skip I:Help");
    } else if (m_netMode == NetworkMode::Online) {
        m_hintBar.setText(m_clock.enabled()
                              ? "D:Draw G:+15 R:Res"
                              : "D:Draw R:Res H:Help");
    } else {
        m_hintBar.setText(m_timeControl == TimeControl::None
                              ? "U:Undo V:View H:Help"
                              : "H:Help Esc:Menu");
    }
}

void ChessScene::updateMovesLabel() {
    if (m_netMode == NetworkMode::Online) {
        char label[32];
        const char* name = m_opponentName[0] ? m_opponentName : "Player";
        std::snprintf(label, sizeof(label), "vs %.8s %02X:%02X", name,
                      m_opponentMac[4], m_opponentMac[5]);
        m_movesLabel.setText(label);
    } else {
        m_movesLabel.setText("Moves");
    }
}

void ChessScene::closeActionModal() {
    m_actionModal.hide();
    restoreGameInputFocus();
    updateHintBar();
    updateStatusBar();
}

void ChessScene::showHelpModal() {
    if (m_actionModal.isVisible() || m_uiState == UIState::GameOver ||
        m_uiState == UIState::PromotionPending ||
        m_uiState == UIState::ExitConfirm) {
        return;
    }

    m_actionModal.clearButtons();
    m_actionModal.setTitle("Controls");
    if (m_puzzleMode) {
        m_actionModal.setMessage(
            "Enter move\nSpace next legal\nEsc/Del cancel\nH hint  S skip\nB pieces  T theme");
    } else if (m_netMode == NetworkMode::Online) {
        m_actionModal.setMessage(m_clock.enabled()
            ? "Enter move\nSpace next legal\nEsc/Del cancel\nD offer draw\nR resign  G give +15"
            : "Enter move\nSpace next legal\nEsc/Del cancel\nD offer draw\nR resign  B pieces");
    } else {
        m_actionModal.setMessage(m_timeControl == TimeControl::None
            ? "Enter move\nSpace next legal\nEsc/Del cancel\nB pieces  T theme\nU undo  V review"
            : "Enter move\nSpace next legal\nEsc/Del cancel\nB pieces  T theme\nEsc menu");
    }
    m_actionModal.setEscapeCallback([this]() { closeActionModal(); });
    m_actionModal.addButton("Close", [this]() { closeActionModal(); });
    m_actionModal.show();
    focusChain().focusWidget(&m_actionModal);
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

void ChessScene::checkGameEnd(bool notifyPeer,
                              uint32_t terminalTimestamp) {
    if (ChessRules::isCheckmate(m_board)) {
        const bool whiteLost = m_board.sideToMove() == PieceColor::White;
        const GameOutcome outcome = whiteLost
            ? GameOutcome::BlackWin : GameOutcome::WhiteWin;
        const char* winner = whiteLost ? "Black wins!" : "White wins!";
        m_statusBar.setRight("Mate!");
        finishGame(outcome, TerminationReason::Checkmate,
                   "Checkmate!", winner, notifyPeer,
                   terminalTimestamp);
    } else if (ChessRules::isStalemate(m_board)) {
        m_statusBar.setRight("Draw");
        finishGame(GameOutcome::Draw, TerminationReason::Stalemate,
                   "Stalemate!", "Game is a draw.", notifyPeer,
                   terminalTimestamp);
    } else if (ChessRules::isDraw50Move(m_board)) {
        m_statusBar.setRight("Draw");
        finishGame(GameOutcome::Draw, TerminationReason::FiftyMove,
                   "50-Move Rule", "Game is a draw.", notifyPeer,
                   terminalTimestamp);
    } else if (ChessRules::isInsufficientMaterial(m_board)) {
        m_statusBar.setRight("Draw");
        finishGame(GameOutcome::Draw,
                   TerminationReason::InsufficientMaterial,
                   "Insufficient", "Material for mate.", notifyPeer,
                   terminalTimestamp);
    } else if (ChessRules::isThreefoldRepetition(
                   m_board, m_history, m_historyCount)) {
        m_statusBar.setRight("Draw");
        finishGame(GameOutcome::Draw, TerminationReason::Repetition,
                   "Threefold Rep.", "Game is a draw.", notifyPeer,
                   terminalTimestamp);
    }
}

void ChessScene::finishGame(GameOutcome outcome,
                            TerminationReason termination,
                            const char* title, const char* message,
                            bool notifyPeer,
                            uint32_t terminalTimestamp) {
    if (m_uiState == UIState::GameOver || m_resultOutcome != GameOutcome::None) {
        return;
    }

    const uint32_t now = terminalTimestamp != 0
        ? terminalTimestamp : millis();
    if (m_clock.isRunning()) m_clock.pause(now);
    m_finishDrawOnAck = false;
    m_clockPausedForDrawAccept = false;
    m_localDrawOfferEventId = 0;
    m_localDrawOfferBoardHash = 0;
    m_remoteDrawOfferEventId = 0;
    m_resultOutcome = outcome;
    m_termination = termination;
    captureTerminalSummary();
    // Persist an immutable result journal before any cleanup or scene exit.
    // recordCompletedGame() is idempotent and retries safely from onTick.
    recordCompletedGame();
    if (m_netMode == NetworkMode::Online) {
        // Give the final move/control ACK path a bounded chance to drain
        // before an immediate Lobby/Escape tears down ESP-NOW.
        // A draw accept is itself the terminal packet. Keep the receiver
        // available through the sender's normal five-second retry window so
        // a lost first ACK cannot leave the two boards disagreeing.
        m_terminalDrainUntil = now +
            (termination == TerminationReason::Agreement ? 6000 : 1500);
    }
    if (notifyPeer && m_netMode == NetworkMode::Online) {
        sendGameEnd(outcome, termination);
    }
    showGameOverModal(title, message);
}

void ChessScene::captureTerminalSummary() {
    if (m_terminalSummaryReady || m_resultOutcome == GameOutcome::None ||
        m_resultOutcome == GameOutcome::Incomplete) {
        return;
    }

    GameSummary& summary = m_terminalSummary;
    summary = GameSummary{};
    summary.gameId = m_participants.gameId;
    if (summary.gameId == 0) {
        summary.gameId = ChessZobrist::hash(m_board) ^ millis();
        if (summary.gameId == 0) summary.gameId = 1;
        m_participants.gameId = summary.gameId;
    }
    summary.whiteProfileId = m_participants.whiteProfileId;
    summary.blackProfileId = m_participants.blackProfileId;
    std::strncpy(summary.whiteName, m_participants.whiteName,
                 PLAYER_NAME_MAX);
    summary.whiteName[PLAYER_NAME_MAX] = '\0';
    std::strncpy(summary.blackName, m_participants.blackName,
                 PLAYER_NAME_MAX);
    summary.blackName[PLAYER_NAME_MAX] = '\0';
    if (summary.whiteName[0] == '\0') std::strcpy(summary.whiteName, "White");
    if (summary.blackName[0] == '\0') std::strcpy(summary.blackName, "Black");
    summary.mode = m_netMode == NetworkMode::Online
        ? GameMode::Online
        : (m_aiDifficulty == AIDifficulty::None
               ? GameMode::Local : GameMode::AI);
    summary.variant = m_variant;
    summary.positionIndex = m_positionIndex;
    summary.timeControl = m_timeControl;
    summary.aiDifficulty = summary.mode == GameMode::AI
        ? static_cast<uint8_t>(m_aiDifficulty) : 0;
    summary.localColor = m_localColor;
    summary.outcome = m_resultOutcome;
    summary.termination = m_termination;
    const uint32_t fullmove = m_board.fullmoveNumber();
    const uint32_t boardPly = fullmove == 0
        ? m_historyCount
        : (fullmove - 1u) * 2u +
          (m_board.sideToMove() == PieceColor::Black ? 1u : 0u);
    summary.plyCount = boardPly > std::numeric_limits<uint16_t>::max()
        ? std::numeric_limits<uint16_t>::max()
        : static_cast<uint16_t>(boardPly);
    const ChessClockSnapshot clock = m_clock.snapshot(millis());
    summary.whiteRemainingMs = clock.whiteRemainingMs;
    summary.blackRemainingMs = clock.blackRemainingMs;
    m_terminalSummaryReady = true;
}

bool ChessScene::recordCompletedGame() {
    if (m_resultRecorded || m_puzzleMode ||
        m_resultOutcome == GameOutcome::None ||
        m_resultOutcome == GameOutcome::Incomplete) {
        return m_resultRecorded;
    }

    m_lastResultSaveAttempt = millis();
    captureTerminalSummary();
    if (!m_terminalSummaryReady) return false;

    const GameSummary& summary = m_terminalSummary;
    const ProfileStorage::PendingSaveStatus journalStatus =
        ProfileStorage::savePendingResult(summary);
    if (journalStatus != ProfileStorage::PendingSaveStatus::Saved &&
        journalStatus != ProfileStorage::PendingSaveStatus::AlreadyPresent) {
        m_statusBar.setRight(
            journalStatus == ProfileStorage::PendingSaveStatus::Conflict
                ? "Pending result conflict" : "Result journal failed");
        return false;
    }
    m_terminalJournaled = true;

    ProfileData profiles;
    const ProfileStorage::LoadStatus profileStatus =
        ProfileStorage::loadOrCreate(profiles);
    if (profileStatus != ProfileStorage::LoadStatus::Loaded &&
        profileStatus != ProfileStorage::LoadStatus::Created) {
        m_statusBar.setRight("Profiles unavailable");
        return false;
    }

    const ProfileStorage::SummaryMatch match =
        ProfileStorage::findSummary(profiles, summary);
    if (match == ProfileStorage::SummaryMatch::Conflict) {
        m_statusBar.setRight("History ID conflict");
        return false;
    }
    if (match == ProfileStorage::SummaryMatch::Missing) {
        if (!GameRecords::recordCompletedGame(profiles, summary) ||
            !ProfileStorage::save(profiles)) {
            m_statusBar.setRight("History save failed");
            return false;
        }
    }

    // Cleanup is deliberately after the exact summary commit. The journal is
    // the final record erased, so any power loss leaves enough information for
    // the lobby to resume this same transaction.
    const bool gameCleared = summary.mode == GameMode::Online ||
        ChessStorage::clearIfGameId(summary.gameId);
    if (!gameCleared || !ProfileStorage::clearPendingResult(summary)) {
        m_statusBar.setRight("Result cleanup pending");
        return false;
    }

    m_resultRecorded = true;
    return true;
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
    m_promotionModal.setEscapeCallback([this]() {
        m_promotionModal.hide();
        m_uiState = UIState::ShowMoves;
        if (!isNoSquare(m_selectedSquare)) {
            m_boardGrid.setCursor(toGridCol(m_selectedSquare.col),
                                  toGridRow(m_selectedSquare.row));
        }
        updateBoardHighlights();
        focusChain().focusWidget(&m_boardGrid);
    });
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

void ChessScene::requestExitToMenu() {
    if (CardGFX::scenes().active() != this || m_leavingToMenu ||
        m_exitModal.isVisible() || m_puzzleMode ||
        m_netMode == NetworkMode::Online || m_uiState == UIState::GameOver ||
        m_uiState == UIState::PromotionPending) {
        return;
    }

    m_preExitState = m_uiState;
    m_uiState = UIState::ExitConfirm;
    // A confirmation dialog must not become a pause button in a timed game.
    m_clockPausedForExit = false;

    m_exitModal.clearButtons();
    m_exitModal.setTitle("Leave game?");
    m_exitModal.setMessage("Current game will be discarded.");
    m_exitModal.setEscapeCallback([this]() { cancelExitToMenu(); });
    m_exitModal.addButton("Stay", [this]() { cancelExitToMenu(); });
    m_exitModal.addButton("Menu", [this]() { leaveToMenu(); });
    m_exitModal.show();
    focusChain().focusWidget(&m_exitModal);
}

void ChessScene::cancelExitToMenu() {
    if (!m_exitModal.isVisible() || m_leavingToMenu) return;

    m_exitModal.hide();
    m_uiState = m_preExitState;
    if (m_clockPausedForExit) {
        m_clock.resume(millis());
        m_clockPausedForExit = false;
    }
    focusChain().focusWidget(&m_boardGrid);
    if (m_uiState == UIState::Reviewing) {
        // Rebuild the historical position and its Review x/y status instead of
        // replacing it with the live-game turn/cursor status.
        reviewGoTo(m_reviewIndex);
    } else {
        updateStatusBar();
    }
    m_boardGrid.markDirty();
}

void ChessScene::leaveToMenu(bool discardUnsavedResult) {
    if (CardGFX::scenes().active() != this || m_leavingToMenu) return;
    if (m_resultOutcome != GameOutcome::None && !m_resultRecorded &&
        !discardUnsavedResult) {
        recordCompletedGame();
        if (!m_resultRecorded && !m_terminalJournaled) {
            showUnsavedResultModal(false);
            return;
        }
    }

    m_leavingToMenu = true;
    m_exitModal.hide();
    m_promotionModal.hide();
    m_gameOverModal.hide();
    m_actionModal.hide();
    if (m_resultOutcome == GameOutcome::None || m_resultRecorded ||
        discardUnsavedResult) {
        if (m_resultOutcome == GameOutcome::None || discardUnsavedResult) {
            ChessStorage::clearIfGameId(m_participants.gameId);
        }
        // This sidecar is legacy-only. A guarded exact clear may tidy a known
        // record, but corrupt/future/unrelated metadata is preserved.
        ProfileStorage::clearActiveGame(m_participants);
    }

    // Reset transient session state without calling newGame(); queued input is
    // still routed to this scene until the current frame finishes.
    m_aiDifficulty = AIDifficulty::None;
    m_aiThinking = false;
    m_localColor = PieceColor::White;
    m_boardFlipped = false;
    m_clock.configure(TimeControl::None);
    m_moveAnim.active = false;
    m_moveAnim.pendingFlip = false;
    m_uiState = UIState::SelectPiece;
    m_selectedSquare = NO_SQUARE;
    m_legalMoves.clear();

    CardGFX::scenes().pop();
}

void ChessScene::leaveOnlineGame(bool force, bool discardUnsavedResult) {
    if (CardGFX::scenes().active() != this || m_leavingToMenu) return;
    const bool reviewingFinishedGame =
        m_uiState == UIState::Reviewing &&
        m_preReviewState == UIState::GameOver;
    const bool terminalControlPending =
        (m_controlAwaitingAck &&
         m_pendingControl.control == NetControlType::GameEnd) ||
        (m_hasQueuedControl &&
         m_queuedControl.control == NetControlType::GameEnd);
    const bool withinTerminalDrain = m_terminalDrainUntil != 0 &&
        static_cast<int32_t>(millis() - m_terminalDrainUntil) < 0;
    if (!force &&
        (m_uiState == UIState::GameOver || reviewingFinishedGame) &&
        (withinTerminalDrain || terminalControlPending)) {
        if (m_gameOverModal.isVisible()) {
            m_gameOverModal.setMessage("Sending result; try again.");
        } else {
            m_statusBar.setRight("Sending result...");
        }
        return;
    }
    if (m_resultOutcome != GameOutcome::None && !m_resultRecorded &&
        !discardUnsavedResult) {
        recordCompletedGame();
        if (!m_resultRecorded && !m_terminalJournaled) {
            showUnsavedResultModal(true);
            return;
        }
    }

    m_leavingToMenu = true;
    m_exitModal.hide();
    m_promotionModal.hide();
    m_gameOverModal.hide();
    m_actionModal.hide();
    // Online sessions never own the local Resume slot or its legacy participant
    // sidecar, so leaving one must not modify either record.
    clearNetworkMode();
    CardGFX::scenes().pop();
}

void ChessScene::quitUnfinishedOnlineGame() {
    if (m_netMode != NetworkMode::Online || m_leavingToMenu ||
        m_resultOutcome != GameOutcome::None) {
        return;
    }
    // Finish as a local loss and send the MACed GameEnd now. leaveOnlineGame
    // still waits for the terminal drain or the Quit Anyway path.
    // Reuse an in-flight control id so the peer is still waiting for this
    // event. A fresh id is rejected as out of sequence.
    if (m_controlAwaitingAck && m_pendingControl.eventId != 0) {
        m_nextControlEventId = m_pendingControl.eventId;
    }
    m_finishDrawOnAck = false;
    m_clockPausedForDrawAccept = false;
    m_timeGiftPending = false;
    m_controlAwaitingAck = false;
    m_hasQueuedControl = false;
    m_pendingControl = ControlNetMsg{};
    const GameOutcome outcome = m_localColor == PieceColor::White
        ? GameOutcome::BlackWin : GameOutcome::WhiteWin;
    finishGame(outcome, TerminationReason::Resignation, "Resigned",
               outcome == GameOutcome::WhiteWin ? "White wins!" : "Black wins!",
               true);
}

void ChessScene::showUnsavedResultModal(bool online) {
    m_actionModal.clearButtons();
    m_actionModal.setTitle("Result Not Saved");
    m_actionModal.setMessage("Storage is unavailable. Discard this result?");
    auto stay = [this]() {
        m_actionModal.hide();
        if (m_uiState == UIState::Reviewing &&
            m_preReviewState == UIState::GameOver) {
            focusChain().focusWidget(&m_boardGrid);
        } else {
            m_gameOverModal.show();
            focusChain().focusWidget(&m_gameOverModal);
        }
    };
    m_actionModal.setEscapeCallback(stay);
    m_actionModal.addButton("Stay", stay);
    m_actionModal.addButton("Discard", [this, online]() {
        m_actionModal.hide();
        if (online) leaveOnlineGame(true, true);
        else leaveToMenu(true);
    });
    m_actionModal.show();
    focusChain().focusWidget(&m_actionModal);
}

void ChessScene::showGameOverModal(const char* title, const char* message) {
    m_uiState = UIState::GameOver;
    // A terminal result supersedes a transient disconnect warning. Clearing
    // this flag prevents a recovered packet later in the same tick from
    // hiding the newly configured result modal.
    m_disconnectShown = false;
    m_promotionModal.hide();
    m_exitModal.hide();
    m_actionModal.hide();
    m_clockPausedForExit = false;
    m_clockPausedForReview = false;

    m_gameOverModal.clearButtons();
    m_gameOverModal.setTitle(title);
    m_gameOverModal.setMessage(message);

    if (m_netMode == NetworkMode::Online) {
        m_gameOverModal.setEscapeCallback([this]() { leaveOnlineGame(); });
        m_gameOverModal.setInputCallback(
            [this](const InputEvent& event) {
                if (event.key != 'n' && event.key != 'N') return false;
                leaveOnlineGame();
                return true;
            });
        m_gameOverModal.addButton("Lobby", [this]() { leaveOnlineGame(); });
    } else {
        m_gameOverModal.setEscapeCallback([this]() { leaveToMenu(); });
        m_gameOverModal.setInputCallback([this](const InputEvent& event) {
            if (event.key != 'n' && event.key != 'N') return false;
            leaveToMenu();
            return true;
        });
        m_gameOverModal.addButton("Menu", [this]() { leaveToMenu(); });
    }
    if (!m_historyOverflow) {
        m_gameOverModal.addButton("Review", [this]() {
            m_gameOverModal.hide();
            enterReviewMode();
        });
    }

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
    const ChessBoard& displayBoard =
        self->m_uiState == UIState::Reviewing
            ? self->m_reviewBoard : self->m_board;
    Piece piece = displayBoard.at(boardCol, boardRow);
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
        !self->m_gameOverModal.isVisible() &&
        !self->m_exitModal.isVisible()) {
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

    // Recover the start of the retained history, including puzzle positions.
    ChessBoard tempBoard = m_board;
    for (int i = m_historyCount - 1; i >= 0; --i) tempBoard.unmakeMove(m_history[i]);

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
            } else {
                char line[32];
                snprintf(line, sizeof(line), "%d... %s", tempBoard.fullmoveNumber() - 1, sanBuf);
                m_moveList.addItem(line);
            }
        }
    }

    if (m_moveList.itemCount() > 0) {
        m_moveList.scrollToBottom();
    }
}

// ── Persistence ──────────────────────────────────────────────────────

bool ChessScene::saveGameState() {
    // Don't save network games (connection can't survive power cycle)
    if (m_netMode == NetworkMode::Online) return true;

    m_lastGameSaveAttempt = millis();
    const ChessClockSnapshot clock = m_clock.snapshot(millis());
    const bool saved = ChessStorage::saveGame(
        m_board, m_history, m_historyCount, m_historyOverflow,
        m_aiDifficulty, m_aiColor, m_localColor, m_boardFlipped,
        m_variant, m_positionIndex, m_timeControl,
        clock.whiteRemainingMs, clock.blackRemainingMs, clock.started,
        m_participants);
    m_gameSaveDirty = !saved;
    if (!saved && m_uiState != UIState::GameOver) {
        m_statusBar.setRight("Game save failed");
    }
    return saved;
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
    ActiveGameParticipants loadedParticipants;

    const ChessStorage::LoadResult loadResult = ChessStorage::loadGame(
        m_board, m_history, histCount, histOverflow,
        aiDiff, aiCol, localCol, flipped, variant,
        posIndex, tc, twMs, tbMs, timerRun, loadedParticipants);
    if (loadResult.status != ChessStorage::LoadStatus::Loaded) {
        return false;
    }

    // Restore mode state
    m_variant = variant;
    m_positionIndex = posIndex;
    m_board.setVariant(variant);
    m_board.setPositionIndex(posIndex);
    m_timeControl = tc;
    m_clock.configure(tc);
    if (tc != TimeControl::None) {
        ChessClockSnapshot clock;
        clock.timeControl = tc;
        clock.whiteRemainingMs = twMs;
        clock.blackRemainingMs = tbMs;
        clock.activeSide = m_board.sideToMove();
        // A timed game resumes with the side-to-move clock active. This also
        // upgrades older pre-first-move saves to conventional clock behavior.
        clock.started = true;
        clock.paused = false;
        if (!m_clock.loadSnapshot(clock, millis())) return false;
    }
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
    m_resultOutcome = GameOutcome::None;
    m_termination = TerminationReason::None;
    m_resultRecorded = false;
    m_terminalSummaryReady = false;
    m_terminalJournaled = false;
    m_terminalSummary = GameSummary{};
    m_lastResultSaveAttempt = 0;
    m_gameSaveDirty = false;
    m_lastGameSaveAttempt = 0;
    m_clockPausedForReview = false;
    m_clockPausedForExit = false;
    if (loadResult.participantsEmbedded) {
        m_participants = loadedParticipants;
    } else {
        // Legacy active_meta was a separate write and cannot be proven to
        // belong to this board. Attribute the migration only to the currently
        // loaded profile (when available), otherwise use explicit guest names.
        m_participants = ActiveGameParticipants{};
        ProfileData profiles;
        const PlayerProfile* active =
            ProfileStorage::load(profiles) ==
                    ProfileStorage::LoadStatus::Loaded
                ? GameRecords::activeProfile(profiles) : nullptr;
        m_participants.valid = true;
        m_participants.gameId = ChessZobrist::hash(m_board) ^ millis();
        if (m_participants.gameId == 0) m_participants.gameId = 1;
        m_participants.mode = aiDiff == AIDifficulty::None
            ? GameMode::Local : GameMode::AI;
        if (active) {
            if (localCol == PieceColor::White) {
                m_participants.whiteProfileId = active->id;
                std::strncpy(m_participants.whiteName, active->name,
                             PLAYER_NAME_MAX);
            } else {
                m_participants.blackProfileId = active->id;
                std::strncpy(m_participants.blackName, active->name,
                             PLAYER_NAME_MAX);
            }
        }
        if (m_participants.whiteName[0] == '\0') {
            std::strcpy(m_participants.whiteName,
                        aiDiff != AIDifficulty::None && aiCol == PieceColor::White
                            ? "Computer" : "White");
        }
        if (m_participants.blackName[0] == '\0') {
            std::strcpy(m_participants.blackName,
                        aiDiff != AIDifficulty::None && aiCol == PieceColor::Black
                            ? "Computer" : "Black");
        }
    }

    // For local pass-and-play, recalculate flip from sideToMove
    // (saveGameState runs before the pending animation flip completes)
    if (m_aiDifficulty == AIDifficulty::None) {
        m_boardFlipped = (m_board.sideToMove() == PieceColor::Black);
    }
    m_historyCount = histCount;
    m_historyOverflow = histOverflow;
    m_reviewBoard = m_board;

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

    updateHintBar();

    // Position cursor on the last move destination or king
    if (!isNoSquare(m_lastTo)) {
        m_boardGrid.setCursor(toGridCol(m_lastTo.col), toGridRow(m_lastTo.row));
    } else {
        Square king = m_board.findKing(m_board.sideToMove());
        if (!isNoSquare(king)) {
            m_boardGrid.setCursor(toGridCol(king.col), toGridRow(king.row));
        }
    }

    if (loadResult.sourceVersion < ChessStorageCodec::CURRENT_VERSION) {
        // Do not let play continue on a legacy save until its board and
        // participants have been atomically upgraded to the current format.
        if (!saveGameState()) return false;
    }

    m_boardGrid.markDirty();
    return true;
}

// ── Review Mode ──────────────────────────────────────────────────────

void ChessScene::enterReviewMode() {
    if (m_uiState != UIState::SelectPiece &&
        m_uiState != UIState::GameOver) {
        return;
    }
    if (m_netMode == NetworkMode::Online &&
        m_uiState != UIState::GameOver) {
        m_statusBar.setRight("Review after game");
        return;
    }
    if (m_timeControl != TimeControl::None &&
        m_uiState != UIState::GameOver) {
        m_statusBar.setRight("Review after game");
        return;
    }
    if (m_historyOverflow) {
        m_actionModal.clearButtons();
        m_actionModal.setTitle("Review Unavailable");
        m_actionModal.setMessage("This game exceeded the 250-ply review limit.");
        auto closeUnavailable = [this]() {
            m_actionModal.hide();
            if (m_uiState == UIState::GameOver) {
                m_gameOverModal.show();
                focusChain().focusWidget(&m_gameOverModal);
            } else {
                closeActionModal();
            }
        };
        m_actionModal.setEscapeCallback(closeUnavailable);
        m_actionModal.addButton("Close", closeUnavailable);
        m_actionModal.show();
        focusChain().focusWidget(&m_actionModal);
        return;
    }
    m_preReviewState = m_uiState;
    // Historical boards must not inherit selection/legal-move highlights from
    // the live position. The normal hotkey already requires SelectPiece; this
    // also keeps button/programmatic entry defensive.
    m_selectedSquare = NO_SQUARE;
    m_legalMoves.clear();
    m_uiState = UIState::Reviewing;
    m_reviewIndex = m_historyCount;
    if (m_netMode != NetworkMode::Online &&
        m_preReviewState != UIState::GameOver) {
        m_clockPausedForReview = m_clock.pause(millis());
    }
    updateHintBar();
    m_movesLabel.setText("Review");
    focusChain().focusWidget(&m_boardGrid);
    reviewGoTo(m_reviewIndex);
}

void ChessScene::exitReviewMode() {
    // Restore board to final position
    reviewGoTo(m_historyCount);
    m_uiState = m_preReviewState;
    updateMovesLabel();
    if (m_clockPausedForReview && m_preReviewState != UIState::GameOver) {
        m_clock.resume(millis());
        m_clockPausedForReview = false;
    }

    if (m_preReviewState == UIState::GameOver) {
        // Re-show the retained result. Re-deriving it from the board would
        // lose timeout and resignation results, which are not board states.
        m_gameOverModal.show();
        focusChain().focusWidget(&m_gameOverModal);
    } else {
        updateHintBar();
        focusChain().focusWidget(&m_boardGrid);
    }
    updateStatusBar();
}

void ChessScene::reviewGoTo(uint8_t index) {
    if (index > m_historyCount) index = m_historyCount;
    m_reviewIndex = index;

    // Replay from start to index
    ChessBoard tempBoard;
    tempBoard.setVariant(m_variant);
    tempBoard.setPositionIndex(m_positionIndex);
    tempBoard.reset();

    for (uint8_t i = 0; i < index; i++) {
        tempBoard.makeMove(m_history[i].move);
    }

    m_reviewBoard = tempBoard;

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
    snprintf(leftBuf, sizeof(leftBuf), "Ply %u/%u",
             static_cast<unsigned>(index),
             static_cast<unsigned>(m_historyCount));
    m_statusBar.setLeft(leftBuf);
    m_statusBar.setCenter("");

    char evalBuf[16];
    if (ChessRules::isCheckmate(m_reviewBoard)) {
        std::snprintf(evalBuf, sizeof(evalBuf), "Mate %c",
                      m_reviewBoard.sideToMove() == PieceColor::White ? 'B' : 'W');
    } else if (ChessRules::isStalemate(m_reviewBoard) ||
               ChessRules::isDraw50Move(m_reviewBoard) ||
               ChessRules::isInsufficientMaterial(m_reviewBoard) ||
               ChessRules::isThreefoldRepetition(
                   m_reviewBoard, m_history, index)) {
        std::strcpy(evalBuf, "Draw");
    } else {
        int16_t eval = ChessAI::evaluate(m_reviewBoard);
        // The engine evaluates the side to move. Review uses the conventional
        // White-relative sign so the value has one stable meaning at every ply.
        if (m_reviewBoard.sideToMove() == PieceColor::Black) eval = -eval;
        const int32_t magnitude = eval < 0
            ? -static_cast<int32_t>(eval) : static_cast<int32_t>(eval);
        std::snprintf(evalBuf, sizeof(evalBuf), "Eval %c%ld.%02ld",
                      eval < 0 ? '-' : '+',
                      static_cast<long>(magnitude / 100),
                      static_cast<long>(magnitude % 100));
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
    m_clock.configure(tc);
}

void ChessScene::setParticipants(
        const ActiveGameParticipants& participants) {
    m_participants = participants;
}

// ── Puzzle Mode ──────────────────────────────────────────────────────

void ChessScene::setPuzzleMode(uint8_t puzzleIndex) {
    m_puzzleMode = false;
    m_puzzleIndex = puzzleIndex;
    m_puzzleSolutionStep = 0;
    m_puzzleHintLevel = 0;
    m_puzzleAutoPlayPending = false;

    // Load puzzle
    uint8_t prating = 0;
    if (!loadPuzzleIntoBoard(puzzleIndex, m_board, m_puzzleSolution,
                             m_puzzleSolutionLen, m_puzzleType, prating)) {
        // Do not render or play the previous board with invalid puzzle metadata.
        // Present a safe exit instead; this also works when called by "Next".
        m_uiState = UIState::GameOver;
        m_selectedSquare = NO_SQUARE;
        m_legalMoves.clear();
        m_moveAnim.active = false;
        m_clock.configure(TimeControl::None);

        m_statusBar.setLeft("Puzzle");
        m_statusBar.setCenter("Unavailable");
        m_statusBar.setRight("Error");
        m_hintBar.setText("");
        m_movesLabel.setText("Puzzle");

        m_gameOverModal.clearButtons();
        m_gameOverModal.setTitle("Puzzle Error");
        m_gameOverModal.setMessage("Puzzle unavailable.");
        m_gameOverModal.addButton("Menu", [this]() {
            m_gameOverModal.hide();
            clearPuzzleMode();
            CardGFX::scenes().pop();
        });
        m_gameOverModal.show();
        focusChain().focusWidget(&m_gameOverModal);
        m_boardGrid.markDirty();
        return;
    }
    m_puzzleMode = true;
    m_puzzleRating = prating;
    m_puzzleFeedbackUntil = 0;

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
    m_timeControl = TimeControl::None;
    m_clock.configure(TimeControl::None);

    // Flip board if Black to move
    m_boardFlipped = (m_board.sideToMove() == PieceColor::Black);

    // Position cursor on center
    Square king = m_board.findKing(m_board.sideToMove());
    if (!isNoSquare(king)) {
        m_boardGrid.setCursor(toGridCol(king.col), toGridRow(king.row));
    }

    updateStatusBar();
    updateHintBar();
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

namespace {

void advanceNonZero(uint16_t& sequence) {
    ++sequence;
    if (sequence == 0) sequence = 1;
}

uint32_t saturatingAddMs(uint32_t value, uint32_t addition) {
    const uint32_t maximum = std::numeric_limits<uint32_t>::max();
    return addition > maximum - value ? maximum : value + addition;
}

constexpr uint32_t kRemoteClockSkewMs = 200;

NetGameResult toNetResult(GameOutcome outcome) {
    switch (outcome) {
        case GameOutcome::WhiteWin: return NetGameResult::WhiteWin;
        case GameOutcome::BlackWin: return NetGameResult::BlackWin;
        case GameOutcome::Draw: return NetGameResult::Draw;
        default: return NetGameResult::None;
    }
}

NetTermination toNetTermination(TerminationReason termination) {
    switch (termination) {
        case TerminationReason::Checkmate: return NetTermination::Checkmate;
        case TerminationReason::Timeout: return NetTermination::Timeout;
        case TerminationReason::Resignation: return NetTermination::Resignation;
        case TerminationReason::Agreement: return NetTermination::Agreement;
        case TerminationReason::Stalemate: return NetTermination::Stalemate;
        case TerminationReason::Repetition: return NetTermination::Repetition;
        case TerminationReason::FiftyMove: return NetTermination::FiftyMove;
        case TerminationReason::InsufficientMaterial:
            return NetTermination::Insufficient;
        case TerminationReason::Disconnection: return NetTermination::Disconnect;
        default: return NetTermination::Unknown;
    }
}

bool fromNetResult(uint8_t value, GameOutcome& outcome) {
    switch (static_cast<NetGameResult>(value)) {
        case NetGameResult::WhiteWin: outcome = GameOutcome::WhiteWin; return true;
        case NetGameResult::BlackWin: outcome = GameOutcome::BlackWin; return true;
        case NetGameResult::Draw: outcome = GameOutcome::Draw; return true;
        default: return false;
    }
}

bool fromNetTermination(uint8_t value, TerminationReason& termination) {
    switch (static_cast<NetTermination>(value)) {
        case NetTermination::Checkmate:
            termination = TerminationReason::Checkmate; return true;
        case NetTermination::Timeout:
            termination = TerminationReason::Timeout; return true;
        case NetTermination::Resignation:
            termination = TerminationReason::Resignation; return true;
        case NetTermination::Agreement:
            termination = TerminationReason::Agreement; return true;
        case NetTermination::Stalemate:
            termination = TerminationReason::Stalemate; return true;
        case NetTermination::Repetition:
            termination = TerminationReason::Repetition; return true;
        case NetTermination::FiftyMove:
            termination = TerminationReason::FiftyMove; return true;
        case NetTermination::Insufficient:
            termination = TerminationReason::InsufficientMaterial; return true;
        case NetTermination::Disconnect:
            termination = TerminationReason::Disconnection; return true;
        default: return false;
    }
}

const char* terminalTitle(TerminationReason termination) {
    switch (termination) {
        case TerminationReason::Checkmate: return "Checkmate!";
        case TerminationReason::Timeout: return "Time's Up!";
        case TerminationReason::Resignation: return "Opponent Resigned";
        case TerminationReason::Agreement: return "Draw Agreed";
        case TerminationReason::Stalemate: return "Stalemate!";
        case TerminationReason::Repetition: return "Threefold Rep.";
        case TerminationReason::FiftyMove: return "50-Move Rule";
        case TerminationReason::InsufficientMaterial: return "Insufficient";
        default: return "Game Over";
    }
}

} // namespace

bool ChessScene::remoteClockRaiseAllowed(PieceColor color, uint32_t sampleMs,
                                         uint32_t maxRaiseMs) const {
    if (!m_clock.enabled()) return true;
    const uint8_t index = static_cast<uint8_t>(color);
    if (index > 1 || !m_hasRemoteClockBaseline[index]) return true;
    return sampleMs <= saturatingAddMs(m_remoteClockBaselineMs[index], maxRaiseMs);
}

void ChessScene::acceptRemoteClockSample(PieceColor color, uint32_t sampleMs) {
    if (!m_clock.enabled()) return;
    const uint8_t index = static_cast<uint8_t>(color);
    if (index > 1) return;
    m_hasRemoteClockBaseline[index] = true;
    m_remoteClockBaselineMs[index] = sampleMs;
}

void ChessScene::setNetworkMode(PieceColor localColor, uint32_t sessionId,
                                uint16_t gameId,
                                const uint8_t opponentMac[6],
                                const char* localName,
                                const char* opponentName,
                                bool reackGameStart,
                                const uint8_t macKey[NetCrypto::KEY_SIZE]) {
    m_netMode = NetworkMode::Online;
    if (macKey != nullptr) std::memcpy(m_macKey, macKey, sizeof(m_macKey));
    else std::memset(m_macKey, 0, sizeof(m_macKey));
    m_hasRemoteClockBaseline[0] = false;
    m_hasRemoteClockBaseline[1] = false;
    m_remoteClockBaselineMs[0] = 0;
    m_remoteClockBaselineMs[1] = 0;
    m_localColor = localColor;
    m_sessionId = sessionId;
    m_networkGameId = gameId;
    if (opponentMac) std::memcpy(m_opponentMac, opponentMac, 6);
    else std::memset(m_opponentMac, 0, sizeof(m_opponentMac));
    copyNetDisplayName(m_opponentName, opponentName);
    if (m_opponentName[0] == '\0') std::strcpy(m_opponentName, "Player");
    m_reackGameStart = reackGameStart;
    m_boardFlipped = (localColor == PieceColor::Black);
    m_awaitingAck = false;
    m_nextMoveSequence = 1;
    m_lastSentSeq = 0;
    m_expectedRemoteSequence = 1;
    m_lastAppliedRemoteSequence = 0;
    m_lastRemotePreHash = 0;
    m_lastRemotePostHash = 0;
    m_lastMoveSendTime = millis();
    m_lastHeartbeatSendTime = 0;
    m_moveRetryCount = 0;
    m_controlAwaitingAck = false;
    m_pendingControl = ControlNetMsg{};
    m_nextControlEventId = 1;
    m_expectedRemoteControlEventId = 1;
    m_lastRemoteControlEventId = 0;
    m_lastRemoteControl = ControlNetMsg{};
    m_lastRemoteControlStatus = NetAckStatus::Rejected;
    m_localDrawOfferEventId = 0;
    m_localDrawOfferBoardHash = 0;
    m_remoteDrawOfferEventId = 0;
    m_lastLocalDrawAcceptEventId = 0;
    m_finishDrawOnAck = false;
    m_clockPausedForDrawAccept = false;
    m_controlRetryCount = 0;
    m_hasQueuedControl = false;
    m_queuedControl = ControlNetMsg{};
    m_remoteClockPending = false;
    m_disconnectShown = false;
    m_disconnectGraceUntil = 0;
    m_lastValidPeerPacketTime = millis();

    char localDisplay[PLAYER_NAME_MAX + 1] = {};
    if (!GameRecords::sanitizeName(localName, localDisplay)) {
        std::strcpy(localDisplay, "Player");
    }
    ProfileData profiles;
    const bool profilesLoaded =
        ProfileStorage::load(profiles) == ProfileStorage::LoadStatus::Loaded;
    const PlayerProfile* active = profilesLoaded
        ? GameRecords::activeProfile(profiles) : nullptr;
    // A profile ID is meaningful only if it belongs to the exact identity
    // advertised during pairing. Never attach stale metadata to a new session.
    if (active && std::strcmp(active->name, localDisplay) != 0) active = nullptr;
    m_participants = ActiveGameParticipants{};
    m_participants.valid = sessionId != 0;
    m_participants.gameId = sessionId;
    m_participants.mode = GameMode::Online;
    if (localColor == PieceColor::White) {
        if (active) {
            m_participants.whiteProfileId = active->id;
        }
        std::strncpy(m_participants.whiteName, localDisplay,
                     PLAYER_NAME_MAX);
        std::strncpy(m_participants.blackName, m_opponentName,
                     PLAYER_NAME_MAX);
    } else {
        std::strncpy(m_participants.whiteName, m_opponentName,
                     PLAYER_NAME_MAX);
        if (active) {
            m_participants.blackProfileId = active->id;
        }
        std::strncpy(m_participants.blackName, localDisplay,
                     PLAYER_NAME_MAX);
    }
    m_participants.whiteName[PLAYER_NAME_MAX] = '\0';
    m_participants.blackName[PLAYER_NAME_MAX] = '\0';
    newGame();
}

void ChessScene::clearNetworkMode() {
    EspNowTransport::instance().shutdown();
    m_netMode = NetworkMode::Local;
    m_localColor = PieceColor::White;
    m_boardFlipped = false;
    m_applyingRemoteMove = false;
    m_remoteClockPending = false;
    m_awaitingAck = false;
    m_controlAwaitingAck = false;
    m_pendingControl = ControlNetMsg{};
    m_disconnectShown = false;
    m_networkGameId = 0;
    m_sessionId = 0;
    std::memset(m_opponentMac, 0, sizeof(m_opponentMac));
    std::memset(m_opponentName, 0, sizeof(m_opponentName));
    m_reackGameStart = false;
    std::memset(m_macKey, 0, sizeof(m_macKey));
    m_hasRemoteClockBaseline[0] = false;
    m_hasRemoteClockBaseline[1] = false;
    m_remoteClockBaselineMs[0] = 0;
    m_remoteClockBaselineMs[1] = 0;
    m_nextMoveSequence = 1;
    m_lastSentSeq = 0;
    m_expectedRemoteSequence = 1;
    m_lastAppliedRemoteSequence = 0;
    m_nextControlEventId = 1;
    m_expectedRemoteControlEventId = 1;
    m_lastRemoteControlEventId = 0;
    m_lastRemoteControl = ControlNetMsg{};
    m_lastRemoteControlStatus = NetAckStatus::Rejected;
    m_localDrawOfferEventId = 0;
    m_localDrawOfferBoardHash = 0;
    m_remoteDrawOfferEventId = 0;
    m_lastLocalDrawAcceptEventId = 0;
    m_finishDrawOnAck = false;
    m_clockPausedForDrawAccept = false;
    m_lastMoveSendTime = 0;
    m_lastHeartbeatSendTime = 0;
    m_lastControlSendTime = 0;
    m_moveRetryCount = 0;
    m_controlRetryCount = 0;
    m_hasQueuedControl = false;
    m_queuedControl = ControlNetMsg{};
    m_disconnectGraceUntil = 0;
    m_lastValidPeerPacketTime = 0;
    m_participants = ActiveGameParticipants{};
    newGame();
}

void ChessScene::sendMove(const Move& move, uint32_t preBoardHash,
                          uint32_t postBoardHash) {
    const uint16_t sequence = m_nextMoveSequence;
    advanceNonZero(m_nextMoveSequence);
    const uint32_t moverRemaining = m_clock.enabled()
        ? m_clock.remainingMs(opponent(m_board.sideToMove())) : 0;
    m_lastSentMove = moveToNetMsg(
        move, sequence, m_networkGameId, m_sessionId,
        moverRemaining, preBoardHash, postBoardHash);
    stampSessionPacket(&m_lastSentMove, sizeof(m_lastSentMove), m_macKey);
    m_lastSentSeq = sequence;
    m_lastSentPostHash = postBoardHash;
    m_awaitingAck = true;
    m_moveRetryCount = 0;
    m_lastMoveSendTime = millis();

    EspNowTransport::instance().send(
        reinterpret_cast<const uint8_t*>(&m_lastSentMove), sizeof(m_lastSentMove));
}

void ChessScene::sendHeartbeat() {
    HeartbeatMsg hb;
    hb.header.gameId = m_networkGameId;
    hb.header.sessionId = m_sessionId;
    hb.activeColor = static_cast<uint8_t>(m_board.sideToMove());
    hb.activeRemainingMs = m_clock.enabled()
        ? m_clock.remainingMsAt(m_board.sideToMove(), millis()) : 0;
    hb.positionEpoch = m_positionEpoch;
    hb.lastAppliedSequence = m_lastAppliedRemoteSequence;
    hb.boardHash = ChessZobrist::hash(m_board);
    stampSessionPacket(&hb, sizeof(hb), m_macKey);
    EspNowTransport::instance().send(
        reinterpret_cast<const uint8_t*>(&hb), sizeof(hb));
}

void ChessScene::sendGameStartAck() {
    GameStartAckMsg ack;
    ack.header.gameId = m_networkGameId;
    ack.header.sessionId = m_sessionId;
    stampSessionPacket(&ack, sizeof(ack), m_macKey);
    EspNowTransport::instance().send(
        reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
}

void ChessScene::sendMoveAck(const MoveNetMsg& msg, NetAckStatus status,
                             uint32_t boardHash) {
    MoveAckMsg ack;
    ack.header.gameId = m_networkGameId;
    ack.header.sessionId = m_sessionId;
    ack.sequence = msg.sequence;
    ack.status = status;
    ack.boardHash = boardHash;
    stampSessionPacket(&ack, sizeof(ack), m_macKey);
    EspNowTransport::instance().send(
        reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
}

void ChessScene::pollNetwork() {
    auto& transport = EspNowTransport::instance();
    const uint32_t now = millis();

    uint8_t buf[NET_PACKET_MAX_SIZE];
    uint8_t mac[6];
    while (transport.hasReceived()) {
        const uint8_t len = transport.receive(buf, sizeof(buf), mac);
        if (len == 0) break;
        if (!transport.isPeerMac(mac)) continue;

        const NetMsgType msgType = static_cast<NetMsgType>(buf[0]);

        switch (msgType) {
        case NetMsgType::MoveMsg:
            if (len == sizeof(MoveNetMsg) &&
                sessionPacketTagMatches(buf, len, m_macKey)) {
                MoveNetMsg moveMsg;
                memcpy(&moveMsg, buf, sizeof(moveMsg));
                if (isValidGameHeader(moveMsg.header, NetMsgType::MoveMsg,
                                      m_networkGameId, m_sessionId)) {
                    noteValidPeerPacket(now);
                    onRemoteMoveReceived(moveMsg);
                }
            }
            break;

        case NetMsgType::MoveAck:
            if (len == sizeof(MoveAckMsg) &&
                sessionPacketTagMatches(buf, len, m_macKey)) {
                MoveAckMsg ack;
                memcpy(&ack, buf, sizeof(ack));
                if (isValidGameHeader(ack.header, NetMsgType::MoveAck,
                                      m_networkGameId, m_sessionId) &&
                    m_awaitingAck && ack.sequence == m_lastSentSeq) {
                    noteValidPeerPacket(now);
                    if ((ack.status == NetAckStatus::Accepted ||
                         ack.status == NetAckStatus::Duplicate) &&
                        ack.boardHash == m_lastSentPostHash) {
                        m_awaitingAck = false;
                    } else if (ack.status == NetAckStatus::Accepted ||
                               ack.status == NetAckStatus::Duplicate) {
                        m_awaitingAck = false;
                        onConnectionLost("Game out of sync");
                    } else if (ack.status == NetAckStatus::Rejected) {
                        m_awaitingAck = false;
                        onConnectionLost("Game out of sync");
                    }
                }
            }
            break;

        case NetMsgType::Heartbeat: {
            if (len != sizeof(HeartbeatMsg) ||
                !sessionPacketTagMatches(buf, len, m_macKey)) {
                break;
            }
            HeartbeatMsg heartbeat;
            memcpy(&heartbeat, buf, sizeof(heartbeat));
            if (!isValidGameHeader(heartbeat.header, NetMsgType::Heartbeat,
                                   m_networkGameId, m_sessionId) ||
                !isValidColorValue(heartbeat.activeColor)) {
                break;
            }
            noteValidPeerPacket(now);
            const uint32_t boardHash = ChessZobrist::hash(m_board);
            const NetPositionRelation relation = classifyNetPosition(
                m_positionEpoch, boardHash,
                heartbeat.positionEpoch, heartbeat.boardHash);
            if (relation == NetPositionRelation::Match) {
                // If a newer heartbeat already proved the peer is one move
                // ahead, this exact-position heartbeat is merely reordered
                // stale traffic. Only receipt of the expected Move may clear
                // that causal deadline.
                if (m_awaitingAck &&
                    heartbeat.lastAppliedSequence == m_lastSentSeq &&
                    boardHash == m_lastSentPostHash) {
                    m_awaitingAck = false;
                }
                const PieceColor active =
                    static_cast<PieceColor>(heartbeat.activeColor);
                const bool reviewingFinishedGame =
                    m_uiState == UIState::Reviewing &&
                    m_preReviewState == UIState::GameOver;
                if (m_resultOutcome == GameOutcome::None &&
                    !reviewingFinishedGame && m_clock.enabled() &&
                    active == opponent(m_localColor) &&
                    active == m_board.sideToMove() &&
                    m_uiState != UIState::GameOver &&
                    remoteClockRaiseAllowed(active, heartbeat.activeRemainingMs,
                                            kRemoteClockSkewMs)) {
                    m_clock.setRemainingMs(active,
                                           heartbeat.activeRemainingMs, now);
                    acceptRemoteClockSample(active, heartbeat.activeRemainingMs);
                    updateStatusBar();
                }
            } else if (relation == NetPositionRelation::PeerAheadOne) {
                if (!m_missingRemoteMove ||
                    m_missingRemoteEpoch != heartbeat.positionEpoch) {
                    m_missingRemoteMove = true;
                    m_missingRemoteEpoch = heartbeat.positionEpoch;
                    m_missingRemoteMoveSince = now;
                }
            } else if (relation == NetPositionRelation::Diverged &&
                       m_uiState != UIState::GameOver) {
                onConnectionLost("Game out of sync");
            }
            break;
        }

        case NetMsgType::ControlMsg:
            if (len == sizeof(ControlNetMsg) &&
                sessionPacketTagMatches(buf, len, m_macKey)) {
                ControlNetMsg control;
                memcpy(&control, buf, sizeof(control));
                if (isValidGameHeader(control.header,
                                      NetMsgType::ControlMsg,
                                      m_networkGameId, m_sessionId)) {
                    noteValidPeerPacket(now);
                    onControlReceived(control);
                }
            }
            break;

        case NetMsgType::ControlAck:
            if (len == sizeof(ControlAckMsg) &&
                sessionPacketTagMatches(buf, len, m_macKey)) {
                ControlAckMsg ack;
                memcpy(&ack, buf, sizeof(ack));
                if (isValidGameHeader(ack.header, NetMsgType::ControlAck,
                                      m_networkGameId, m_sessionId) &&
                    m_controlAwaitingAck &&
                    ack.eventId == m_pendingControl.eventId &&
                    ack.control == m_pendingControl.control) {
                    noteValidPeerPacket(now);
                    if (ack.status == NetAckStatus::Accepted ||
                        ack.status == NetAckStatus::Duplicate) {
                        const bool completesTimeGift =
                            m_timeGiftPending &&
                            m_pendingControl.control ==
                                NetControlType::TimeGift;
                        if (completesTimeGift &&
                            (ack.boardHash != m_pendingControl.boardHash ||
                             ack.clockRemainingMs == 0)) {
                            // Do not guess whether the remote side committed the
                            // gift. Preserve the immutable pending packet so a
                            // deliberate Wait can retry it.
                            m_controlAwaitingAck = false;
                            onConnectionLost("Gift confirmation mismatch");
                            break;
                        }
                        if (completesTimeGift && m_clock.enabled() &&
                            !remoteClockRaiseAllowed(
                                m_timeGiftRecipient, ack.clockRemainingMs,
                                saturatingAddMs(15000, kRemoteClockSkewMs))) {
                            m_controlAwaitingAck = false;
                            onConnectionLost("Clock out of sync");
                            break;
                        }
                        const bool completesTerminalReceipt =
                            m_pendingControl.control ==
                                NetControlType::GameEnd;
                        const bool completesDraw =
                            m_finishDrawOnAck &&
                            m_pendingControl.control ==
                                NetControlType::DrawAccept &&
                            ack.boardHash == m_pendingControl.boardHash;
                        m_controlAwaitingAck = false;
                        if (completesTimeGift) {
                            m_clock.setRemainingMs(
                                m_timeGiftRecipient,
                                ack.clockRemainingMs, now);
                            acceptRemoteClockSample(m_timeGiftRecipient,
                                                    ack.clockRemainingMs);
                            m_timeGiftPending = false;
                            m_pendingControl = ControlNetMsg{};
                            updateStatusBar();
                            m_statusBar.setRight("Gave +15 sec");
                            sendQueuedControl();
                            finishDeferredTimeoutIfReady();
                        } else if (completesDraw) {
                            m_pendingControl = ControlNetMsg{};
                            m_finishDrawOnAck = false;
                            m_clockPausedForDrawAccept = false;
                            finishGame(GameOutcome::Draw,
                                       TerminationReason::Agreement,
                                       "Draw Agreed", "Game is a draw.",
                                       false);
                        } else if (m_finishDrawOnAck &&
                                   m_pendingControl.control ==
                                       NetControlType::DrawAccept) {
                            m_finishDrawOnAck = false;
                            m_pendingControl = ControlNetMsg{};
                            if (m_clockPausedForDrawAccept) {
                                m_clock.resume(now);
                                m_clockPausedForDrawAccept = false;
                            }
                            onConnectionLost("Draw confirmation mismatch");
                        } else {
                            m_pendingControl = ControlNetMsg{};
                            sendQueuedControl();
                            if (completesTerminalReceipt) {
                                m_terminalDrainUntil = now;
                                if (m_actionModal.isVisible()) {
                                    m_actionModal.hide();
                                    if (m_uiState == UIState::Reviewing &&
                                        m_preReviewState ==
                                            UIState::GameOver) {
                                        focusChain().focusWidget(&m_boardGrid);
                                    } else {
                                        focusChain().focusWidget(
                                            &m_gameOverModal);
                                    }
                                }
                            }
                        }
                    } else if (ack.status == NetAckStatus::Rejected) {
                        const bool rejectedTimeGift =
                            m_timeGiftPending &&
                            m_pendingControl.control ==
                                NetControlType::TimeGift;
                        // GameEnd can legitimately beat its final move through
                        // the radio. Keep retrying until the board hashes match.
                        if (m_pendingControl.control !=
                                NetControlType::GameEnd &&
                            m_hasQueuedControl &&
                            m_queuedControl.control ==
                                NetControlType::GameEnd) {
                            if (m_pendingControl.control ==
                                NetControlType::DrawOffer) {
                                // Stale draw offers are consumed by the peer,
                                // so its stream now expects our next event ID.
                                m_controlAwaitingAck = false;
                                m_localDrawOfferEventId = 0;
                                m_localDrawOfferBoardHash = 0;
                                m_pendingControl = ControlNetMsg{};
                                sendQueuedControl();
                                break;
                            }
                            // Rejection does not advance the receiver's event
                            // sequence. Reuse this event ID for the terminal
                            // packet so the game result cannot be stranded
                            // behind a rejected draw/time action.
                            m_controlAwaitingAck = false;
                            sendQueuedControl(true);
                        } else if (m_pendingControl.control ==
                                   NetControlType::DrawOffer) {
                            m_controlAwaitingAck = false;
                            m_localDrawOfferEventId = 0;
                            m_localDrawOfferBoardHash = 0;
                            m_pendingControl = ControlNetMsg{};
                            m_statusBar.setRight("Draw offer expired");
                            sendQueuedControl();
                        } else if (m_pendingControl.control !=
                                   NetControlType::GameEnd) {
                            if (m_pendingControl.control ==
                                NetControlType::DrawOffer) {
                                m_localDrawOfferEventId = 0;
                                m_localDrawOfferBoardHash = 0;
                            }
                            if (m_finishDrawOnAck) {
                                m_finishDrawOnAck = false;
                                if (m_clockPausedForDrawAccept) {
                                    m_clock.resume(now);
                                    m_clockPausedForDrawAccept = false;
                                }
                            }
                            m_controlAwaitingAck = false;
                            m_hasQueuedControl = false;
                            m_pendingControl = ControlNetMsg{};
                            if (m_uiState != UIState::GameOver) {
                                onConnectionLost("Action rejected");
                            }
                        }
                        if (rejectedTimeGift) {
                            m_timeGiftPending = false;
                            finishDeferredTimeoutIfReady();
                        }
                    }
                }
            }
            break;

        case NetMsgType::GameStart:
            if (m_reackGameStart && len == sizeof(GameStartMsg) &&
                sessionPacketTagMatches(buf, len, m_macKey)) {
                GameStartMsg start;
                memcpy(&start, buf, sizeof(start));
                if (isValidGameHeader(start.header, NetMsgType::GameStart,
                                      m_networkGameId, m_sessionId) &&
                    start.yourColor == static_cast<uint8_t>(m_localColor) &&
                    start.variant == static_cast<uint8_t>(m_variant) &&
                    start.positionIndex == m_positionIndex &&
                    start.timeControl == static_cast<uint8_t>(m_timeControl)) {
                    noteValidPeerPacket(now);
                    sendGameStartAck();
                }
            }
            break;

        case NetMsgType::Resign:
            // Legacy one-way resign is ignored even when the header is valid.
            break;

        default:
            break;
        }
    }

    if (m_awaitingAck && (now - m_lastMoveSendTime) > 200) {
        ++m_moveRetryCount;
        if (m_moveRetryCount > 25) {
            m_awaitingAck = false;
            onConnectionLost();
        } else {
            m_lastMoveSendTime = now;
            transport.send(
                reinterpret_cast<const uint8_t*>(&m_lastSentMove),
                sizeof(m_lastSentMove));
        }
    }

    const bool hasTerminalGameEnd =
        m_pendingControl.control == NetControlType::GameEnd ||
        (m_hasQueuedControl &&
         m_queuedControl.control == NetControlType::GameEnd);
    const bool drainingTerminalControl =
        hasTerminalGameEnd || m_finishDrawOnAck;
    const uint32_t controlRetryInterval =
        drainingTerminalControl && m_controlRetryCount >= 25 ? 1000 : 200;
    if (m_controlAwaitingAck &&
        (now - m_lastControlSendTime) > controlRetryInterval) {
        if (m_controlRetryCount < 255) ++m_controlRetryCount;
        if (m_controlRetryCount == 25 && m_finishDrawOnAck) {
            m_actionModal.clearButtons();
            m_actionModal.setTitle("Draw Pending");
            m_actionModal.setMessage("No confirmation yet.");
            auto keepWaitingForDraw = [this]() {
                m_controlRetryCount = 0;
                m_lastControlSendTime = millis();
                m_actionModal.clearButtons();
                m_actionModal.setTitle("Draw Agreement");
                m_actionModal.setMessage("Confirming with opponent...");
                m_actionModal.setEscapeCallback([]() {});
                focusChain().focusWidget(&m_actionModal);
            };
            m_actionModal.setEscapeCallback(keepWaitingForDraw);
            m_actionModal.addButton("Keep Waiting", keepWaitingForDraw);
            m_actionModal.addButton("Quit", [this]() {
                quitUnfinishedOnlineGame();
            });
            m_actionModal.show();
            focusChain().focusWidget(&m_actionModal);
        } else if (m_controlRetryCount >= 25 &&
                   hasTerminalGameEnd &&
                   (m_uiState == UIState::GameOver ||
                    (m_uiState == UIState::Reviewing &&
                     m_preReviewState == UIState::GameOver)) &&
                   !m_actionModal.isVisible()) {
            m_actionModal.clearButtons();
            m_actionModal.setTitle("Result Pending");
            m_actionModal.setMessage("Waiting for opponent receipt.");
            auto keepWaitingForResult = [this]() {
                m_actionModal.hide();
                m_controlRetryCount = 0;
                m_lastControlSendTime = millis();
                if (m_uiState == UIState::Reviewing &&
                    m_preReviewState == UIState::GameOver) {
                    focusChain().focusWidget(&m_boardGrid);
                } else {
                    focusChain().focusWidget(&m_gameOverModal);
                }
            };
            m_actionModal.setEscapeCallback(keepWaitingForResult);
            m_actionModal.addButton("Keep Waiting", keepWaitingForResult);
            m_actionModal.addButton("Quit Anyway", [this]() {
                m_controlAwaitingAck = false;
                m_hasQueuedControl = false;
                m_pendingControl = ControlNetMsg{};
                leaveOnlineGame(true);
            });
            m_actionModal.show();
            focusChain().focusWidget(&m_actionModal);
        }
        if (m_controlRetryCount > 25 && !drainingTerminalControl) {
            m_controlAwaitingAck = false;
            if (m_uiState != UIState::GameOver) onConnectionLost();
        } else {
            m_lastControlSendTime = now;
            transport.send(
                reinterpret_cast<const uint8_t*>(&m_pendingControl),
                sizeof(m_pendingControl));
        }
    }

    if ((now - m_lastHeartbeatSendTime) > 500) {
        m_lastHeartbeatSendTime = now;
        sendHeartbeat();
    }

    // Heartbeats prove liveness but must not extend this causal deadline. A
    // peer one position ahead means the exact next Move is missing.
    if (m_missingRemoteMove &&
        (now - m_missingRemoteMoveSince) > 5200 &&
        m_uiState != UIState::GameOver) {
        m_missingRemoteMove = false;
        onConnectionLost("Move not received");
    }

    const bool graceExpired = m_disconnectGraceUntil == 0 ||
        static_cast<int32_t>(now - m_disconnectGraceUntil) >= 0;
    if (!m_disconnectShown &&
        (now - m_lastValidPeerPacketTime) > 4000 &&
        m_uiState != UIState::GameOver && graceExpired) {
        onConnectionLost();
    }
}

void ChessScene::onRemoteMoveReceived(const MoveNetMsg& msg) {
    if (m_lastAppliedRemoteSequence != 0 &&
        msg.sequence == m_lastAppliedRemoteSequence &&
        msg.preBoardHash == m_lastRemotePreHash &&
        msg.postBoardHash == m_lastRemotePostHash) {
        // ACK the position produced by the duplicated move, even if this side
        // has already played its reply and the current board has advanced.
        sendMoveAck(msg, NetAckStatus::Duplicate, msg.postBoardHash);
        return;
    }

    if (m_uiState == UIState::GameOver ||
        (m_uiState == UIState::Reviewing &&
         m_preReviewState == UIState::GameOver) ||
        msg.sequence != m_expectedRemoteSequence) {
        sendMoveAck(msg, NetAckStatus::Rejected,
                    ChessZobrist::hash(m_board));
        return;
    }

    if (m_uiState == UIState::Reviewing) {
        exitReviewMode();
    }

    const uint32_t currentHash = ChessZobrist::hash(m_board);
    if (msg.preBoardHash != currentHash ||
        m_board.sideToMove() != opponent(m_localColor) ||
        msg.fromCol > 7 || msg.fromRow > 7 || msg.toCol > 7 ||
        msg.toRow > 7 || (msg.flags & ~0x03u) != 0 ||
        !isValidMoveClockValue(m_clock.enabled(), msg.moverRemainingMs)) {
        sendMoveAck(msg, NetAckStatus::Rejected, currentHash);
        onConnectionLost("Game out of sync");
        return;
    }

    const Move move = netMsgToMove(msg);
    MoveList legal;
    ChessRules::generateLegal(m_board, legal);
    if (!legal.contains(move)) {
        sendMoveAck(msg, NetAckStatus::Rejected, currentHash);
        onConnectionLost("Invalid remote move");
        return;
    }

    const MoveRecord probe = m_board.makeMove(move);
    const uint32_t probedPostHash = ChessZobrist::hash(m_board);
    m_board.unmakeMove(probe);
    if (probedPostHash != msg.postBoardHash) {
        sendMoveAck(msg, NetAckStatus::Rejected, currentHash);
        onConnectionLost("Game out of sync");
        return;
    }

    const PieceColor mover = opponent(m_localColor);
    if (m_clock.enabled() &&
        !remoteClockRaiseAllowed(
            mover, msg.moverRemainingMs,
            saturatingAddMs(m_clock.incrementMs(), kRemoteClockSkewMs))) {
        sendMoveAck(msg, NetAckStatus::Rejected, currentHash);
        onConnectionLost("Clock out of sync");
        return;
    }

    // A valid reply proves that the peer applied our preceding move even if
    // its explicit ACK was the packet that got lost.
    m_awaitingAck = false;
    // Preserve draw-offer correlation until a delayed decline arrives. An
    // accept remains valid only while the board still has the offer hash.
    if (m_clock.enabled()) {
        m_remoteClockPending = true;
        m_remoteMoverRemainingMs = msg.moverRemainingMs;
    }
    m_applyingRemoteMove = true;
    executeMove(move);
    m_applyingRemoteMove = false;

    const uint32_t appliedHash = ChessZobrist::hash(m_board);
    if (appliedHash != msg.postBoardHash) {
        m_remoteClockPending = false;
        sendMoveAck(msg, NetAckStatus::Rejected, appliedHash);
        onConnectionLost("Game out of sync");
        return;
    }

    if (m_clock.enabled()) {
        acceptRemoteClockSample(opponent(m_localColor), msg.moverRemainingMs);
    }
    m_lastAppliedRemoteSequence = msg.sequence;
    m_lastRemotePreHash = msg.preBoardHash;
    m_lastRemotePostHash = msg.postBoardHash;
    advanceNonZero(m_expectedRemoteSequence);
    m_missingRemoteMove = false;
    sendMoveAck(msg, NetAckStatus::Accepted, appliedHash);
}

bool ChessScene::sendControl(NetControlType control, uint8_t arg0,
                             uint8_t arg1, uint16_t relatedEventId,
                             uint32_t valueMs, bool replacePending) {
    ControlNetMsg message = buildControl(control, arg0, arg1,
                                         relatedEventId, valueMs);
    if (m_controlAwaitingAck) {
        if (!replacePending) return false;
        if (m_hasQueuedControl &&
            m_queuedControl.control == NetControlType::GameEnd) {
            // A terminal event is the final word for this stream and must not
            // be overwritten by a later nonterminal action.
            return control == NetControlType::GameEnd;
        }
        m_hasQueuedControl = true;
        m_queuedControl = message;
        return true;
    }

    message.eventId = m_nextControlEventId;
    advanceNonZero(m_nextControlEventId);

    stampSessionPacket(&message, sizeof(message), m_macKey);
    m_pendingControl = message;
    m_controlAwaitingAck = true;
    m_controlRetryCount = 0;
    m_lastControlSendTime = millis();
    EspNowTransport::instance().send(
        reinterpret_cast<const uint8_t*>(&m_pendingControl),
        sizeof(m_pendingControl));
    return true;
}

void ChessScene::sendQueuedControl(bool reusePendingEventId) {
    if (!m_hasQueuedControl || m_controlAwaitingAck) return;
    const TerminalReliability::ImmutableQueuedControl queued(m_queuedControl);
    const uint16_t rejectedEventId = m_pendingControl.eventId;
    m_hasQueuedControl = false;
    m_queuedControl = ControlNetMsg{};
    ControlNetMsg message = reusePendingEventId
        ? queued.promoteReusingEventId(rejectedEventId)
        : queued.promote();
    if (!reusePendingEventId) {
        message.eventId = m_nextControlEventId;
        advanceNonZero(m_nextControlEventId);
    }
    stampSessionPacket(&message, sizeof(message), m_macKey);
    m_pendingControl = message;
    m_controlAwaitingAck = true;
    m_controlRetryCount = 0;
    m_lastControlSendTime = millis();
    EspNowTransport::instance().send(
        reinterpret_cast<const uint8_t*>(&m_pendingControl),
        sizeof(m_pendingControl));
}

ControlNetMsg ChessScene::buildControl(NetControlType control, uint8_t arg0,
                                       uint8_t arg1,
                                       uint16_t relatedEventId,
                                       uint32_t valueMs) const {
    ControlNetMsg message;
    message.header.gameId = m_networkGameId;
    message.header.sessionId = m_sessionId;
    message.control = control;
    message.arg0 = arg0;
    message.arg1 = arg1;
    message.relatedEventId = relatedEventId;
    message.valueMs = valueMs;
    const ChessClockSnapshot clock = m_clock.snapshot(millis());
    message.whiteRemainingMs = clock.whiteRemainingMs;
    message.blackRemainingMs = clock.blackRemainingMs;
    message.boardHash = ChessZobrist::hash(m_board);
    return message;
}

void ChessScene::sendControlAck(const ControlNetMsg& msg,
                                NetAckStatus status) {
    ControlAckMsg ack;
    ack.header.gameId = m_networkGameId;
    ack.header.sessionId = m_sessionId;
    ack.eventId = msg.eventId;
    ack.control = msg.control;
    ack.status = status;
    if (msg.control == NetControlType::TimeGift &&
        status != NetAckStatus::Rejected && m_clock.enabled()) {
        ack.clockRemainingMs =
            m_clock.remainingMsAt(m_localColor, millis());
    }
    ack.boardHash = ChessZobrist::hash(m_board);
    stampSessionPacket(&ack, sizeof(ack), m_macKey);
    EspNowTransport::instance().send(
        reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
}

void ChessScene::onControlReceived(const ControlNetMsg& msg) {
    if (msg.eventId == m_lastRemoteControlEventId) {
        const bool same =
            std::memcmp(&msg, &m_lastRemoteControl, sizeof(msg)) == 0;
        sendControlAck(msg, same &&
            m_lastRemoteControlStatus == NetAckStatus::Accepted
                ? NetAckStatus::Duplicate : NetAckStatus::Rejected);
        return;
    }
    if (msg.eventId != m_expectedRemoteControlEventId ||
        static_cast<uint8_t>(msg.control) <
            static_cast<uint8_t>(NetControlType::DrawOffer) ||
        static_cast<uint8_t>(msg.control) >
            static_cast<uint8_t>(NetControlType::GameEnd)) {
        sendControlAck(msg, NetAckStatus::Rejected);
        return;
    }

    const uint32_t boardHash = ChessZobrist::hash(m_board);
    bool valid = true;
    bool consumeRejectedDraw = false;
    GameOutcome remoteOutcome = GameOutcome::None;
    TerminationReason remoteTermination = TerminationReason::None;
    if (msg.control == NetControlType::DrawOffer) {
        const bool validPayload = msg.arg0 == 0 && msg.arg1 == 0 &&
            msg.relatedEventId == 0 && msg.valueMs == 0;
        valid = validPayload && m_resultOutcome == GameOutcome::None &&
                !m_controlAwaitingAck && !m_hasQueuedControl &&
                msg.boardHash == boardHash &&
                m_board.sideToMove() == m_localColor;
        consumeRejectedDraw = validPayload && !valid;
    } else if (msg.control == NetControlType::DrawAccept ||
        msg.control == NetControlType::DrawDecline) {
        valid = valid && m_localDrawOfferEventId != 0 &&
                msg.relatedEventId == m_localDrawOfferEventId &&
                msg.arg0 == 0 && msg.arg1 == 0 && msg.valueMs == 0 &&
                msg.boardHash == m_localDrawOfferBoardHash;
        if (msg.control == NetControlType::DrawAccept) {
            valid = valid && boardHash == m_localDrawOfferBoardHash;
            consumeRejectedDraw = !valid &&
                m_localDrawOfferEventId != 0 &&
                msg.relatedEventId == m_localDrawOfferEventId &&
                msg.boardHash == m_localDrawOfferBoardHash;
        }
    } else if (msg.control == NetControlType::TimeGift) {
        valid = valid && m_resultOutcome == GameOutcome::None &&
                m_clock.enabled() && msg.valueMs == 15000 &&
                msg.arg0 == static_cast<uint8_t>(m_localColor) &&
                msg.arg1 == 0 && msg.relatedEventId == 0 &&
                msg.boardHash == boardHash &&
                m_board.sideToMove() == opponent(m_localColor);
    } else if (msg.control == NetControlType::GameEnd) {
        valid = msg.valueMs == 0 &&
                fromNetResult(msg.arg0, remoteOutcome) &&
                fromNetTermination(msg.arg1, remoteTermination);
        if (valid) {
            const PieceColor sender = opponent(m_localColor);
            const GameOutcome receiverWin = m_localColor == PieceColor::White
                ? GameOutcome::WhiteWin : GameOutcome::BlackWin;
            switch (remoteTermination) {
                case TerminationReason::Checkmate: {
                    const GameOutcome boardOutcome =
                        m_board.sideToMove() == PieceColor::White
                            ? GameOutcome::BlackWin
                            : GameOutcome::WhiteWin;
                    valid = msg.relatedEventId == 0 &&
                            msg.boardHash == boardHash &&
                            ChessRules::isCheckmate(m_board) &&
                            remoteOutcome == boardOutcome;
                    break;
                }
                case TerminationReason::Stalemate:
                    valid = msg.relatedEventId == 0 &&
                            msg.boardHash == boardHash &&
                            remoteOutcome == GameOutcome::Draw &&
                            ChessRules::isStalemate(m_board);
                    break;
                case TerminationReason::Repetition:
                    valid = msg.relatedEventId == 0 &&
                            msg.boardHash == boardHash &&
                            remoteOutcome == GameOutcome::Draw &&
                            ChessRules::isThreefoldRepetition(
                                m_board, m_history, m_historyCount);
                    break;
                case TerminationReason::FiftyMove:
                    valid = msg.relatedEventId == 0 &&
                            msg.boardHash == boardHash &&
                            remoteOutcome == GameOutcome::Draw &&
                            ChessRules::isDraw50Move(m_board);
                    break;
                case TerminationReason::InsufficientMaterial:
                    valid = msg.relatedEventId == 0 &&
                            msg.boardHash == boardHash &&
                            remoteOutcome == GameOutcome::Draw &&
                            ChessRules::isInsufficientMaterial(m_board);
                    break;
                case TerminationReason::Resignation:
                    // Quit forfeits on either turn. The hash still binds the
                    // packet to this position, and the sender cannot claim a win.
                    valid = msg.relatedEventId == 0 &&
                            msg.boardHash == boardHash &&
                            remoteOutcome == receiverWin;
                    break;
                case TerminationReason::Timeout: {
                    const uint32_t senderRemaining =
                        sender == PieceColor::White
                            ? msg.whiteRemainingMs
                            : msg.blackRemainingMs;
                    valid = m_clock.enabled() && msg.relatedEventId == 0 &&
                            msg.boardHash == boardHash &&
                            remoteOutcome == timeoutOutcome(sender) &&
                            m_board.sideToMove() == sender &&
                            senderRemaining == 0;
                    break;
                }
                case TerminationReason::Agreement:
                    // The offerer sends this reliable receipt after accepting
                    // DrawAccept. It lets the accepting side finish even when
                    // the first ControlAck was lost.
                    valid = remoteOutcome == GameOutcome::Draw &&
                            msg.boardHash == boardHash &&
                            msg.relatedEventId != 0 &&
                            msg.relatedEventId ==
                                m_lastLocalDrawAcceptEventId &&
                            ((m_finishDrawOnAck &&
                              m_pendingControl.control ==
                                  NetControlType::DrawAccept &&
                              m_pendingControl.eventId ==
                                  msg.relatedEventId) ||
                             (m_resultOutcome == GameOutcome::Draw &&
                              m_termination ==
                                  TerminationReason::Agreement));
                    break;
                default:
                    // Disconnect is a local transport state and is never
                    // trusted as a remote game result.
                    valid = false;
                    break;
            }
        }
    }

    if (!valid) {
        if (consumeRejectedDraw) {
            m_lastRemoteControlEventId = msg.eventId;
            m_lastRemoteControl = msg;
            m_lastRemoteControlStatus = NetAckStatus::Rejected;
            advanceNonZero(m_expectedRemoteControlEventId);
        }
        sendControlAck(msg, NetAckStatus::Rejected);
        return;
    }

    if (msg.control == NetControlType::TimeGift) {
        // Commit before acknowledging. A lost ACK is harmless because the
        // duplicate path below reports the already-committed clock without
        // applying the gift twice.
        const uint32_t giftNow = millis();
        const uint32_t current =
            m_clock.remainingMsAt(m_localColor, giftNow);
        m_clock.setRemainingMs(
            m_localColor, saturatingAddMs(current, msg.valueMs), giftNow);
    }

    m_lastRemoteControlEventId = msg.eventId;
    m_lastRemoteControl = msg;
    m_lastRemoteControlStatus = NetAckStatus::Accepted;
    advanceNonZero(m_expectedRemoteControlEventId);
    sendControlAck(msg, NetAckStatus::Accepted);

    switch (msg.control) {
        case NetControlType::DrawOffer:
            if (m_uiState == UIState::GameOver) break;
            m_remoteDrawOfferEventId = msg.eventId;
            m_actionModal.clearButtons();
            m_actionModal.setTitle("Draw Offer");
            m_actionModal.setMessage("Opponent offers a draw.");
            m_actionModal.setEscapeCallback([this, msg]() {
                m_actionModal.hide();
                m_remoteDrawOfferEventId = 0;
                sendControl(NetControlType::DrawDecline, 0, 0,
                            msg.eventId, 0, true);
                restoreGameInputFocus();
            });
            m_actionModal.addButton("Accept", [this, msg]() {
                m_actionModal.hide();
                m_remoteDrawOfferEventId = 0;
                const uint32_t now = millis();
                m_clockPausedForDrawAccept = m_clock.pause(now);
                if (m_clock.enabled() && !m_clockPausedForDrawAccept) {
                    m_clock.update(now);
                    if (m_clock.hasFlaggedSide() &&
                        m_clock.flaggedSide() == m_localColor) {
                        finishTimeout(m_localColor);
                        return;
                    }
                }
                if (!sendControl(NetControlType::DrawAccept, 0, 0,
                                 msg.eventId, 0, true)) {
                    if (m_clockPausedForDrawAccept) {
                        m_clock.resume(millis());
                        m_clockPausedForDrawAccept = false;
                    }
                    closeActionModal();
                    return;
                }
                m_lastLocalDrawAcceptEventId = m_pendingControl.eventId;
                m_finishDrawOnAck = true;
                m_actionModal.clearButtons();
                m_actionModal.setTitle("Draw Agreement");
                m_actionModal.setMessage("Confirming with opponent...");
                m_actionModal.setEscapeCallback([]() {});
                m_actionModal.show();
                focusChain().focusWidget(&m_actionModal);
            });
            m_actionModal.addButton("Decline", [this, msg]() {
                m_actionModal.hide();
                m_remoteDrawOfferEventId = 0;
                sendControl(NetControlType::DrawDecline, 0, 0,
                            msg.eventId, 0, true);
                restoreGameInputFocus();
            });
            m_actionModal.show();
            focusChain().focusWidget(&m_actionModal);
            break;

        case NetControlType::DrawAccept:
            // The response proves the peer received our DrawOffer even if its
            // ACK was lost, so the offer no longer needs its own retry slot.
            if (m_controlAwaitingAck &&
                m_pendingControl.control == NetControlType::DrawOffer) {
                m_controlAwaitingAck = false;
                m_pendingControl = ControlNetMsg{};
            }
            m_localDrawOfferEventId = 0;
            m_localDrawOfferBoardHash = 0;
            {
                const uint32_t terminalNow = millis();
                if (m_clock.enabled()) {
                    const PieceColor sender = opponent(m_localColor);
                    const uint32_t senderRemaining =
                        sender == PieceColor::White
                            ? msg.whiteRemainingMs : msg.blackRemainingMs;
                    m_clock.setRemainingMs(sender, senderRemaining,
                                           terminalNow);
                }
                finishGame(GameOutcome::Draw,
                           TerminationReason::Agreement,
                           "Draw Agreed", "Game is a draw.", false,
                           terminalNow);
                // Reliable final receipt: if the direct ACK above is lost,
                // this independently confirms that DrawAccept was processed.
                sendGameEnd(GameOutcome::Draw,
                            TerminationReason::Agreement, msg.eventId);
            }
            break;

        case NetControlType::DrawDecline:
            if (m_controlAwaitingAck &&
                m_pendingControl.control == NetControlType::DrawOffer) {
                m_controlAwaitingAck = false;
                m_pendingControl = ControlNetMsg{};
            }
            m_localDrawOfferEventId = 0;
            m_localDrawOfferBoardHash = 0;
            if (m_uiState != UIState::GameOver) {
                m_statusBar.setRight("Draw declined");
            }
            break;

        case NetControlType::TimeGift: {
            updateStatusBar();
            m_statusBar.setRight("+15 seconds");
            break;
        }

        case NetControlType::GameEnd: {
            const uint32_t terminalNow = millis();
            const bool alreadyFinished =
                m_resultOutcome != GameOutcome::None;
            if (remoteTermination == TerminationReason::Agreement &&
                m_finishDrawOnAck &&
                m_pendingControl.control == NetControlType::DrawAccept &&
                m_pendingControl.eventId == msg.relatedEventId) {
                m_controlAwaitingAck = false;
                m_pendingControl = ControlNetMsg{};
                m_finishDrawOnAck = false;
                m_clockPausedForDrawAccept = false;
            }
            if (!alreadyFinished && m_clock.enabled()) {
                const PieceColor sender = opponent(m_localColor);
                const uint32_t senderRemaining =
                    sender == PieceColor::White
                        ? msg.whiteRemainingMs : msg.blackRemainingMs;
                m_clock.setRemainingMs(sender, senderRemaining, terminalNow);
            }
            const char* message = remoteOutcome == GameOutcome::Draw
                ? "Game is a draw."
                : (remoteOutcome == GameOutcome::WhiteWin
                       ? "White wins!" : "Black wins!");
            if (!alreadyFinished) {
                finishGame(remoteOutcome, remoteTermination,
                           terminalTitle(remoteTermination), message, false,
                           terminalNow);
            }
            break;
        }
    }
}

void ChessScene::offerDraw() {
    if (m_board.sideToMove() == m_localColor || m_awaitingAck) {
        m_statusBar.setRight("Offer after move");
        return;
    }
    if (m_localDrawOfferEventId != 0 || m_controlAwaitingAck ||
        m_actionModal.isVisible()) {
        m_statusBar.setRight("Offer pending");
        return;
    }
    m_actionModal.clearButtons();
    m_actionModal.setTitle("Offer Draw?");
    m_actionModal.setMessage("Send a draw offer to your opponent?");
    m_actionModal.setEscapeCallback([this]() { closeActionModal(); });
    m_actionModal.addButton("Offer", [this]() {
        m_actionModal.hide();
        if (sendControl(NetControlType::DrawOffer)) {
            m_localDrawOfferEventId = m_pendingControl.eventId;
            m_localDrawOfferBoardHash = m_pendingControl.boardHash;
            m_statusBar.setRight("Draw offered");
        }
        focusChain().focusWidget(&m_boardGrid);
    });
    m_actionModal.addButton("Cancel", [this]() { closeActionModal(); });
    m_actionModal.show();
    focusChain().focusWidget(&m_actionModal);
}

void ChessScene::giveOpponentTime() {
    if (m_board.sideToMove() != m_localColor || m_awaitingAck) {
        m_statusBar.setRight("Give time on your turn");
        return;
    }
    if (m_localDrawOfferEventId != 0) {
        m_statusBar.setRight("Resolve draw first");
        return;
    }
    if (m_controlAwaitingAck || m_actionModal.isVisible()) {
        m_statusBar.setRight("Action pending");
        return;
    }
    m_actionModal.clearButtons();
    m_actionModal.setTitle("Give +15 Seconds?");
    m_actionModal.setMessage("This cannot be undone.");
    m_actionModal.setEscapeCallback([this]() { closeActionModal(); });
    m_actionModal.addButton("Give", [this]() {
        m_actionModal.hide();
        const PieceColor recipient = opponent(m_localColor);
        if (sendControl(NetControlType::TimeGift,
                        static_cast<uint8_t>(recipient), 0, 0, 15000)) {
            m_timeGiftRecipient = recipient;
            m_timeGiftPending = true;
            m_statusBar.setRight("Confirming +15 sec");
        } else {
            m_statusBar.setRight("Could not send gift");
        }
        focusChain().focusWidget(&m_boardGrid);
    });
    m_actionModal.addButton("Cancel", [this]() { closeActionModal(); });
    m_actionModal.show();
    focusChain().focusWidget(&m_actionModal);
}

void ChessScene::confirmResign() {
    if (m_board.sideToMove() != m_localColor || m_awaitingAck ||
        m_controlAwaitingAck) {
        m_statusBar.setRight("Resign on your turn");
        return;
    }
    if (m_actionModal.isVisible()) return;
    m_actionModal.clearButtons();
    m_actionModal.setTitle("Resign?");
    m_actionModal.setMessage("Give up this game?");
    m_actionModal.setEscapeCallback([this]() { closeActionModal(); });
    m_actionModal.addButton("Resign", [this]() {
        m_actionModal.hide();
        const GameOutcome outcome = m_localColor == PieceColor::White
            ? GameOutcome::BlackWin : GameOutcome::WhiteWin;
        finishGame(outcome, TerminationReason::Resignation, "Resigned",
                   outcome == GameOutcome::WhiteWin
                       ? "White wins!" : "Black wins!", true);
    });
    m_actionModal.addButton("Cancel", [this]() { closeActionModal(); });
    m_actionModal.show();
    focusChain().focusWidget(&m_actionModal);
}

void ChessScene::sendGameEnd(GameOutcome outcome,
                             TerminationReason termination,
                             uint16_t relatedEventId) {
    sendControl(NetControlType::GameEnd,
                static_cast<uint8_t>(toNetResult(outcome)),
                static_cast<uint8_t>(toNetTermination(termination)),
                relatedEventId, 0, true);
}

bool ChessScene::localMoveBlockedByControl() const {
    return m_netMode == NetworkMode::Online && m_timeGiftPending;
}

GameOutcome ChessScene::timeoutOutcome(PieceColor flagged) const {
    if (ChessRules::hasInsufficientMatingMaterial(m_board, opponent(flagged)))
        return GameOutcome::Draw;
    return flagged == PieceColor::White ? GameOutcome::BlackWin : GameOutcome::WhiteWin;
}

void ChessScene::finishTimeout(PieceColor flagged) {
    const GameOutcome outcome = timeoutOutcome(flagged);
    const char* message = outcome == GameOutcome::Draw ? "Draw: no mating material."
        : outcome == GameOutcome::WhiteWin ? "White wins!" : "Black wins!";
    finishGame(outcome, TerminationReason::Timeout, "Time's Up!", message, true);
}

void ChessScene::finishDeferredTimeoutIfReady() {
    if (!m_deferredLocalTimeout || m_timeGiftPending ||
        m_resultOutcome != GameOutcome::None) {
        return;
    }

    const PieceColor flagged = m_deferredFlaggedSide;
    m_deferredLocalTimeout = false;
    finishTimeout(flagged);
}

void ChessScene::onConnectionLost(const char* message) {
    const bool reviewingFinishedGame =
        m_uiState == UIState::Reviewing && m_preReviewState == UIState::GameOver;
    if (m_disconnectShown || m_uiState == UIState::GameOver ||
        reviewingFinishedGame) {
        return;
    }
    m_disconnectShown = true;

    // A disconnect overlay replaces the draw-offer modal. Reliably decline
    // that interrupted offer so the peer cannot remain permanently stuck in
    // an acknowledged-but-unanswered offer state after recovery.
    if (m_remoteDrawOfferEventId != 0) {
        const uint16_t offerEventId = m_remoteDrawOfferEventId;
        m_remoteDrawOfferEventId = 0;
        sendControl(NetControlType::DrawDecline, 0, 0,
                    offerEventId, 0, true);
    }

    m_actionModal.clearButtons();
    m_actionModal.setTitle("Disconnected");
    m_actionModal.setMessage(message ? message : "Lost connection");

    auto waitForConnection = [this]() {
        m_actionModal.hide();
        m_disconnectShown = false;
        m_disconnectGraceUntil = millis() + 5000; // 5s grace before re-checking
        if (m_lastSentSeq != 0 &&
            m_board.sideToMove() == opponent(m_localColor) &&
            ChessZobrist::hash(m_board) == m_lastSentPostHash) {
            m_awaitingAck = true;
            m_moveRetryCount = 0;
            m_lastMoveSendTime = millis();
        }
        if (m_pendingControl.eventId != 0) {
            m_controlAwaitingAck = true;
            m_controlRetryCount = 0;
            m_lastControlSendTime = millis();
        }
        if (m_finishDrawOnAck) {
            m_actionModal.clearButtons();
            m_actionModal.setTitle("Draw Agreement");
            m_actionModal.setMessage("Confirming with opponent...");
            m_actionModal.setEscapeCallback([]() {});
            m_actionModal.show();
            focusChain().focusWidget(&m_actionModal);
        } else {
            restoreGameInputFocus();
        }
    };
    m_actionModal.setEscapeCallback(waitForConnection);
    m_actionModal.addButton("Wait", waitForConnection);
    m_actionModal.addButton("Quit", [this]() {
        quitUnfinishedOnlineGame();
    });

    m_actionModal.show();
    focusChain().focusWidget(&m_actionModal);
}

void ChessScene::restoreGameInputFocus() {
    if (m_uiState == UIState::PromotionPending) {
        if (!m_promotionModal.isVisible()) m_promotionModal.show();
        focusChain().focusWidget(&m_promotionModal);
    } else {
        focusChain().focusWidget(&m_boardGrid);
    }
}

void ChessScene::noteValidPeerPacket(uint32_t now) {
    m_lastValidPeerPacketTime = now;
    if (!m_disconnectShown) return;

    // A normal control gives up its fast retry loop when the disconnect
    // prompt is raised but deliberately retains the packet. Recovery must
    // re-arm it before dismissing the prompt; this is especially important
    // for an auto-declined draw offer whose source modal no longer exists.
    if (!m_controlAwaitingAck && m_pendingControl.eventId != 0) {
        m_controlAwaitingAck = true;
        m_controlRetryCount = 0;
        m_lastControlSendTime = now;
    }

    m_disconnectShown = false;
    m_actionModal.hide();
    if (m_finishDrawOnAck) {
        // The clock remains deliberately paused while the terminal draw
        // acknowledgement is retried. Do not expose an apparently playable
        // board merely because another valid packet restored liveness.
        m_actionModal.clearButtons();
        m_actionModal.setTitle("Draw Agreement");
        m_actionModal.setMessage("Confirming with opponent...");
        m_actionModal.setEscapeCallback([]() {});
        m_actionModal.show();
        focusChain().focusWidget(&m_actionModal);
    } else {
        restoreGameInputFocus();
    }
}
