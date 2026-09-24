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
#include "lobby_scene.h"
#undef private
#include "net_crypto.h"
#include "esp_now_transport.h"
#include "profile_storage.h"
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
void puzzleCategory() {
    LobbyScene lobby;
    PuzzleProgress progress{};
    for (PuzzleType type : {PuzzleType::MateIn1, PuzzleType::MateIn2, PuzzleType::Tactic}) {
        uint16_t count = puzzleCountByType(type);
        CHECK(count > 1);
        CHECK(lobby.firstUnsolvedPuzzleIndex(type, progress) == puzzleIndexByType(type, 0));
        PuzzleStorage::markPuzzleCompleted(progress, (uint8_t)puzzleIndexByType(type, 0));
        CHECK(lobby.firstUnsolvedPuzzleIndex(type, progress) == puzzleIndexByType(type, 1));
        for (uint16_t n = 1; n < count; ++n) {
            PuzzleStorage::markPuzzleCompleted(progress, (uint8_t)puzzleIndexByType(type, n));
        }
        CHECK(lobby.firstUnsolvedPuzzleIndex(type, progress) == puzzleIndexByType(type, 0));
    }
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
void runningClock() {
    ChessScene scene; scene.setup();
    scene.setTimeControl(TimeControl::Bullet1);
    ActiveGameParticipants players;
    players.valid = true; players.gameId = 77; players.mode = GameMode::Local;
    std::strcpy(players.whiteName, "Alice"); std::strcpy(players.blackName, "Bob");
    scene.setParticipants(players);
    scene.onEnter();
    CHECK(scene.m_clock.enabled() && scene.m_clock.isRunning() && !scene.m_gameSaveDirty);
    ChessScene initial; initial.setup(); CHECK(initial.loadSavedGame());
    const uint32_t full = initial.m_clock.initialMs();
    CHECK(initial.m_participants.gameId == 77 &&
          initial.m_board.sideToMove() == PieceColor::White &&
          initial.m_clock.remainingMs(PieceColor::White) == full &&
          initial.m_clock.remainingMs(PieceColor::Black) == full);
    tick(scene, 6000);
    ChessScene resumed; resumed.setup(); CHECK(resumed.loadSavedGame());
    const uint32_t white = resumed.m_clock.remainingMs(PieceColor::White);
    CHECK(resumed.m_participants.gameId == 77 &&
          resumed.m_board.sideToMove() == PieceColor::White &&
          white < full && white == full - 6000 &&
          resumed.m_clock.remainingMs(PieceColor::Black) == full);
}

extern std::vector<std::vector<uint8_t>> auditSent, auditIncoming;

const uint8_t kAliceSecret[32] = {
    0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d, 0x3c, 0x16, 0xc1,
    0x72, 0x51, 0xb2, 0x66, 0x45, 0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0,
    0x99, 0x2a, 0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a,
};
const uint8_t kAlicePublic[32] = {
    0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54, 0x74, 0x8b, 0x7d,
    0xdc, 0xb4, 0x3e, 0xf7, 0x5a, 0x0d, 0xbf, 0x3a, 0x0d, 0x26, 0x38,
    0x1a, 0xf4, 0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b, 0x4e, 0x6a,
};
const uint8_t kBobSecret[32] = {
    0x5d, 0xab, 0x08, 0x7e, 0x62, 0x4a, 0x8a, 0x4b, 0x79, 0xe1, 0x7f,
    0x8b, 0x83, 0x80, 0x0e, 0xe6, 0x6f, 0x3b, 0xb1, 0x29, 0x26, 0x18,
    0xb6, 0xfd, 0x1c, 0x2f, 0x8b, 0x27, 0xff, 0x88, 0xe0, 0xeb,
};
const uint8_t kBobPublic[32] = {
    0xde, 0x9e, 0xdb, 0x7d, 0x7b, 0x7d, 0xc1, 0xb4, 0xd3, 0x5b, 0x61,
    0xc2, 0xec, 0xe4, 0x35, 0x37, 0x3f, 0x83, 0x43, 0xc8, 0x5b, 0x78,
    0x67, 0x4d, 0xad, 0xfc, 0x7e, 0x14, 0x6f, 0x88, 0x2b, 0x4f,
};

void testMacKey(uint8_t macKey[32]) {
    uint8_t shared[32];
    CHECK(NetCrypto::sharedSecret(kAliceSecret, kBobPublic, shared));
    NetCrypto::deriveMacKey(shared, macKey);
}

void injectPacket(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    auditIncoming.emplace_back(bytes, bytes + size);
}

void drainScenes() {
    while (!CardGFX::scenes().empty()) CardGFX::scenes().pop();
}

void prepareOnline(ChessScene& scene, PieceColor local, const uint8_t macKey[32]) {
    const uint8_t mac[6] = {0x02, 0x10, 0x20, 0x30, 0x40, 0x50};
    EspNowTransport::instance().init();
    EspNowTransport::instance().addPeer(mac);
    scene.setNetworkMode(local, 0x89abcdefu, 0x1234, mac, "Ada", "Bob",
                         false, macKey);
    scene.m_lastHeartbeatSendTime = auditNow;
    scene.m_lastMoveSendTime = auditNow;
    scene.m_lastValidPeerPacketTime = auditNow;
    auditSent.clear();
    auditIncoming.clear();
}

bool sentTagged(NetMsgType type, size_t size, const uint8_t macKey[32]) {
    for (const auto& packet : auditSent) {
        if (packet.size() != size || packet.empty() ||
            static_cast<NetMsgType>(packet[0]) != type) {
            continue;
        }
        if (sessionPacketTagMatches(packet.data(), packet.size(), macKey)) return true;
    }
    return false;
}

void authenticatedPackets() {
    uint8_t macKey[32];
    testMacKey(macKey);
    ChessScene scene;
    scene.setup();
    scene.setTimeControl(TimeControl::Bullet1);
    prepareOnline(scene, PieceColor::White, macKey);
    scene.m_board.setSideToMove(PieceColor::Black);
    const uint32_t boardHash = ChessZobrist::hash(scene.m_board);
    const uint32_t blackTime = scene.m_clock.remainingMs(PieceColor::Black);
    scene.m_lastValidPeerPacketTime = auditNow - 1000;

    HeartbeatMsg heartbeat;
    heartbeat.header.gameId = scene.m_networkGameId;
    heartbeat.header.sessionId = scene.m_sessionId;
    heartbeat.activeColor = static_cast<uint8_t>(PieceColor::Black);
    heartbeat.activeRemainingMs = blackTime + 50000;
    heartbeat.positionEpoch = scene.m_positionEpoch;
    heartbeat.boardHash = boardHash;
    stampSessionPacket(&heartbeat, sizeof(heartbeat), macKey);
    reinterpret_cast<uint8_t*>(&heartbeat)[sizeof(heartbeat) - 1] ^= 0x01u;

    MoveNetMsg move;
    move.header.gameId = scene.m_networkGameId;
    move.header.sessionId = scene.m_sessionId;
    move.sequence = 1;
    move.fromCol = 4;
    move.fromRow = 6;
    move.toCol = 4;
    move.toRow = 4;
    move.moverRemainingMs = 1;
    move.preBoardHash = boardHash;
    stampSessionPacket(&move, sizeof(move), macKey);
    reinterpret_cast<uint8_t*>(&move)[sizeof(move) - 1] ^= 0x01u;

    injectPacket(&heartbeat, sizeof(heartbeat));
    injectPacket(&move, sizeof(move));
    scene.pollNetwork();
    CHECK(ChessZobrist::hash(scene.m_board) == boardHash);
    CHECK(scene.m_historyCount == 0);
    CHECK(scene.m_clock.remainingMs(PieceColor::Black) == blackTime);
    CHECK(scene.m_clock.remainingMs(PieceColor::White) == blackTime);
    CHECK(!scene.m_disconnectShown);
    CHECK(scene.m_resultOutcome == GameOutcome::None);
    CHECK(scene.m_netMode == ChessScene::NetworkMode::Online);
    CHECK(scene.m_lastValidPeerPacketTime == auditNow - 1000);
}

void resignationAuthentication() {
    uint8_t macKey[32];
    testMacKey(macKey);
    ChessScene scene;
    scene.setup();
    prepareOnline(scene, PieceColor::White, macKey);

    auto deliver = [&](uint32_t boardHash) {
        auditSent.clear();
        auditIncoming.clear();
        ControlNetMsg message;
        message.header.gameId = scene.m_networkGameId;
        message.header.sessionId = scene.m_sessionId;
        message.eventId = scene.m_expectedRemoteControlEventId;
        message.control = NetControlType::GameEnd;
        message.arg0 = static_cast<uint8_t>(NetGameResult::WhiteWin);
        message.arg1 = static_cast<uint8_t>(NetTermination::Resignation);
        message.relatedEventId = 0;
        message.boardHash = boardHash;
        stampSessionPacket(&message, sizeof(message), macKey);
        injectPacket(&message, sizeof(message));
        scene.pollNetwork();
    };

    scene.m_board.setSideToMove(PieceColor::Black);
    deliver(ChessZobrist::hash(scene.m_board) ^ 0x11111111u);
    CHECK(scene.m_resultOutcome == GameOutcome::None);
    CHECK(sentTagged(NetMsgType::ControlAck, sizeof(ControlAckMsg), macKey));
    ControlAckMsg rejected{};
    bool sawReject = false;
    for (const auto& packet : auditSent) {
        if (packet.size() != sizeof(ControlAckMsg)) continue;
        std::memcpy(&rejected, packet.data(), sizeof(rejected));
        if (rejected.status == NetAckStatus::Rejected) sawReject = true;
    }
    CHECK(sawReject);

    // White to move means the resigning peer is not the side to move.
    // An explicit quit still has to be accepted.
    scene.m_board.setSideToMove(PieceColor::White);
    deliver(ChessZobrist::hash(scene.m_board));
    CHECK(scene.m_resultOutcome == GameOutcome::WhiteWin);
    CHECK(scene.m_termination == TerminationReason::Resignation);
    CHECK(scene.m_lastRemoteControlStatus == NetAckStatus::Accepted);
}

void remoteClockBound() {
    uint8_t macKey[32];
    testMacKey(macKey);
    ChessScene scene;
    scene.setup();
    scene.setTimeControl(TimeControl::Bullet1);
    prepareOnline(scene, PieceColor::White, macKey);
    scene.m_board.setSideToMove(PieceColor::Black);
    const uint32_t before = scene.m_clock.remainingMs(PieceColor::Black);
    scene.m_hasRemoteClockBaseline[static_cast<uint8_t>(PieceColor::Black)] = true;
    scene.m_remoteClockBaselineMs[static_cast<uint8_t>(PieceColor::Black)] = before;
    scene.m_lastValidPeerPacketTime = auditNow - 1000;

    HeartbeatMsg heartbeat;
    heartbeat.header.gameId = scene.m_networkGameId;
    heartbeat.header.sessionId = scene.m_sessionId;
    heartbeat.activeColor = static_cast<uint8_t>(PieceColor::Black);
    heartbeat.activeRemainingMs = before + 201;
    heartbeat.positionEpoch = scene.m_positionEpoch;
    heartbeat.lastAppliedSequence = scene.m_lastAppliedRemoteSequence;
    heartbeat.boardHash = ChessZobrist::hash(scene.m_board);
    stampSessionPacket(&heartbeat, sizeof(heartbeat), macKey);
    injectPacket(&heartbeat, sizeof(heartbeat));
    scene.pollNetwork();
    CHECK(scene.m_clock.remainingMs(PieceColor::Black) == before);
    CHECK(scene.m_clock.remainingMs(PieceColor::White) == before);
    CHECK(!scene.m_disconnectShown);
    CHECK(scene.m_resultOutcome == GameOutcome::None);
    CHECK(scene.m_lastValidPeerPacketTime == auditNow);
}

void onlineQuitResignation() {
    uint8_t macKey[32];
    testMacKey(macKey);
    ChessScene scene;
    scene.setup();
    prepareOnline(scene, PieceColor::White, macKey);
    GameSummary stale;
    if (ProfileStorage::loadPendingResult(stale) ==
        ProfileStorage::LoadStatus::Loaded) {
        CHECK(ProfileStorage::clearPendingResult(stale));
    }
    scene.m_controlAwaitingAck = true;
    scene.m_pendingControl.eventId = 7;
    scene.m_pendingControl.control = NetControlType::DrawAccept;
    scene.m_nextControlEventId = 8;
    scene.m_board.setSideToMove(PieceColor::Black);
    scene.quitUnfinishedOnlineGame();
    CHECK(scene.m_localColor == PieceColor::White);
    CHECK(scene.m_resultOutcome == GameOutcome::BlackWin);
    CHECK(scene.m_termination == TerminationReason::Resignation);
    CHECK(scene.m_terminalJournaled || scene.m_resultRecorded);
    CHECK(sentTagged(NetMsgType::ControlMsg, sizeof(ControlNetMsg), macKey));
    bool resignation = false;
    for (const auto& packet : auditSent) {
        if (packet.size() != sizeof(ControlNetMsg)) continue;
        ControlNetMsg message{};
        std::memcpy(&message, packet.data(), sizeof(message));
        if (message.control != NetControlType::GameEnd) continue;
        CHECK(message.eventId == 7);
        CHECK(message.arg0 == static_cast<uint8_t>(NetGameResult::BlackWin));
        CHECK(message.arg1 == static_cast<uint8_t>(NetTermination::Resignation));
        CHECK(message.relatedEventId == 0);
        CHECK(sessionPacketTagMatches(packet.data(), packet.size(), macKey));
        resignation = true;
    }
    CHECK(resignation);
    CHECK(CardGFX::scenes().push(&scene));
    scene.leaveOnlineGame();
    CHECK(CardGFX::scenes().active() == &scene);
    CHECK(scene.m_netMode == ChessScene::NetworkMode::Online);
    CardGFX::scenes().pop();
}

void pressModalButton(Modal& modal, const char* label) {
    CHECK(modal.m_buttonCount > 0);
    for (uint8_t i = 0; i < modal.m_buttonCount; ++i) {
        if (std::strcmp(modal.m_buttons[i].label, label) != 0) continue;
        auto action = modal.m_buttons[i].callback;
        CHECK(static_cast<bool>(action));
        action();
        return;
    }
    CHECK(false);
}

void armJoiner(LobbyScene& lobby, ChessScene& chess) {
    chess.setup();
    lobby.setup(&chess);
    std::memcpy(lobby.m_localKeyPair.secret, kBobSecret, sizeof(kBobSecret));
    std::memcpy(lobby.m_localKeyPair.publicKey, kBobPublic, sizeof(kBobPublic));
    std::memcpy(lobby.m_peerPublicKey, kAlicePublic, sizeof(kAlicePublic));
    lobby.m_gameId = 0x1234;
    lobby.m_selectedVariant = ChessVariant::Standard;
    lobby.m_positionIndex = 518;
    lobby.m_selectedTimeControl = TimeControl::None;
    lobby.m_state = LobbyScene::LobbyState::Joining;
    lobby.m_connecting = true;
    lobby.m_pairCodeVisible = false;
    lobby.m_stateStartTime = auditNow;
    lobby.m_lastAcceptSendTime = auditNow;
    const uint8_t mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::memcpy(lobby.m_peerMac, mac, sizeof(mac));
    EspNowTransport::instance().init();
    CHECK(EspNowTransport::instance().addPeer(mac));
    auditSent.clear();
    auditIncoming.clear();
}

GameStartMsg taggedGameStart(const uint8_t macKey[32], bool corruptTag) {
    GameStartMsg start;
    start.header.gameId = 0x1234;
    start.header.sessionId = 0x89abcdefu;
    start.yourColor = static_cast<uint8_t>(PieceColor::Black);
    start.variant = static_cast<uint8_t>(ChessVariant::Standard);
    start.positionIndex = 518;
    start.timeControl = static_cast<uint8_t>(TimeControl::None);
    stampSessionPacket(&start, sizeof(start), macKey);
    if (corruptTag) {
        reinterpret_cast<uint8_t*>(&start)[sizeof(start) - 1] ^= 0x01u;
    }
    return start;
}

void pairCodeGatesStart() {
    drainScenes();
    uint8_t macKey[32];
    testMacKey(macKey);
    char sas[7];
    uint8_t shared[32];
    CHECK(NetCrypto::sharedSecret(kBobSecret, kAlicePublic, shared));
    NetCrypto::deriveSas(shared, kAlicePublic, kBobPublic, 0x1234, 0x89abcdefu, sas);
    char expected[24];
    std::snprintf(expected, sizeof(expected), "Pair code %s", sas);

    {
        ChessScene chess;
        LobbyScene lobby;
        armJoiner(lobby, chess);
        const GameStartMsg bad = taggedGameStart(macKey, true);
        injectPacket(&bad, sizeof(bad));
        lobby.onTick(0);
        CHECK(!lobby.m_pairCodeVisible);
        CHECK(lobby.m_state == LobbyScene::LobbyState::Joining);
        CHECK(chess.m_netMode == ChessScene::NetworkMode::Local);
        CHECK(!sentTagged(NetMsgType::GameStartAck, sizeof(GameStartAckMsg), macKey));

        const GameStartMsg good = taggedGameStart(macKey, false);
        injectPacket(&good, sizeof(good));
        lobby.onTick(0);
        CHECK(lobby.m_pairCodeVisible);
        CHECK(std::strcmp(lobby.m_menuModal.m_message, expected) == 0);
        CHECK(!sentTagged(NetMsgType::GameStartAck, sizeof(GameStartAckMsg), macKey));
        pressModalButton(lobby.m_menuModal, "Match");
        CHECK(sentTagged(NetMsgType::GameStartAck, sizeof(GameStartAckMsg), macKey));
        CHECK(lobby.m_state == LobbyScene::LobbyState::Paired);
        CHECK(chess.m_netMode == ChessScene::NetworkMode::Online);
        CHECK(std::memcmp(chess.m_macKey, macKey, sizeof(macKey)) == 0);
        CHECK(CardGFX::scenes().active() == &chess);
        drainScenes();
    }
    {
        ChessScene chess;
        LobbyScene lobby;
        armJoiner(lobby, chess);
        const GameStartMsg good = taggedGameStart(macKey, false);
        injectPacket(&good, sizeof(good));
        lobby.onTick(0);
        CHECK(lobby.m_pairCodeVisible);
        CHECK(std::strcmp(lobby.m_menuModal.m_message, expected) == 0);
        pressModalButton(lobby.m_menuModal, "Cancel");
        CHECK(lobby.m_state != LobbyScene::LobbyState::Paired);
        CHECK(lobby.m_state != LobbyScene::LobbyState::Joining);
        CHECK(chess.m_netMode == ChessScene::NetworkMode::Local);
        CHECK(CardGFX::scenes().active() != &chess);
        CHECK(!sentTagged(NetMsgType::GameStartAck, sizeof(GameStartAckMsg), macKey));
    }
    drainScenes();
}

int main() {
    keyboard(); canvas(); puzzle(); puzzleCategory(); castling(); legacyResume(); timeout(); longGame(); runningClock();
    authenticatedPackets(); resignationAuthentication(); remoteClockBound(); onlineQuitResignation(); pairCodeGatesStart();
    std::puts("Scene regressions passed (keyboard, clipping, puzzles, puzzle categories, Chess960, legacy resume, local/remote timeout, 260-ply game and repetition, running clock save, bad tags, resignation, clock bounds, online quit, pair code).");
}
