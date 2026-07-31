#ifndef CHESS_CLOCK_H
#define CHESS_CLOCK_H

#include "chess_types.h"

#include <cstdint>

// Result of attempting to commit a move to the clock. The board should only
// accept the clock transition when Applied is returned.
enum class ChessClockMoveResult : uint8_t {
    Applied = 0,
    Disabled,
    NotStarted,
    Paused,
    WrongSide,
    Flagged,
};

// Serializable clock state. Timestamps are deliberately absent: snapshot()
// settles elapsed time at the supplied timestamp, and loadSnapshot() anchors
// a running clock at a new timestamp supplied by the caller.
struct ChessClockSnapshot {
    TimeControl timeControl = TimeControl::None;
    uint32_t whiteRemainingMs = 0;
    uint32_t blackRemainingMs = 0;
    PieceColor activeSide = PieceColor::White;
    bool started = false;
    bool paused = false;
    bool hasFlaggedSide = false;
    PieceColor flaggedSide = PieceColor::White;
    bool hasLastAddTimeEvent = false;
    uint32_t lastAddTimeEventId = 0;
};

// A deterministic chess clock driven entirely by caller-provided uint32_t
// monotonic timestamps. Unsigned timestamp subtraction makes elapsed-time
// accounting safe across the normal uint32_t rollover, provided no single
// observation interval spans a complete 2^32 millisecond cycle.
class ChessClock {
public:
    ChessClock() = default;
    explicit ChessClock(TimeControl timeControl);

    // Selects a time control and resets all clock state.
    void configure(TimeControl timeControl);

    TimeControl timeControl() const { return m_timeControl; }
    bool enabled() const { return m_timeControl != TimeControl::None; }
    uint32_t initialMs() const { return m_initialMs; }
    uint32_t incrementMs() const { return m_incrementMs; }

    bool hasStarted() const { return m_started; }
    bool isPaused() const { return m_paused; }
    bool isRunning() const;
    PieceColor activeSide() const { return m_activeSide; }

    // Starts a configured clock with White active at readyTimestampMs.
    bool start(uint32_t readyTimestampMs);

    // pause() first charges the active player through nowMs. resume() starts
    // charging that same player again from nowMs.
    bool pause(uint32_t nowMs);
    bool resume(uint32_t nowMs);

    // Settles the active clock through nowMs and records a flag if it expired.
    void update(uint32_t nowMs);

    // Charges mover through moveTimestampMs, applies increment (including on
    // White's first move), then activates the opponent at that same timestamp.
    ChessClockMoveResult commitMove(PieceColor mover,
                                    uint32_t moveTimestampMs);

    // Stored values are settled only through the most recent state-changing
    // timestamp. remainingMsAt() projects a running value without mutation.
    uint32_t remainingMs(PieceColor side) const;
    uint32_t remainingMsAt(PieceColor side, uint32_t nowMs) const;

    bool hasFlaggedSide() const { return m_hasFlaggedSide; }
    bool flagged(PieceColor side) const;
    bool flaggedAt(PieceColor side, uint32_t nowMs) const;
    PieceColor flaggedSide() const { return m_flaggedSide; }

    // Authoritative synchronization helpers. They settle local elapsed time at
    // nowMs, replace one or both values, re-evaluate flag state, and re-anchor
    // a live clock at nowMs. A positive correction can therefore clear a
    // prematurely observed local flag before the game result is finalized.
    bool setRemainingMs(PieceColor side, uint32_t remainingMs,
                        uint32_t nowMs);
    bool synchronizeRemainingMs(uint32_t whiteRemainingMs,
                                uint32_t blackRemainingMs,
                                uint32_t nowMs);

    // Applies an externally validated add-time event at most once. Event IDs
    // must come from one session-scoped, monotonically increasing uint32_t
    // sequence; duplicates and older/out-of-order IDs are rejected. Sequence
    // rollover is supported when fewer than 2^31 IDs separate observations.
    bool addTimeOnce(PieceColor recipient, uint32_t amountMs,
                     uint32_t eventId, uint32_t nowMs);

    ChessClockSnapshot snapshot(uint32_t nowMs) const;
    bool loadSnapshot(const ChessClockSnapshot& snapshot,
                      uint32_t resumeTimestampMs);

private:
    static uint8_t sideIndex(PieceColor side);
    static uint32_t saturatingAdd(uint32_t value, uint32_t addition);
    static bool sequenceAfter(uint32_t candidate, uint32_t reference);

    void settle(uint32_t nowMs);
    void reanchor(uint32_t nowMs);
    void reconcileAuthoritativeTimes(uint32_t nowMs);

    TimeControl m_timeControl = TimeControl::None;
    uint32_t m_initialMs = 0;
    uint32_t m_incrementMs = 0;
    uint32_t m_remainingMs[2] = {0, 0};
    PieceColor m_activeSide = PieceColor::White;
    uint32_t m_anchorTimestampMs = 0;
    bool m_started = false;
    bool m_paused = false;
    bool m_hasFlaggedSide = false;
    PieceColor m_flaggedSide = PieceColor::White;
    bool m_hasLastAddTimeEvent = false;
    uint32_t m_lastAddTimeEventId = 0;
};

#endif // CHESS_CLOCK_H
