#pragma once
#include "domain/types.h"

#include <unordered_map>
#include <vector>

namespace vpr {

// Counting line in normalized coordinates (0 to 1), so it survives a resolution change.
struct CountingLine {
    PointF a{0.0, 0.8};
    PointF b{1.0, 0.8};
};

struct LineCrossing {
    int trackId = 0;
    CrossingDirection direction = CrossingDirection::Forward;
};

// Reports one crossing per track, from the side change of the bottom centre of its box.
// A side test still works when frames are dropped, unlike testing for a touch on the line.
class LineCrossingDetector {
public:
    explicit LineCrossingDetector(CountingLine line = {});

    void setLine(const CountingLine& line);
    [[nodiscard]] const CountingLine& line() const { return m_line; }

    [[nodiscard]] std::vector<LineCrossing> update(const std::vector<Track>& tracks,
                                                   int frameWidth, int frameHeight);
    void forget(int trackId);

private:
    struct State {
        int side = 0;
        bool crossed = false;
    };

    CountingLine m_line;
    std::unordered_map<int, State> m_states;
};

} // namespace vpr
