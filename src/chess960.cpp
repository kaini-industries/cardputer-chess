#include "chess960.h"
#include <esp_random.h>

// Knight placement lookup table: maps index (0-9) to two positions
// within 5 remaining squares after bishops and queen are placed.
static const uint8_t KNIGHT_TABLE[10][2] = {
    {0, 1}, {0, 2}, {0, 3}, {0, 4},
    {1, 2}, {1, 3}, {1, 4},
    {2, 3}, {2, 4},
    {3, 4}
};

Chess960Position chess960Generate(uint16_t index) {
    Chess960Position pos;
    // Track which columns are available
    bool used[8] = {};

    // 1. Place light-square bishop (columns 1,3,5,7)
    uint8_t lb = index % 4;
    index /= 4;
    uint8_t lightCols[4] = {1, 3, 5, 7};
    pos.backRank[lightCols[lb]] = PieceType::Bishop;
    used[lightCols[lb]] = true;

    // 2. Place dark-square bishop (columns 0,2,4,6)
    uint8_t db = index % 4;
    index /= 4;
    uint8_t darkCols[4] = {0, 2, 4, 6};
    pos.backRank[darkCols[db]] = PieceType::Bishop;
    used[darkCols[db]] = true;

    // 3. Place queen in one of 6 remaining squares
    uint8_t q = index % 6;
    index /= 6;
    uint8_t remaining[6];
    uint8_t ri = 0;
    for (uint8_t c = 0; c < 8; c++) {
        if (!used[c]) remaining[ri++] = c;
    }
    pos.backRank[remaining[q]] = PieceType::Queen;
    used[remaining[q]] = true;

    // 4. Place two knights in 5 remaining squares (10 combinations)
    uint8_t kn = index; // 0-9
    uint8_t rem5[5];
    ri = 0;
    for (uint8_t c = 0; c < 8; c++) {
        if (!used[c]) rem5[ri++] = c;
    }
    pos.backRank[rem5[KNIGHT_TABLE[kn][0]]] = PieceType::Knight;
    pos.backRank[rem5[KNIGHT_TABLE[kn][1]]] = PieceType::Knight;
    used[rem5[KNIGHT_TABLE[kn][0]]] = true;
    used[rem5[KNIGHT_TABLE[kn][1]]] = true;

    // 5. Place R-K-R in the 3 remaining squares (in order)
    uint8_t rem3[3];
    ri = 0;
    for (uint8_t c = 0; c < 8; c++) {
        if (!used[c]) rem3[ri++] = c;
    }
    pos.backRank[rem3[0]] = PieceType::Rook;
    pos.backRank[rem3[1]] = PieceType::King;
    pos.backRank[rem3[2]] = PieceType::Rook;

    pos.rookQS = rem3[0];  // Left rook (queenside)
    pos.kingCol = rem3[1];
    pos.rookKS = rem3[2];  // Right rook (kingside)

    return pos;
}

uint16_t chess960RandomIndex() {
    return (uint16_t)(esp_random() % 960);
}
