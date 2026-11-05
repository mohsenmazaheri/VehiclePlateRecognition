#include "byte_tracker.h"

#include <algorithm>
#include <tuple>

namespace vpr {

ByteTracker::ByteTracker(TrackerConfig config) : m_config(config) {}

void ByteTracker::associate(const std::vector<Detection>& detections, std::vector<bool>& used,
                            bool highPass, std::int64_t timestampMs)
{
    std::vector<std::tuple<double, std::size_t, std::size_t>> pairs;
    for (std::size_t t = 0; t < m_tracks.size(); ++t) {
        if (m_tracks[t].matchedNow)
            continue;
        for (std::size_t d = 0; d < detections.size(); ++d) {
            if (used[d])
                continue;
            const bool isHigh = detections[d].confidence >= m_config.highConfidence;
            if (isHigh != highPass || detections[d].confidence < m_config.lowConfidence)
                continue;
            const double iou = intersectionOverUnion(m_tracks[t].predicted, detections[d].box);
            if (iou >= m_config.minIou)
                pairs.emplace_back(iou, t, d);
        }
    }
    std::sort(pairs.begin(), pairs.end(),
              [](const auto& a, const auto& b) { return std::get<0>(a) > std::get<0>(b); });

    for (const auto& [iou, t, d] : pairs) {
        Internal& track = m_tracks[t];
        if (track.matchedNow || used[d])
            continue;
        const auto dt = static_cast<double>(timestampMs - track.lastSeenMs);
        if (dt > 0.0) {
            const PointF before = track.box.centre();
            const PointF now = detections[d].box.centre();
            const double alpha = track.hits <= 1 ? 1.0 : 0.5;
            track.vx = alpha * (now.x - before.x) / dt + (1.0 - alpha) * track.vx;
            track.vy = alpha * (now.y - before.y) / dt + (1.0 - alpha) * track.vy;
        }
        track.box = detections[d].box;
        track.confidence = detections[d].confidence;
        track.lastSeenMs = timestampMs;
        track.matchedNow = true;
        ++track.hits;
        track.confirmed = track.confirmed || track.hits >= m_config.confirmHits;
        used[d] = true;
    }
}

TrackUpdate ByteTracker::update(const std::vector<Detection>& detections, std::int64_t timestampMs)
{
    for (Internal& track : m_tracks) {
        const auto dt = static_cast<double>(timestampMs - track.lastSeenMs);
        track.predicted = track.box;
        track.predicted.x += track.vx * dt;
        track.predicted.y += track.vy * dt;
        track.matchedNow = false;
    }

    std::vector<bool> used(detections.size(), false);
    associate(detections, used, true, timestampMs);
    associate(detections, used, false, timestampMs);

    for (std::size_t d = 0; d < detections.size(); ++d) {
        if (used[d] || detections[d].confidence < m_config.highConfidence)
            continue;
        Internal track;
        track.id = m_nextId++;
        track.box = detections[d].box;
        track.predicted = track.box;
        track.confidence = detections[d].confidence;
        track.hits = 1;
        track.confirmed = m_config.confirmHits <= 1;
        track.lastSeenMs = timestampMs;
        track.matchedNow = true;
        m_tracks.push_back(track);
    }

    TrackUpdate out;
    for (const Internal& track : m_tracks) {
        if (track.matchedNow && track.confirmed)
            out.active.push_back({track.id, track.box, track.confidence});
    }
    const auto expired = [&](const Internal& track) {
        return !track.matchedNow && timestampMs - track.lastSeenMs > m_config.maxLostMs;
    };
    for (const Internal& track : m_tracks) {
        if (expired(track) && track.confirmed)
            out.removed.push_back(track.id);
    }
    m_tracks.erase(std::remove_if(m_tracks.begin(), m_tracks.end(), expired), m_tracks.end());
    return out;
}

} // namespace vpr
