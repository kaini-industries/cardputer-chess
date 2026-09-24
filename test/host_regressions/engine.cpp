#include <cctype>
#include <cstring>
#include <initializer_list>
#include "check.h"
// Include search internals so horizon scores and the full sorter are tested
// deterministically without adding firmware-only public test hooks.
#include "chess_ai.cpp"
#include "chess_zobrist.h"
uint32_t auditNow = 1000;
static uint32_t millisCalls = 0;
static uint32_t millisAbortOn = 0;
static uint32_t scriptedMillis() {
    ++millisCalls;
    if (millisAbortOn != 0 && millisCalls >= millisAbortOn)
        return auditNow + 0x10000000u;
    return auditNow;
}

ChessBoard position(const char* fen) {
    ChessBoard board;
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) board.set(c, r, Piece{});
    int row = 7, col = 0;
    for (; *fen && *fen != ' '; ++fen) {
        if (*fen == '/') { --row; col = 0; }
        else if (*fen >= '1' && *fen <= '8') col += *fen - '0';
        else {
            const char* types = "PNBRQK";
            const auto type = static_cast<PieceType>(std::strchr(types, std::toupper(*fen)) - types + 1);
            board.set(col++, row, Piece(type, std::islower(*fen) ? PieceColor::Black : PieceColor::White));
        }
    }
    board.setSideToMove(fen[1] == 'b' ? PieceColor::Black : PieceColor::White);
    board.setCastleRights(0);
    board.setEnPassantTarget(NO_SQUARE);
    return board;
}
uint64_t perft(ChessBoard& board, int depth) {
    if (!depth) return 1;
    MoveList legal;
    ChessRules::generateLegal(board, legal);
    uint64_t count = 0;
    for (uint16_t i = 0; i < legal.count; ++i) {
        auto record = board.makeMove(legal.moves[i]);
        count += perft(board, depth - 1);
        board.unmakeMove(record);
    }
    return count;
}
void ordering() {
    auto board = position("R6R/3Q4/1Q4Q1/4Q3/2Q4Q/Q4Q2/pp1Q4/kBNN1KB1 w");
    MoveList legal;
    ChessRules::generateLegal(board, legal);
    CHECK(legal.count == 218);
    CHECK(ChessAI::evaluate(board) < MATE_SCORE - MAX_SEARCH_PLY);
    MoveList original = legal;
    sortMoves(legal, board);
    for (unsigned i = 0; i < legal.count; ++i) {
        if (i) CHECK(scoreMoveForOrdering(legal.moves[i-1], board) >= scoreMoveForOrdering(legal.moves[i], board));
        unsigned matches = 0;
        for (unsigned j = 0; j < original.count; ++j) matches += legal.moves[i] == original.moves[j];
        CHECK(matches == 1);
    }
}
void abortedSearches();
void horizon() {
    s_searchAborted = false;
    s_searchDeadline = auditNow + 1000;
    auto mate = position("7k/6Q1/5K2/8/8/8/8/8 b");
    CHECK(ChessRules::isCheckmate(mate));
    CHECK(quiesce(mate, -20000, 20000, 0, 4) == -MATE_SCORE + 4);
    mate.setHalfmoveClock(100);
    CHECK(quiesce(mate, -20000, 20000, 0, 4) == -MATE_SCORE + 4);
    auto stale = position("7k/5K2/6Q1/8/8/8/8/8 b");
    CHECK(ChessRules::isStalemate(stale));
    CHECK(quiesce(stale, -20000, 20000, 0) == 0);
    CHECK(quiesce(stale, -20000, -15000, 0) == 0); // Before stand-pat cutoff.
    ChessBoard fifty;
    fifty.setHalfmoveClock(100);
    CHECK(quiesce(fifty, -20000, 20000, 0) == 0);
    auto loneBishop = position("7k/8/8/8/8/8/1B6/K7 w");
    CHECK(quiesce(loneBishop, -20000, 20000, 0) == 0);
    auto checking = position("7k/7R/8/8/8/8/8/K7 b");
    const auto before = ChessZobrist::hash(checking);
    CHECK(quiesce(checking, -20000, 20000, 8, MAX_SEARCH_PLY) == ChessAI::evaluate(checking));
    CHECK(ChessZobrist::hash(checking) == before);
    auditNow = UINT32_MAX - 20;
    s_searchDeadline = auditNow + 100;
    CHECK(quiesce(stale, -20000, 20000, 0) == 0 && !s_searchAborted);
    auditNow = 1000;
    abortedSearches();
}
void abortedSearches() {
    auditMillisHook = nullptr;
    millisAbortOn = 0;
    millisCalls = 0;
    auditNow = 1000;

    // Queen a1, king e1, black king e8. Not an opening-book position.
    // Generation order makes Qb2 the first legal move and Qc3 the second;
    // Qc3 is the better quiet move, so a finished depth-1 search would play it.
    auto quiet = position("4k3/8/8/8/8/8/8/Q3K3 w");
    Move book;
    CHECK(!ChessOpeningBook::probe(quiet, book));
    MoveList quietMoves;
    ChessRules::generateLegal(quiet, quietMoves);
    sortMoves(quietMoves, quiet);
    CHECK(quietMoves.count > 2);
    CHECK(quietMoves.moves[0].from == Square(0, 0) && quietMoves.moves[0].to == Square(1, 1));
    CHECK(quietMoves.moves[1].from == Square(0, 0) && quietMoves.moves[1].to == Square(2, 2));

    // Child hits an already-due deadline and returns the draw sentinel 0.
    // That must not become a beta cutoff.
    const auto quietHash = ChessZobrist::hash(quiet);
    s_searchAborted = false;
    s_searchDeadline = auditNow;
    CHECK(alphaBeta(quiet, 1, -SEARCH_INFINITY, -10, 0) == 0);
    CHECK(s_searchAborted);
    CHECK(ChessZobrist::hash(quiet) == quietHash);

    // Quiescence must ignore the same sentinel from a child.
    auto capture = position("4k3/8/8/8/4p3/3Q4/8/4K3 w");
    CHECK(!ChessRules::isInCheck(capture, capture.sideToMove()));
    MoveList captures;
    ChessRules::generateLegal(capture, captures);
    bool hasCapture = false;
    for (uint16_t i = 0; i < captures.count; ++i)
        hasCapture = hasCapture || !capture.at(captures.moves[i].to.col, captures.moves[i].to.row).empty();
    CHECK(hasCapture);
    auditMillisHook = scriptedMillis;
    millisCalls = 0;
    millisAbortOn = 2;
    s_searchAborted = false;
    s_searchDeadline = auditNow + 1000;
    const auto captureHash = ChessZobrist::hash(capture);
    CHECK(quiesce(capture, -20000, 20000, 0) == 0);
    CHECK(s_searchAborted);
    CHECK(ChessZobrist::hash(capture) == captureHash);
    auditMillisHook = nullptr;
    millisAbortOn = 0;

    // Deadline already due before the search: keep the ordered first legal move.
    s_searchAborted = false;
    s_searchDeadline = auditNow;
    CHECK(ChessAI::findBestMove(quiet, AIDifficulty::Hard, 1) == quietMoves.moves[0]);
    CHECK(ChessZobrist::hash(quiet) == quietHash);

    auto wide = position("R6R/3Q4/1Q4Q1/4Q3/2Q4Q/Q4Q2/pp1Q4/kBNN1KB1 w");
    CHECK(!ChessOpeningBook::probe(wide, book));
    MoveList wideMoves;
    ChessRules::generateLegal(wide, wideMoves);
    CHECK(wideMoves.count == 218);
    sortMoves(wideMoves, wide);
    s_searchAborted = false;
    s_searchDeadline = auditNow;
    Move widePlayed = ChessAI::findBestMove(wide, AIDifficulty::Hard);
    CHECK(widePlayed == wideMoves.moves[0]);
    CHECK(wideMoves.contains(widePlayed));

    // Depth 1 scores Qb2, then the better Qc3, then the clock expires.
    // The aborted iteration must not replace Qb2, and the sentinel 0 must not
    // be stored as the score of the move that timed out.
    auditMillisHook = scriptedMillis;
    auto depth0 = [&](const Move& move, uint32_t& calls) {
        s_searchAborted = false;
        s_searchDeadline = auditNow + 100000;
        millisAbortOn = 0;
        millisCalls = 0;
        auto rec = quiet.makeMove(move);
        CHECK(!ChessRules::isInCheck(quiet, quiet.sideToMove()));
        int16_t score = -alphaBeta(quiet, 0, -SEARCH_INFINITY, SEARCH_INFINITY, 1);
        quiet.unmakeMove(rec);
        CHECK(!s_searchAborted);
        calls = millisCalls;
        return score;
    };
    uint32_t calls0 = 0, calls1 = 0;
    int16_t worse = depth0(quietMoves.moves[0], calls0);
    int16_t better = depth0(quietMoves.moves[1], calls1);
    CHECK(better > worse);
    CHECK(calls0 == 2 && calls1 == 2);
    millisCalls = 0;
    millisAbortOn = 1 + calls0 + calls1 + 1;
    s_searchDeadline = 0;
    s_searchAborted = false;
    CHECK(ChessAI::findBestMove(quiet, AIDifficulty::Hard) == quietMoves.moves[0]);
    CHECK(ChessZobrist::hash(quiet) == quietHash);

    auditMillisHook = nullptr;
    millisAbortOn = 0;
    millisCalls = 0;
    s_searchDeadline = 0;
    s_searchAborted = false;
    auditNow = 1000;
}
void repetition() {
    ChessBoard board;
    MoveRecord history[9];
    unsigned count = 0;
    for (auto text : {"e2e4", "g8f6", "g1f3", "f6g8", "f3g1", "g8f6", "g1f3", "f6g8", "f3g1"}) {
        MoveList legal;
        ChessRules::generateLegal(board, legal);
        bool found = false;
        for (unsigned i = 0; i < legal.count; ++i) {
            const auto& move = legal.moves[i];
            if (move.from == Square(text[0]-'a', text[1]-'1') && move.to == Square(text[2]-'a', text[3]-'1')) {
                history[count++] = board.makeMove(move); found = true; break;
            }
        }
        CHECK(found);
    }
    CHECK(ChessRules::isThreefoldRepetition(board, history, count));
    for (bool pinned : {false, true}) {
        auto ep = position(pinned ? "k3r3/8/8/3pP3/8/8/8/4K3 w" : "k7/8/8/3pP3/8/8/8/4K3 w");
        auto without = ep;
        ep.setEnPassantTarget(Square(3, 5));
        CHECK(ChessZobrist::hash(ep) != ChessZobrist::hash(without));
        CHECK((ChessZobrist::repetitionHash(ep) == ChessZobrist::repetitionHash(without)) == pinned);
    }
}
void material() {
    auto white = PieceColor::White;
    CHECK(ChessRules::hasInsufficientMatingMaterial(position("7k/7q/8/8/8/8/8/K7 b"), white));
    CHECK(ChessRules::hasInsufficientMatingMaterial(position("7k/8/8/8/8/8/8/KN6 w"), white));
    CHECK(ChessRules::hasInsufficientMatingMaterial(position("7k/7q/8/8/8/8/8/KN6 w"), white));
    CHECK(!ChessRules::hasInsufficientMatingMaterial(position("7k/7r/8/8/8/8/8/KN6 w"), white));
    CHECK(ChessRules::hasInsufficientMatingMaterial(position("7k/8/8/8/8/8/8/KNN5 w"), white));
    CHECK(!ChessRules::hasInsufficientMatingMaterial(position("7k/8/8/8/8/8/8/KNNN4 w"), white));
    CHECK(!ChessRules::hasInsufficientMatingMaterial(position("7k/7r/8/8/8/8/8/KNN5 w"), white));
    CHECK(ChessRules::isInsufficientMaterial(position("7k/8/8/8/5b2/4B3/3B4/K7 w")));
    CHECK(!ChessRules::isInsufficientMaterial(position("7k/8/8/8/4b3/4B3/3B4/K7 w")));
    CHECK(!ChessRules::hasInsufficientMatingMaterial(position("7k/7p/8/8/8/8/1B6/K7 w"), white));
}
int main() {
    ordering(); horizon(); repetition(); material();
    ChessBoard initial;
    CHECK(perft(initial, 4) == 197281);
    std::puts("Engine regressions passed (ordering, horizon, repetition, material, perft).");
}
