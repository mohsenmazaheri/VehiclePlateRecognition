#pragma once
#include "domain/ports.h"

#include <vector>

namespace vpr {

struct TrackerConfig {
    float highConfidence = 0.5F;  // only these can start a new track
    float lowConfidence = 0.1F;   // below this a detection is ignored
    double minIou = 0.2;
    int confirmHits = 3;          // hits before a track is reported, filters one frame false positives
    std::int64_t maxLostMs = 1000;
};

// ByteTrack style association: high confidence detections first, then low confidence ones can
// still keep an existing track alive, which is what holds a vehicle through partial occlusion.
// Uses a constant velocity prediction per millisecond, so dropped frames do not break tracks.
class ByteTracker final : public ITracker {
public:
    explicit ByteTracker(TrackerConfig config = {});
    [[nodiscard]] TrackUpdate update(const std::vector<Detection>& detections,
                                     std::int64_t timestampMs) override;

private:
    struct Internal {
        int id = 0;
        RectF box;
        RectF predicted;
        double vx = 0.0;
        double vy = 0.0;
        float confidence = 0.0F;
        int hits = 0;
        bool confirmed = false;
        std::int64_t lastSeenMs = 0;
        bool matchedNow = false;
    };

    void associate(const std::vector<Detection>& detections, std::vector<bool>& used,
                   bool highPass, std::int64_t timestampMs);

    TrackerConfig m_config;
    std::vector<Internal> m_tracks;
    int m_nextId = 1;
};

} // namespace vpr
