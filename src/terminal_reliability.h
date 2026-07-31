#ifndef TERMINAL_RELIABILITY_H
#define TERMINAL_RELIABILITY_H

#include "chess_net_protocol.h"
#include "game_records.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace TerminalReliability {

// Compare serialized GameSummary fields explicitly instead of comparing the
// struct representation, whose padding is not part of the persisted result.
inline bool summariesExactlyEqual(const GameSummary& lhs,
                                  const GameSummary& rhs) noexcept {
    return lhs.gameId == rhs.gameId &&
           lhs.whiteProfileId == rhs.whiteProfileId &&
           lhs.blackProfileId == rhs.blackProfileId &&
           std::memcmp(lhs.whiteName, rhs.whiteName,
                       sizeof(lhs.whiteName)) == 0 &&
           std::memcmp(lhs.blackName, rhs.blackName,
                       sizeof(lhs.blackName)) == 0 &&
           lhs.mode == rhs.mode &&
           lhs.variant == rhs.variant &&
           lhs.positionIndex == rhs.positionIndex &&
           lhs.timeControl == rhs.timeControl &&
           lhs.aiDifficulty == rhs.aiDifficulty &&
           lhs.localColor == rhs.localColor &&
           lhs.outcome == rhs.outcome &&
           lhs.termination == rhs.termination &&
           lhs.plyCount == rhs.plyCount &&
           lhs.whiteRemainingMs == rhs.whiteRemainingMs &&
           lhs.blackRemainingMs == rhs.blackRemainingMs;
}

enum class ResultCommitDecision : uint8_t {
    RecordAbsent,
    AlreadyCommitted,
    Conflict,
    InvalidInput,
};

// Classify an immutable candidate before mutating profile history. A repeated
// gameId is idempotent only when every persisted summary field is identical.
inline ResultCommitDecision decideResultCommit(
        const GameSummary* history, std::size_t historyCount,
        const GameSummary& candidate) noexcept {
    if (candidate.gameId == 0 ||
        (history == nullptr && historyCount != 0)) {
        return ResultCommitDecision::InvalidInput;
    }

    bool foundExact = false;
    for (std::size_t i = 0; i < historyCount; ++i) {
        if (history[i].gameId != candidate.gameId) continue;
        if (!summariesExactlyEqual(history[i], candidate)) {
            return ResultCommitDecision::Conflict;
        }
        foundExact = true;
    }
    return foundExact ? ResultCommitDecision::AlreadyCommitted
                      : ResultCommitDecision::RecordAbsent;
}

inline ResultCommitDecision decideResultCommit(
        const ProfileData& profiles,
        const GameSummary& candidate) noexcept {
    if (profiles.historyCount > MAX_GAME_SUMMARIES) {
        return ResultCommitDecision::InvalidInput;
    }
    return decideResultCommit(profiles.history, profiles.historyCount,
                              candidate);
}

// Owns the complete packet captured at the causal event. Promotion returns a
// copy, so delayed sending cannot resnapshot a mutable board or clock.
class ImmutableQueuedControl {
public:
    explicit ImmutableQueuedControl(const ControlNetMsg& captured) noexcept
        : m_captured(captured) {}

    const ControlNetMsg& captured() const noexcept { return m_captured; }

    ControlNetMsg promote() const noexcept { return m_captured; }

    // A receiver that did not consume the preceding control still expects that
    // event ID. Reuse changes only the ordering token; the captured terminal
    // payload, board hash, and clock values remain immutable.
    ControlNetMsg promoteReusingEventId(uint16_t eventId) const noexcept {
        ControlNetMsg promoted = m_captured;
        promoted.eventId = eventId;
        return promoted;
    }

private:
    ControlNetMsg m_captured;
};

} // namespace TerminalReliability

#endif // TERMINAL_RELIABILITY_H
