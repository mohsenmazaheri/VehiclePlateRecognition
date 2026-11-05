#include "line_crossing.h"

namespace vpr {

LineCrossingDetector::LineCrossingDetector(CountingLine line) : m_line(line) {}

void LineCrossingDetector::setLine(const CountingLine& line)
{
    m_line = line;
    // Sides were measured against the old line. Crossed tracks stay crossed, so that
    // moving the line can never produce a second event for the same vehicle.
    for (auto& [id, state] : m_states)
        state.side = 0;
}

std::vector<LineCrossing> LineCrossingDetector::update(const std::vector<Track>& tracks,
                                                       int frameWidth, int frameHeight)
{
    std::vector<LineCrossing> crossings;
    if (frameWidth <= 0 || frameHeight <= 0)
        return crossings;

    const double ax = m_line.a.x * frameWidth;
    const double ay = m_line.a.y * frameHeight;
    const double dx = (m_line.b.x - m_line.a.x) * frameWidth;
    const double dy = (m_line.b.y - m_line.a.y) * frameHeight;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0)
        return crossings;

    for (const Track& track : tracks) {
        const PointF p = track.box.bottomCentre();
        const double cross = dx * (p.y - ay) - dy * (p.x - ax);
        const int side = cross > 0.0 ? 1 : (cross < 0.0 ? -1 : 0);
        const double along = ((p.x - ax) * dx + (p.y - ay) * dy) / lengthSquared;

        State& state = m_states[track.id];
        if (side == 0)
            continue;
        if (state.side != 0 && side != state.side && !state.crossed && along >= 0.0 && along <= 1.0) {
            state.crossed = true;
            crossings.push_back({track.id, side > 0 ? CrossingDirection::Forward
                                                    : CrossingDirection::Backward});
        }
        state.side = side;
    }
    return crossings;
}

void LineCrossingDetector::forget(int trackId)
{
    m_states.erase(trackId);
}

} // namespace vpr
