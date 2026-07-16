#include "cursor_navigation.h"

namespace {

bool supportedDirection(CursorDirection direction) {
    switch (direction) {
        case CursorDirection::Up:
        case CursorDirection::Down:
        case CursorDirection::Left:
        case CursorDirection::Right:
            return true;
    }
    return false;
}

uint8_t distance(uint8_t a, uint8_t b) {
    return (a > b) ? static_cast<uint8_t>(a - b)
                   : static_cast<uint8_t>(b - a);
}

bool isForward(Square candidate, Square current,
               CursorDirection direction) {
    switch (direction) {
        case CursorDirection::Up:
            return candidate.row < current.row;
        case CursorDirection::Down:
            return candidate.row > current.row;
        case CursorDirection::Left:
            return candidate.col < current.col;
        case CursorDirection::Right:
            return candidate.col > current.col;
    }
    return false;
}

uint8_t perpendicularDistance(Square candidate, Square current,
                              CursorDirection direction) {
    switch (direction) {
        case CursorDirection::Up:
        case CursorDirection::Down:
            return distance(candidate.col, current.col);
        case CursorDirection::Left:
        case CursorDirection::Right:
            return distance(candidate.row, current.row);
    }
    return 0;
}

uint8_t forwardDistance(Square candidate, Square current,
                        CursorDirection direction) {
    switch (direction) {
        case CursorDirection::Up:
            return static_cast<uint8_t>(current.row - candidate.row);
        case CursorDirection::Down:
            return static_cast<uint8_t>(candidate.row - current.row);
        case CursorDirection::Left:
            return static_cast<uint8_t>(current.col - candidate.col);
        case CursorDirection::Right:
            return static_cast<uint8_t>(candidate.col - current.col);
    }
    return 0;
}

bool rowColumnLess(Square candidate, Square incumbent) {
    return candidate.row < incumbent.row ||
           (candidate.row == incumbent.row &&
            candidate.col < incumbent.col);
}

bool betterForwardCandidate(Square candidate, Square incumbent,
                            Square current, CursorDirection direction) {
    const uint8_t candidatePerpendicular =
        perpendicularDistance(candidate, current, direction);
    const uint8_t incumbentPerpendicular =
        perpendicularDistance(incumbent, current, direction);
    if (candidatePerpendicular != incumbentPerpendicular) {
        return candidatePerpendicular < incumbentPerpendicular;
    }

    const uint8_t candidateForward =
        forwardDistance(candidate, current, direction);
    const uint8_t incumbentForward =
        forwardDistance(incumbent, current, direction);
    if (candidateForward != incumbentForward) {
        return candidateForward < incumbentForward;
    }

    return rowColumnLess(candidate, incumbent);
}

uint8_t wrapCoordinate(Square candidate, CursorDirection direction) {
    switch (direction) {
        case CursorDirection::Up:
        case CursorDirection::Down:
            return candidate.row;
        case CursorDirection::Left:
        case CursorDirection::Right:
            return candidate.col;
    }
    return 0;
}

bool betterWrapCandidate(Square candidate, Square incumbent,
                         Square current, CursorDirection direction) {
    const uint8_t candidateEdge = wrapCoordinate(candidate, direction);
    const uint8_t incumbentEdge = wrapCoordinate(incumbent, direction);

    // Moving right/down wraps to the minimum coordinate. Moving left/up
    // wraps to the maximum coordinate.
    const bool wrapsToMinimum = direction == CursorDirection::Right ||
                                direction == CursorDirection::Down;
    if (candidateEdge != incumbentEdge) {
        return wrapsToMinimum ? candidateEdge < incumbentEdge
                              : candidateEdge > incumbentEdge;
    }

    const uint8_t candidatePerpendicular =
        perpendicularDistance(candidate, current, direction);
    const uint8_t incumbentPerpendicular =
        perpendicularDistance(incumbent, current, direction);
    if (candidatePerpendicular != incumbentPerpendicular) {
        return candidatePerpendicular < incumbentPerpendicular;
    }

    return rowColumnLess(candidate, incumbent);
}

} // namespace

bool chooseCursorDestination(const Square* cells, uint8_t count,
                             Square current, CursorDirection direction,
                             Square& next) {
    if (cells == nullptr || count == 0 || !current.valid() ||
        !supportedDirection(direction)) {
        return false;
    }

    bool hasAlternative = false;
    for (uint8_t i = 0; i < count; ++i) {
        if (cells[i].valid() && cells[i] != current) {
            hasAlternative = true;
            break;
        }
    }

    bool found = false;
    Square best;
    for (uint8_t i = 0; i < count; ++i) {
        const Square candidate = cells[i];
        if (!candidate.valid() || (hasAlternative && candidate == current) ||
            !isForward(candidate, current, direction)) {
            continue;
        }

        if (!found || betterForwardCandidate(candidate, best, current,
                                              direction)) {
            best = candidate;
            found = true;
        }
    }

    if (!found) {
        for (uint8_t i = 0; i < count; ++i) {
            const Square candidate = cells[i];
            if (!candidate.valid() ||
                (hasAlternative && candidate == current)) {
                continue;
            }

            if (!found || betterWrapCandidate(candidate, best, current,
                                               direction)) {
                best = candidate;
                found = true;
            }
        }
    }

    if (!found) return false;

    next = best;
    return true;
}
