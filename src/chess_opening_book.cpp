#include "chess_opening_book.h"
#include "chess_zobrist.h"
#include "chess_rules.h"
#include <esp_random.h>
#include <cstring>

// =====================================================================
// Opening Book — built from move sequences at init time.
// Each opening line is a sequence of moves (from, to) pairs.
// At init, we replay each line on a board, recording the Zobrist hash
// at each position along with the next move to play.
// =====================================================================

struct BookEntry {
    uint32_t hash;
    uint8_t fromCol, fromRow, toCol, toRow;
};

// Max entries we can store
static constexpr int MAX_BOOK_ENTRIES = 512;
static BookEntry s_book[MAX_BOOK_ENTRIES];
static int s_bookSize = 0;
static bool s_initialized = false;

// Move encoded as 4 bytes: fromCol, fromRow, toCol, toRow
struct RawMove { uint8_t fc, fr, tc, tr; };

// Opening lines as sequences of raw moves
// Each line ends when the array ends.

// 1.e4 lines
static const RawMove LINE_ITALIAN[] = {
    {4,1,4,3}, {4,6,4,4}, {6,0,5,2}, {1,7,2,5}, {5,0,2,3}, {5,7,2,4}, // 1.e4 e5 2.Nf3 Nc6 3.Bc4 Bc5
    {2,1,2,2}, {6,7,5,5}, // 4.c3 Nf6
};
static const RawMove LINE_RUY_LOPEZ[] = {
    {4,1,4,3}, {4,6,4,4}, {6,0,5,2}, {1,7,2,5}, {5,0,1,4}, // 1.e4 e5 2.Nf3 Nc6 3.Bb5
    {0,6,0,5}, {1,4,0,3}, {6,7,5,5}, {3,0,4,1}, // 3...a6 4.Ba4 Nf6 5.Qe2 (Closed Ruy)
};
static const RawMove LINE_SICILIAN_OPEN[] = {
    {4,1,4,3}, {2,6,2,4}, {6,0,5,2}, {3,6,3,5}, {3,1,3,3}, {2,4,3,3}, {5,2,3,3}, // 1.e4 c5 2.Nf3 d6 3.d4 cxd4 4.Nxd4
    {6,7,5,5}, {1,0,2,2}, // 4...Nf6 5.Nc3
};
static const RawMove LINE_SICILIAN_NAJDORF[] = {
    {4,1,4,3}, {2,6,2,4}, {6,0,5,2}, {3,6,3,5}, {3,1,3,3}, {2,4,3,3}, {5,2,3,3},
    {6,7,5,5}, {1,0,2,2}, {0,6,0,5}, // 1.e4 c5 2.Nf3 d6 3.d4 cxd4 4.Nxd4 Nf6 5.Nc3 a6
};
static const RawMove LINE_FRENCH[] = {
    {4,1,4,3}, {4,6,4,5}, {3,1,3,3}, {3,6,3,4}, {4,3,4,4}, // 1.e4 e6 2.d4 d5 3.e5
    {2,6,2,4}, {2,1,2,2}, {1,7,2,5}, // 3...c5 4.c3 Nc6
};
static const RawMove LINE_CARO_KANN[] = {
    {4,1,4,3}, {2,6,2,5}, {3,1,3,3}, {3,6,3,4}, {4,3,4,4}, // 1.e4 c6 2.d4 d5 3.e5
    {2,7,5,4}, {6,0,5,2}, {4,6,4,5}, // 3...Bf5 4.Nf3 e6
};
static const RawMove LINE_SCANDINAVIAN[] = {
    {4,1,4,3}, {3,6,3,4}, {4,3,3,4}, {3,7,3,4}, // 1.e4 d5 2.exd5 Qxd5
    {1,0,2,2}, {3,4,0,4}, // 3.Nc3 Qa5
};

// 1.d4 lines
static const RawMove LINE_QGD[] = {
    {3,1,3,3}, {3,6,3,4}, {2,1,2,3}, {4,6,4,5}, // 1.d4 d5 2.c4 e6
    {1,0,2,2}, {6,7,5,5}, {2,0,6,4}, // 3.Nc3 Nf6 4.Bg5
};
static const RawMove LINE_QGA[] = {
    {3,1,3,3}, {3,6,3,4}, {2,1,2,3}, {3,4,2,3}, // 1.d4 d5 2.c4 dxc4
    {4,1,4,2}, {4,6,4,5}, {5,0,2,3}, // 3.e3 e6 4.Bxc4
};
static const RawMove LINE_SLAV[] = {
    {3,1,3,3}, {3,6,3,4}, {2,1,2,3}, {2,6,2,5}, // 1.d4 d5 2.c4 c6
    {6,0,5,2}, {6,7,5,5}, {1,0,2,2}, // 3.Nf3 Nf6 4.Nc3
};
static const RawMove LINE_KID[] = {
    {3,1,3,3}, {6,7,5,5}, {2,1,2,3}, {6,6,6,5}, // 1.d4 Nf6 2.c4 g6
    {1,0,2,2}, {5,7,6,6}, {4,1,4,3}, {3,6,3,5}, // 3.Nc3 Bg7 4.e4 d6
};
static const RawMove LINE_NIMZO[] = {
    {3,1,3,3}, {6,7,5,5}, {2,1,2,3}, {4,6,4,5}, // 1.d4 Nf6 2.c4 e6
    {1,0,2,2}, {5,7,1,3}, // 3.Nc3 Bb4
};

// 1.Nf3 / 1.c4 lines
static const RawMove LINE_ENGLISH[] = {
    {2,1,2,3}, {4,6,4,4}, {1,0,2,2}, {6,7,5,5}, // 1.c4 e5 2.Nc3 Nf6
};
static const RawMove LINE_RETI[] = {
    {6,0,5,2}, {3,6,3,4}, {6,1,6,2}, // 1.Nf3 d5 2.g3
};

// Macro to add a line
#define ADD_LINE(arr) addLine(arr, sizeof(arr) / sizeof(arr[0]))

static void addLine(const RawMove* moves, int count) {
    ChessBoard board;
    board.reset();

    for (int i = 0; i < count; i++) {
        if (s_bookSize >= MAX_BOOK_ENTRIES) return;

        uint32_t h = ChessZobrist::hash(board);

        // Check if this position+move already exists
        bool duplicate = false;
        for (int j = 0; j < s_bookSize; j++) {
            if (s_book[j].hash == h &&
                s_book[j].fromCol == moves[i].fc &&
                s_book[j].fromRow == moves[i].fr &&
                s_book[j].toCol == moves[i].tc &&
                s_book[j].toRow == moves[i].tr) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate) {
            s_book[s_bookSize].hash = h;
            s_book[s_bookSize].fromCol = moves[i].fc;
            s_book[s_bookSize].fromRow = moves[i].fr;
            s_book[s_bookSize].toCol = moves[i].tc;
            s_book[s_bookSize].toRow = moves[i].tr;
            s_bookSize++;
        }

        // Apply the move to advance the board
        Move m;
        m.from = makeSquare(moves[i].fc, moves[i].fr);
        m.to = makeSquare(moves[i].tc, moves[i].tr);

        // Detect castling, en passant, promotion from the board state
        MoveList legal;
        ChessRules::generateLegal(board, legal);
        bool matched = false;
        for (uint8_t j = 0; j < legal.count; j++) {
            if (legal.moves[j].from == m.from && legal.moves[j].to == m.to) {
                m = legal.moves[j]; // Get full move with flags
                matched = true;
                break;
            }
        }
        if (!matched) break; // Invalid move — stop processing this line

        board.makeMove(m);
    }
}

// Sort book by hash for binary search
static void sortBook() {
    // Simple insertion sort (small array)
    for (int i = 1; i < s_bookSize; i++) {
        BookEntry key = s_book[i];
        int j = i - 1;
        while (j >= 0 && s_book[j].hash > key.hash) {
            s_book[j + 1] = s_book[j];
            j--;
        }
        s_book[j + 1] = key;
    }
}

namespace ChessOpeningBook {

void init() {
    if (s_initialized) return;
    s_initialized = true;
    s_bookSize = 0;

    // Add all opening lines
    ADD_LINE(LINE_ITALIAN);
    ADD_LINE(LINE_RUY_LOPEZ);
    ADD_LINE(LINE_SICILIAN_OPEN);
    ADD_LINE(LINE_SICILIAN_NAJDORF);
    ADD_LINE(LINE_FRENCH);
    ADD_LINE(LINE_CARO_KANN);
    ADD_LINE(LINE_SCANDINAVIAN);
    ADD_LINE(LINE_QGD);
    ADD_LINE(LINE_QGA);
    ADD_LINE(LINE_SLAV);
    ADD_LINE(LINE_KID);
    ADD_LINE(LINE_NIMZO);
    ADD_LINE(LINE_ENGLISH);
    ADD_LINE(LINE_RETI);

    sortBook();
}

bool probe(ChessBoard& board, Move& move) {
    if (!s_initialized) init();
    if (s_bookSize == 0) return false;

    uint32_t h = ChessZobrist::hash(board);

    // Binary search for first entry with this hash
    int lo = 0, hi = s_bookSize - 1;
    int found = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (s_book[mid].hash < h) lo = mid + 1;
        else if (s_book[mid].hash > h) hi = mid - 1;
        else { found = mid; hi = mid - 1; } // find leftmost
    }

    if (found < 0) return false;

    // Count all entries with this hash
    int count = 0;
    for (int i = found; i < s_bookSize && s_book[i].hash == h; i++) {
        count++;
    }

    // Pick one randomly
    int pick = esp_random() % count;
    const BookEntry& chosen = s_book[found + pick];

    move.from = makeSquare(chosen.fromCol, chosen.fromRow);
    move.to = makeSquare(chosen.toCol, chosen.toRow);
    move.promotion = PieceType::None;
    move.isCastle = false;
    move.isEnPassant = false;

    // Validate against legal moves and fill in flags
    MoveList legal;
    ChessRules::generateLegal(board, legal);
    for (uint8_t i = 0; i < legal.count; i++) {
        if (legal.moves[i].from == move.from && legal.moves[i].to == move.to) {
            move = legal.moves[i];
            return true;
        }
    }

    return false; // Hash collision or invalid entry
}

} // namespace ChessOpeningBook
