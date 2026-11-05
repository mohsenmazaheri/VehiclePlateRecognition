#pragma once
#include "geometry.h"
#include "image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vpr {

struct Frame {
    Image image;
    std::int64_t index = 0;
};

struct Detection {
    RectF box;
    float confidence = 0.0F;
};

struct Track {
    int id = 0;
    RectF box;
    float confidence = 0.0F;
};

struct TrackUpdate {
    std::vector<Track> active;  // confirmed tracks matched in this frame
    std::vector<int> removed;   // tracks that ended in this frame
};

struct PlateReading {
    std::string text;           // UTF-8, as produced by the reader
    float confidence = 0.0F;
};

enum class PassageStatus {
    Read,           // plate read and valid for a configured country format
    UnknownFormat,  // something was read, but it fits no configured format
    NotVisible      // the vehicle crossed the line and no plate text was obtained
};

enum class CrossingDirection { Forward, Backward };

struct PassageEvent {
    std::int64_t timestampMs = 0;
    int trackId = 0;
    PassageStatus status = PassageStatus::NotVisible;
    CrossingDirection direction = CrossingDirection::Forward;
    std::string plateText;      // canonical text, without separators
    std::string displayText;    // for people, for example "12 AB 345"
    std::string countryCode;    // "AM", "IR" or empty
    float confidence = 0.0F;
    Image vehicleImage;
    Image plateImage;
};

} // namespace vpr
