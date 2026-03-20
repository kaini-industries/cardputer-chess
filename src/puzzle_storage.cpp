#include "puzzle_storage.h"
#include <Preferences.h>
#include <cstring>

static constexpr const char* NVS_NAMESPACE = "chess";
static constexpr const char* NVS_KEY = "puzzles";

namespace PuzzleStorage {

void loadProgress(PuzzleProgress& progress) {
    memset(&progress, 0, sizeof(progress));

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    size_t len = prefs.getBytesLength(NVS_KEY);
    if (len >= sizeof(PuzzleProgress)) {
        prefs.getBytes(NVS_KEY, &progress, sizeof(PuzzleProgress));
    }
    prefs.end();
}

void saveProgress(const PuzzleProgress& progress) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBytes(NVS_KEY, &progress, sizeof(PuzzleProgress));
    prefs.end();
}

bool isPuzzleCompleted(const PuzzleProgress& progress, uint8_t index) {
    return (progress.completed[index / 8] & (1 << (index % 8))) != 0;
}

void markPuzzleCompleted(PuzzleProgress& progress, uint8_t index) {
    progress.completed[index / 8] |= (1 << (index % 8));
}

uint16_t completedCount(const PuzzleProgress& progress, uint16_t total) {
    uint16_t count = 0;
    for (uint16_t i = 0; i < total; i++) {
        if (isPuzzleCompleted(progress, (uint8_t)i)) count++;
    }
    return count;
}

} // namespace PuzzleStorage
