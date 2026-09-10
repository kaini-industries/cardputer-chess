#include <cctype>
#include <cstring>
#include <initializer_list>
#include "check.h"
// Include search internals so horizon scores and the full sorter are tested
// deterministically without adding firmware-only public test hooks.
#include "chess_ai.cpp"
#include "chess_zobrist.h"
uint32_t auditNow = 1000;

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
    CHECK(!ChessRules::hasInsufficientMatingMaterial(position("7k/8/8/8/8/8/8/KNN5 w"), white));
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
