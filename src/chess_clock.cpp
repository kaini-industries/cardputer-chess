#include "chess_clock.h"

#include <limits>

ChessClock::ChessClock(TimeControl timeControl) {
    configure(timeControl);
}

void ChessClock::configure(TimeControl timeControl) {
    m_timeControl = timeControl;
    const TimeControlParams params = getTimeControlParams(timeControl);
    m_initialMs = params.initialMs;
    m_incrementMs = params.incrementMs;
    m_remainingMs[0] = m_initialMs;
    m_remainingMs[1] = m_initialMs;
    m_activeSide = PieceColor::White;
    m_anchorTimestampMs = 0;
    m_started = false;
    m_paused = false;
    m_hasFlaggedSide = false;
    m_flaggedSide = PieceColor::White;
    m_hasLastAddTimeEvent = false;
    m_lastAddTimeEventId = 0;
}

bool ChessClock::isRunning() const {
    return enabled() && m_started && !m_paused && !m_hasFlaggedSide;
}

bool ChessClock::start(uint32_t readyTimestampMs) {
    if (!enabled() || m_started) return false;

    m_activeSide = PieceColor::White;
    m_anchorTimestampMs = readyTimestampMs;
    m_started = true;
    m_paused = false;
    m_hasFlaggedSide = false;
    return true;
}

bool ChessClock::pause(uint32_t nowMs) {
    if (!isRunning()) return false;

    settle(nowMs);
    if (m_hasFlaggedSide) return false;
    m_paused = true;
    return true;
}

bool ChessClock::resume(uint32_t nowMs) {
    if (!enabled() || !m_started || !m_paused || m_hasFlaggedSide) {
        return false;
    }

    m_paused = false;
    reanchor(nowMs);
    return true;
}

void ChessClock::update(uint32_t nowMs) {
    settle(nowMs);
}

ChessClockMoveResult ChessClock::commitMove(PieceColor mover,
                                            uint32_t moveTimestampMs) {
    if (!enabled()) return ChessClockMoveResult::Disabled;
    if (!m_started) return ChessClockMoveResult::NotStarted;
    if (m_hasFlaggedSide) return ChessClockMoveResult::Flagged;
    if (m_paused) return ChessClockMoveResult::Paused;
    if (mover != m_activeSide) return ChessClockMoveResult::WrongSide;

    settle(moveTimestampMs);
    if (m_hasFlaggedSide) return ChessClockMoveResult::Flagged;

    const uint8_t moverIndex = sideIndex(mover);
    m_remainingMs[moverIndex] =
        saturatingAdd(m_remainingMs[moverIndex], m_incrementMs);
    m_activeSide = opponent(mover);
    reanchor(moveTimestampMs);
    return ChessClockMoveResult::Applied;
}

uint32_t ChessClock::remainingMs(PieceColor side) const {
    return m_remainingMs[sideIndex(side)];
}

uint32_t ChessClock::remainingMsAt(PieceColor side, uint32_t nowMs) const {
    const uint32_t stored = remainingMs(side);
    if (!isRunning() || side != m_activeSide) return stored;

    const uint32_t elapsed = nowMs - m_anchorTimestampMs;
    return (elapsed >= stored) ? 0 : stored - elapsed;
}

bool ChessClock::flagged(PieceColor side) const {
    return m_hasFlaggedSide && m_flaggedSide == side;
}

bool ChessClock::flaggedAt(PieceColor side, uint32_t nowMs) const {
    if (flagged(side)) return true;
    return isRunning() && side == m_activeSide &&
           remainingMsAt(side, nowMs) == 0;
}

bool ChessClock::setRemainingMs(PieceColor side, uint32_t remaining,
                                uint32_t nowMs) {
    if (!enabled()) return false;

    settle(nowMs);
    m_remainingMs[sideIndex(side)] = remaining;
    reconcileAuthoritativeTimes(nowMs);
    return true;
}

bool ChessClock::synchronizeRemainingMs(uint32_t whiteRemaining,
                                        uint32_t blackRemaining,
                                        uint32_t nowMs) {
    if (!enabled()) return false;

    settle(nowMs);
    m_remainingMs[sideIndex(PieceColor::White)] = whiteRemaining;
    m_remainingMs[sideIndex(PieceColor::Black)] = blackRemaining;
    reconcileAuthoritativeTimes(nowMs);
    return true;
}

bool ChessClock::addTimeOnce(PieceColor recipient, uint32_t amountMs,
                             uint32_t eventId, uint32_t nowMs) {
    if (!enabled() || !m_started || m_hasFlaggedSide) return false;
    if (m_hasLastAddTimeEvent &&
        !sequenceAfter(eventId, m_lastAddTimeEventId)) {
        return false;
    }

    settle(nowMs);
    if (m_hasFlaggedSide) return false;

    const uint8_t recipientIndex = sideIndex(recipient);
    m_remainingMs[recipientIndex] =
        saturatingAdd(m_remainingMs[recipientIndex], amountMs);
    m_hasLastAddTimeEvent = true;
    m_lastAddTimeEventId = eventId;
    reanchor(nowMs);
    return true;
}

ChessClockSnapshot ChessClock::snapshot(uint32_t nowMs) const {
    ChessClockSnapshot result;
    result.timeControl = m_timeControl;
    result.whiteRemainingMs = remainingMsAt(PieceColor::White, nowMs);
    result.blackRemainingMs = remainingMsAt(PieceColor::Black, nowMs);
    result.activeSide = m_activeSide;
    result.started = m_started;
    result.paused = m_paused;
    result.hasFlaggedSide = m_hasFlaggedSide;
    result.flaggedSide = m_flaggedSide;
    result.hasLastAddTimeEvent = m_hasLastAddTimeEvent;
    result.lastAddTimeEventId = m_lastAddTimeEventId;

    // A const snapshot still needs to capture a flag that has elapsed since
    // the caller last invoked update().
    if (!result.hasFlaggedSide && isRunning() &&
        remainingMsAt(m_activeSide, nowMs) == 0) {
        result.hasFlaggedSide = true;
        result.flaggedSide = m_activeSide;
        result.paused = false;
    }
    return result;
}

bool ChessClock::loadSnapshot(const ChessClockSnapshot& state,
                              uint32_t resumeTimestampMs) {
    const uint8_t timeControlValue = static_cast<uint8_t>(state.timeControl);
    if (timeControlValue > static_cast<uint8_t>(TimeControl::Rapid10)) {
        return false;
    }
    if (state.activeSide != PieceColor::White &&
        state.activeSide != PieceColor::Black) {
        return false;
    }
    if (state.flaggedSide != PieceColor::White &&
        state.flaggedSide != PieceColor::Black) {
        return false;
    }
    if ((!state.started && (state.paused || state.hasFlaggedSide)) ||
        (state.paused && state.hasFlaggedSide)) {
        return false;
    }
    if (state.hasFlaggedSide) {
        const uint32_t flaggedRemaining =
            (state.flaggedSide == PieceColor::White)
                ? state.whiteRemainingMs
                : state.blackRemainingMs;
        if (flaggedRemaining != 0) return false;
    }

    configure(state.timeControl);
    if (!enabled()) {
        // TimeControl::None has one canonical, inert state.
        return !state.started && !state.paused && !state.hasFlaggedSide;
    }

    m_remainingMs[sideIndex(PieceColor::White)] = state.whiteRemainingMs;
    m_remainingMs[sideIndex(PieceColor::Black)] = state.blackRemainingMs;
    m_activeSide = state.activeSide;
    m_started = state.started;
    m_paused = state.paused;
    m_hasFlaggedSide = state.hasFlaggedSide;
    m_flaggedSide = state.flaggedSide;
    m_hasLastAddTimeEvent = state.hasLastAddTimeEvent;
    m_lastAddTimeEventId = state.lastAddTimeEventId;
    m_anchorTimestampMs = resumeTimestampMs;

    if (m_started && !m_hasFlaggedSide &&
        (m_remainingMs[0] == 0 || m_remainingMs[1] == 0)) {
        // A zero clock in a started game is terminal even if an older save did
        // not persist an explicit flag bit.
        m_hasFlaggedSide = true;
        m_flaggedSide = (m_remainingMs[sideIndex(m_activeSide)] == 0)
                            ? m_activeSide
                            : opponent(m_activeSide);
        m_paused = false;
    }
    return true;
}

uint8_t ChessClock::sideIndex(PieceColor side) {
    return (side == PieceColor::White) ? 0 : 1;
}

uint32_t ChessClock::saturatingAdd(uint32_t value, uint32_t addition) {
    const uint32_t maximum = std::numeric_limits<uint32_t>::max();
    return (addition > maximum - value) ? maximum : value + addition;
}

bool ChessClock::sequenceAfter(uint32_t candidate, uint32_t reference) {
    const uint32_t distance = candidate - reference;
    return distance != 0 && distance < 0x80000000u;
}

void ChessClock::settle(uint32_t nowMs) {
    if (!isRunning()) return;

    const uint8_t activeIndex = sideIndex(m_activeSide);
    const uint32_t elapsed = nowMs - m_anchorTimestampMs;
    if (elapsed >= m_remainingMs[activeIndex]) {
        m_remainingMs[activeIndex] = 0;
        m_hasFlaggedSide = true;
        m_flaggedSide = m_activeSide;
        m_paused = false;
    } else {
        m_remainingMs[activeIndex] -= elapsed;
        reanchor(nowMs);
    }
}

void ChessClock::reanchor(uint32_t nowMs) {
    m_anchorTimestampMs = nowMs;
}

void ChessClock::reconcileAuthoritativeTimes(uint32_t nowMs) {
    m_hasFlaggedSide = false;
    if (m_started) {
        const uint8_t activeIndex = sideIndex(m_activeSide);
        const uint8_t inactiveIndex = sideIndex(opponent(m_activeSide));
        if (m_remainingMs[activeIndex] == 0) {
            m_hasFlaggedSide = true;
            m_flaggedSide = m_activeSide;
        } else if (m_remainingMs[inactiveIndex] == 0) {
            m_hasFlaggedSide = true;
            m_flaggedSide = opponent(m_activeSide);
        }
    }

    if (m_hasFlaggedSide) m_paused = false;
    reanchor(nowMs);
}
