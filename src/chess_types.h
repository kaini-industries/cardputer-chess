#ifndef CHESS_TYPES_H
#define CHESS_TYPES_H

#include <cstdint>
#include <cstring>

// =====================================================================
// Chess data types: pieces, squares, moves, and undo records.
// Pure data — no logic, no dependencies on CardGFX.
// =====================================================================

enum class PieceType : uint8_t {
    None   = 0,
    Pawn   = 1,
    Knight = 2,
    Bishop = 3,
    Rook   = 4,
    Queen  = 5,
    King   = 6
};

enum class PieceColor : uint8_t {
    White = 0,
    Black = 1
};

inline PieceColor opponent(PieceColor c) {
    return (c == PieceColor::White) ? PieceColor::Black : PieceColor::White;
}

struct Piece {
    PieceType  type  = PieceType::None;
    PieceColor color = PieceColor::White;

    bool empty() const { return type == PieceType::None; }

    bool operator==(const Piece& o) const {
        return type == o.type && color == o.color;
    }
    bool operator!=(const Piece& o) const { return !(*this == o); }

    // Character representation for display: uppercase = white, lowercase = black
    char toChar() const {
        if (empty()) return ' ';
        const char chars[] = " PNBRQK";
        char ch = chars[static_cast<uint8_t>(type)];
        return (color == PieceColor::Black) ? (ch + 32) : ch; // tolower
    }

    // Single uppercase letter for the piece type (used in notation)
    char typeChar() const {
        const char chars[] = " PNBRQK";
        return chars[static_cast<uint8_t>(type)];
    }
};

inline Piece makePiece(PieceType t, PieceColor c) {
    return Piece{t, c};
}

struct Square {
    uint8_t col = 0; // 0-7 (a-h)
    uint8_t row = 0; // 0-7 (rank 1-8, row 0 = rank 1 = white's back rank)

    bool operator==(const Square& o) const { return col == o.col && row == o.row; }
    bool operator!=(const Square& o) const { return !(*this == o); }

    bool valid() const { return col < 8 && row < 8; }

    // File letter: 'a'-'h'
    char file() const { return 'a' + col; }
    // Rank digit: '1'-'8'
    char rank() const { return '1' + row; }
};

inline Square makeSquare(uint8_t col, uint8_t row) {
    return Square{col, row};
}

// Sentinel value for "no square"
static constexpr Square NO_SQUARE = {0xFF, 0xFF};

inline bool isNoSquare(const Square& s) {
    return s.col == 0xFF;
}

// Castle rights packed into 4 bits
namespace CastleRights {
    static constexpr uint8_t WhiteKing  = 0x01;
    static constexpr uint8_t WhiteQueen = 0x02;
    static constexpr uint8_t BlackKing  = 0x04;
    static constexpr uint8_t BlackQueen = 0x08;
    static constexpr uint8_t All        = 0x0F;
    static constexpr uint8_t WhiteAll   = WhiteKing | WhiteQueen;
    static constexpr uint8_t BlackAll   = BlackKing | BlackQueen;
}

struct Move {
    Square    from;
    Square    to;
    PieceType promotion  = PieceType::None; // Non-None only for pawn promotion
    bool      isCastle   = false;
    bool      isEnPassant = false;

    bool operator==(const Move& o) const {
        return from == o.from && to == o.to && promotion == o.promotion;
    }

    // Format as long algebraic string into buffer (e.g. "e2e4", "e4xd5", "e7e8=Q", "O-O")
    void toString(char* buf, uint8_t bufLen, bool isCapture = false) const {
        if (bufLen < 8) { buf[0] = '\0'; return; }
        if (isCastle) {
            if (to.col > from.col) {
                strncpy(buf, "O-O", bufLen);
            } else {
                strncpy(buf, "O-O-O", bufLen);
            }
            buf[bufLen - 1] = '\0';
            return;
        }
        uint8_t i = 0;
        buf[i++] = from.file();
        buf[i++] = from.rank();
        if (isCapture) buf[i++] = 'x';
        buf[i++] = to.file();
        buf[i++] = to.rank();
        if (promotion != PieceType::None) {
            buf[i++] = '=';
            const char chars[] = " PNBRQK";
            buf[i++] = chars[static_cast<uint8_t>(promotion)];
        }
        buf[i] = '\0';
    }
};

// Undo record — everything needed to reverse a move
struct MoveRecord {
    Move    move;
    Piece   captured;          // What was captured (empty if none)
    Square  capturedSquare;    // Where the captured piece was (differs from move.to for en passant)
    uint8_t prevCastleRights;
    Square  prevEnPassantTarget;
    uint8_t prevHalfmoveClock;
};

// Fixed-size move list for move generation
struct MoveList {
    static constexpr uint8_t MAX_MOVES = 128;

    Move    moves[MAX_MOVES];
    uint8_t count = 0;

    void clear() { count = 0; }

    void add(const Move& m) {
        if (count < MAX_MOVES) {
            moves[count++] = m;
        }
    }

    bool contains(const Move& m) const {
        for (uint8_t i = 0; i < count; i++) {
            if (moves[i] == m) return true;
        }
        return false;
    }
};

#endif // CHESS_TYPES_H
