#include <functional>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <limits>
#include <set>
#include <cstring>
#include "check.h"
// Inspect state transitions through actual routed input and clock ticks.
#define private public
#include "chess_scene.h"
#undef private
#include "M5Cardputer.h"
#include "chess_zobrist.h"
#include "chess_storage_codec.h"
#include "nvs_blob_store.h"

void key(ChessScene& scene, uint8_t value) {
    InputEvent event; event.type = EventType::KeyDown; event.key = value;
    scene.routeInput(event);
}
void play(ChessScene& scene, const Move& move) {
    scene.m_boardGrid.setCursor(scene.toGridCol(move.from.col), scene.toGridRow(move.from.row));
    key(scene, Key::ENTER);
    scene.m_boardGrid.setCursor(scene.toGridCol(move.to.col), scene.toGridRow(move.to.row));
    key(scene, Key::ENTER);
}
void tick(ChessScene& scene, uint32_t dt) { auditNow += dt; scene.onTick(dt); }
void clearBoard(ChessBoard& board) {
    for (int r = 0; r < 8; ++r) for (int c = 0; c < 8; ++c) board.set(c, r, Piece{});
    board.setCastleRights(0); board.setEnPassantTarget(NO_SQUARE);
}
void keyboard() {
    InputManager input; input.init();
    M5Cardputer.Keyboard.state.word = {';'};
    input.poll(); CHECK(input.nextEvent().type == EventType::KeyDown);
    auditNow += 500; input.poll(); CHECK(input.nextEvent().type == EventType::KeyRepeat);
    M5Cardputer.Keyboard.state.word = {'/'};
    input.poll(); CHECK(input.isKeyDown('/')); CHECK(!input.isKeyDown(';'));
    CHECK(input.nextEvent().type == EventType::KeyDown);
    CHECK(input.nextEvent().type == EventType::KeyUp);
    M5Cardputer.Keyboard.state.fn = true;
    input.poll(); CHECK(input.isKeyDown(Key::RIGHT)); CHECK(!input.isKeyDown('/'));
    input.clearEvents(); auditNow += 500; input.poll();
    auto repeat = input.nextEvent(); CHECK(repeat.type == EventType::KeyRepeat);
    CHECK(repeat.modifiers & Mod::FN);
    M5Cardputer.Keyboard.state = {};
    input.poll(); input.clearEvents();
    M5Cardputer.BtnA.pressed = true; input.poll();
    CHECK(input.nextEvent().type == EventType::KeyDown);
    auditNow += 50; input.poll(); CHECK(input.isKeyDown(Key::ESCAPE)); CHECK(!input.hasEvents());
    M5Cardputer.BtnA.pressed = false; input.poll(); CHECK(input.nextEvent().type == EventType::KeyUp);
}
void canvas() {
    Canvas canvas; CHECK(canvas.create(8, 8, false)); canvas.fill(0);
    for (int origin : {-32768, -9, -1, 0, 7, 8, 9, 32767}) {
        for (unsigned length : {0u, 1u, 8u, 65535u}) {
            canvas.drawHLine(origin, 0, length, 0xffff);
            canvas.drawVLine(0, origin, length, 0xffff);
        }
    }
    canvas.fill(0); canvas.clearDirty();
    canvas.drawHLine(9, 0, 1, 0xffff); canvas.drawVLine(0, 9, 1, 0xffff);
    for (unsigned i = 0; i < 64; ++i) CHECK(canvas.buffer()[i] == 0);
    canvas.drawHLine(-2, 1, 5, 7); canvas.drawVLine(4, 6, 8, 9);
    CHECK(canvas.getPixel(0, 1) == 7 && canvas.getPixel(2, 1) == 7 && canvas.getPixel(3, 1) == 0);
    CHECK(canvas.getPixel(4, 6) == 9 && canvas.getPixel(4, 7) == 9);
}
void puzzle() {
    ChessScene scene; scene.setup(); scene.setPuzzleMode(12); scene.onEnter();
    play(scene, scene.m_puzzleSolution[0]);
    CHECK(scene.m_puzzleSolutionStep == 1 && scene.m_puzzleAutoPlayPending);
    scene.updateStatusBar();
    CHECK(std::strcmp(scene.m_statusBar.m_left, "Mate in 2") == 0);
    CHECK(std::strstr(scene.m_statusBar.m_center, "#13") != nullptr);
    play(scene, scene.m_puzzleSolution[1]); // During animation.
    CHECK(scene.m_puzzleSolutionStep == 1);
    tick(scene, 250);
    play(scene, scene.m_puzzleSolution[1]); // After animation, before auto reply.
    CHECK(scene.m_puzzleSolutionStep == 1);
    tick(scene, 500);
    CHECK(scene.m_puzzleSolutionStep == 2 && !scene.m_puzzleAutoPlayPending);
    tick(scene, 500);
    CHECK(scene.m_puzzleSolutionStep == 2);
    CHECK(!PuzzleStorage::isPuzzleCompleted(scene.m_puzzleProgress, 12));
    MoveList legal; ChessRules::generateLegal(scene.m_board, legal);
    bool triedWrong = false;
    for (unsigned i = 0; i < legal.count; ++i) {
        auto board = scene.m_board; board.makeMove(legal.moves[i]);
        if (!ChessRules::isCheckmate(board)) {
            play(scene, legal.moves[i]); triedWrong = true; break;
        }
    }
    CHECK(triedWrong && scene.m_puzzleSolutionStep == 2);
    tick(scene, 30);
    CHECK(std::strcmp(scene.m_statusBar.m_right, "Try again") == 0);
    CHECK(std::strstr(scene.m_moveList.getItem(0), "Rxd8+") != nullptr);
    CHECK(std::strstr(scene.m_moveList.getItem(0), "Kh7") != nullptr);
    play(scene, scene.m_puzzleSolution[2]);
    CHECK(PuzzleStorage::isPuzzleCompleted(scene.m_puzzleProgress, 12));
}
void castling() {
    for (PieceColor color : {PieceColor::White, PieceColor::Black}) {
        const uint8_t row = color == PieceColor::White ? 0 : 7;
        for (uint8_t kingCol : {3, 5}) {
            const uint8_t destination = kingCol == 3 ? 2 : 6;
            for (bool castle : {false, true}) {
                ChessScene scene; scene.setup(); scene.onEnter(); clearBoard(scene.m_board);
                scene.m_board.set(kingCol, row, Piece(PieceType::King, color));
                scene.m_board.set(0, row, Piece(PieceType::Rook, color));
                scene.m_board.set(7, row, Piece(PieceType::Rook, color));
                scene.m_board.set(4, 7 - row, Piece(PieceType::King, opponent(color)));
                scene.m_board.setSideToMove(color);
                scene.m_board.setVariant(ChessVariant::Chess960);
                scene.m_board.setInitialColumns(kingCol, 7, 0);
                scene.m_board.setCastleRights(color == PieceColor::White
                    ? CastleRights::WhiteAll : CastleRights::BlackAll);
                Move move; move.from = {kingCol,row}; move.to = {destination,row}; play(scene, move);
                CHECK(scene.m_actionModal.isVisible() && scene.m_historyCount == 0);
                // A stale board focus cannot execute a move behind the dialog.
                scene.onCellAction(scene.toGridCol(destination), scene.toGridRow(row));
                CHECK(scene.m_historyCount == 0);
                key(scene, Key::ESCAPE); CHECK(scene.m_historyCount == 0);
                scene.m_boardGrid.setCursor(scene.toGridCol(destination), scene.toGridRow(row));
                key(scene, Key::ENTER);
                if (castle) key(scene, Key::DOWN);
                key(scene, Key::ENTER);
                CHECK(scene.m_historyCount == 1 && scene.m_history[0].move.isCastle == castle);
                const uint8_t rookCol = castle ? kingCol : kingCol == 3 ? 0 : 7;
                CHECK(scene.m_board.at(rookCol, row).type == PieceType::Rook);
            }
        }
    }
}
void legacyResume() {
    ChessScene scene; scene.setup(); scene.onEnter();
    scene.m_participants.valid = true; scene.m_participants.gameId = 123;
    scene.m_participants.mode = GameMode::Local;
    std::strcpy(scene.m_participants.whiteName, "Alice"); std::strcpy(scene.m_participants.blackName, "Bob");
    Move e4; e4.from = {4,1}; e4.to = {4,3}; play(scene, e4);
    NvsBlobStore::Blob blob; NvsBlobStore::read("chess", "game", 10000, blob); CHECK(blob.size == 151);
    uint8_t legacy[92]; std::memcpy(legacy, blob.data, 78); legacy[0] = 1;
    std::memcpy(legacy + 78, blob.data + 131, 14);
    NvsBlobStore::write("chess", "game", legacy, sizeof(legacy));
    CHECK(ChessStorage::probe().status == ChessStorage::LoadStatus::Loaded);
    ChessScene restored; restored.setup(); CHECK(restored.loadSavedGame());
    CHECK(restored.m_history[0].movedPiece.type == PieceType::Pawn);
    CHECK(ChessStorage::probe().sourceVersion == ChessStorageCodec::CURRENT_VERSION);
    restored.undoLastMove(); CHECK(restored.m_board.at(4,1).type == PieceType::Pawn);
}
void timeout() {
    for (bool canMate : {false, true}) {
        ChessScene scene; scene.setup(); clearBoard(scene.m_board);
        scene.m_board.set(0,0,Piece(PieceType::King,PieceColor::White));
        scene.m_board.set(7,7,Piece(PieceType::King,PieceColor::Black));
        scene.m_board.set(7,6,Piece(PieceType::Queen,PieceColor::Black));
        if (canMate) scene.m_board.set(1,1,Piece(PieceType::Rook,PieceColor::White));
        scene.m_board.setSideToMove(PieceColor::Black); scene.m_timeControl = TimeControl::Bullet1;
        ChessClockSnapshot clock; clock.timeControl = TimeControl::Bullet1;
        clock.whiteRemainingMs = 60000; clock.blackRemainingMs = 1;
        clock.started = true; clock.activeSide = PieceColor::Black;
        scene.m_clock.loadSnapshot(clock, auditNow);
        tick(scene, 10);
        const auto expected = canMate ? GameOutcome::WhiteWin : GameOutcome::Draw;
        CHECK(scene.m_resultOutcome == expected && scene.m_termination == TerminationReason::Timeout);
        ChessScene receiver; receiver.setup(); receiver.m_board = scene.m_board;
        receiver.m_netMode = ChessScene::NetworkMode::Online; receiver.m_localColor = PieceColor::White;
        receiver.m_timeControl = TimeControl::Bullet1; receiver.m_clock.configure(TimeControl::Bullet1);
        ControlNetMsg message; message.eventId = 1; message.control = NetControlType::GameEnd;
        message.arg0 = static_cast<uint8_t>(canMate ? NetGameResult::WhiteWin : NetGameResult::Draw);
        message.arg1 = static_cast<uint8_t>(NetTermination::Timeout);
        message.boardHash = ChessZobrist::hash(receiver.m_board); message.whiteRemainingMs = 60000;
        receiver.m_clock.configure(TimeControl::None);
        receiver.onControlReceived(message);
        CHECK(receiver.m_resultOutcome == GameOutcome::None);
        receiver.m_clock.configure(TimeControl::Bullet1);
        ControlNetMsg wrong = message;
        wrong.arg0 = static_cast<uint8_t>(canMate ? NetGameResult::Draw : NetGameResult::WhiteWin);
        receiver.onControlReceived(wrong);
        CHECK(receiver.m_resultOutcome == GameOutcome::None);
        receiver.onControlReceived(message);
        CHECK(receiver.m_resultOutcome == expected);
        CHECK(receiver.m_lastRemoteControlStatus == NetAckStatus::Accepted);
    }
}
void longGame() {
    ChessScene scene; scene.setup();
    scene.m_netMode = ChessScene::NetworkMode::Online;
    scene.m_applyingRemoteMove = true; // Exercise real move/rule/UI path without radio sends.
    std::vector<MoveRecord> all;
    std::set<uint32_t> seen{ChessZobrist::repetitionHash(scene.m_board)};
    uint32_t random = 7;
    for (unsigned ply = 0; ply < 260; ++ply) {
        MoveList legal; ChessRules::generateLegal(scene.m_board, legal);
        CHECK(legal.count > 0);
        random = random * 1664525u + 1013904223u;
        bool found = false;
        for (unsigned pass = 0; pass < 2 && !found; ++pass) {
            for (unsigned j = 0; j < legal.count; ++j) {
                auto move = legal.moves[(j + random % legal.count) % legal.count];
                auto board = scene.m_board;
                bool pawn = board.at(move.from.col, move.from.row).type == PieceType::Pawn;
                if (!pass && (pawn != (board.halfmoveClock() >= 60) ||
                    !board.at(move.to.col, move.to.row).empty())) continue;
                const auto record = board.makeMove(move);
                auto hash = ChessZobrist::repetitionHash(board);
                if (seen.count(hash) || ChessRules::isDraw50Move(board) ||
                    ChessRules::isCheckmate(board) || ChessRules::isStalemate(board) ||
                    ChessRules::isInsufficientMaterial(board)) continue;
                seen.insert(hash); all.push_back(record);
                scene.executeMove(move);
                CHECK(scene.m_resultOutcome == GameOutcome::None);
                found = true; break;
            }
        }
        CHECK(found);
    }
    CHECK(scene.m_historyOverflow && scene.m_historyCount == 250);
    CHECK(scene.m_moveList.itemCount() <= 126);
    for (unsigned i = 0; i < 250; ++i) CHECK(scene.m_history[i].move == all[i + 10].move);

    // Locate a reversible four-ply cycle and repeat it twice from this late board.
    Move cycle[4]; bool found = false;
    const auto originalHash = ChessZobrist::repetitionHash(scene.m_board);
    MoveList first; ChessRules::generateLegal(scene.m_board, first);
    for (unsigned i = 0; i < first.count && !found; ++i) {
        auto board = scene.m_board;
        auto a = first.moves[i];
        if (a.isCastle || board.at(a.from.col,a.from.row).type == PieceType::Pawn ||
            !board.at(a.to.col,a.to.row).empty()) continue;
        board.makeMove(a);
        MoveList second; ChessRules::generateLegal(board, second);
        for (unsigned j = 0; j < second.count && !found; ++j) {
            auto b = second.moves[j]; auto candidate = board;
            if (b.isCastle || candidate.at(b.from.col,b.from.row).type == PieceType::Pawn ||
                !candidate.at(b.to.col,b.to.row).empty()) continue;
            candidate.makeMove(b);
            Move reverseA; reverseA.from = a.to; reverseA.to = a.from;
            Move reverseB; reverseB.from = b.to; reverseB.to = b.from;
            MoveList reply; ChessRules::generateLegal(candidate, reply);
            if (!reply.contains(reverseA)) continue;
            candidate.makeMove(reverseA); ChessRules::generateLegal(candidate, reply);
            if (!reply.contains(reverseB)) continue;
            candidate.makeMove(reverseB);
            if (ChessZobrist::repetitionHash(candidate) != originalHash ||
                candidate.halfmoveClock() >= 92) continue;
            cycle[0] = a; cycle[1] = b; cycle[2] = reverseA; cycle[3] = reverseB; found = true;
        }
    }
    CHECK(found);
    for (unsigned i = 0; i < 8 && scene.m_resultOutcome == GameOutcome::None; ++i)
        scene.executeMove(cycle[i % 4]);
    CHECK(scene.m_resultOutcome == GameOutcome::Draw && scene.m_termination == TerminationReason::Repetition);
}

int main() {
    keyboard(); canvas(); puzzle(); castling(); legacyResume(); timeout(); longGame();
    std::puts("Scene regressions passed (keyboard, clipping, puzzles, Chess960, legacy resume, local/remote timeout, 260-ply game and repetition).");
}
