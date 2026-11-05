#pragma once
#include "domain/plate_format.h"
#include "domain/ports.h"
#include "line_crossing.h"
#include "plate_voter.h"

#include <memory>
#include <unordered_map>

namespace vpr {

struct PipelineConfig {
    CountingLine line;
    // Plate reading is the slow part. Reading too much starves the tracker, which then loses
    // vehicles, so these two bound its cost. They count processed frames, not source frames.
    int readEveryNFrames = 2;
    int maxPlateReadsPerFrame = 0;  // 0 means every vehicle
    int graceFrames = 5;            // keep reading after the crossing, the plate is often sharpest here
    // Report at the crossing itself when the plate is already read in a configured format, and wait
    // up to graceFrames only for vehicles that have no valid reading yet. For live displays.
    bool reportWhenRead = false;
    float minPlateConfidence = 0.25F;
    // Readings below this are dropped. A blurred plate is often read as a different plate that still
    // fits the format, and "no plate read" is better than a wrong number. 0 keeps every reading.
    float minReadingConfidence = 0.0F;
};

struct PassageCounters {
    long crossed = 0;
    long read = 0;
    long unknownFormat = 0;
    long notVisible = 0;
};

// The use case: frame in, tracked vehicles, one passage event per vehicle at the line.
// Knows only ports, so cameras, models and storage can change without touching it (DIP, OCP).
class PassagePipeline {
public:
    struct Dependencies {
        std::shared_ptr<IVehicleDetector> vehicleDetector;
        std::shared_ptr<IPlateDetector> plateDetector;
        std::shared_ptr<IPlateReader> plateReader;
        std::shared_ptr<ITracker> tracker;
        std::shared_ptr<IPassageSink> sink;
    };

    PassagePipeline(Dependencies deps, PlateFormatRegistry formats, PipelineConfig config = {});

    TrackUpdate process(const Frame& frame);
    void flush();  // emits vehicles that crossed and are still waiting, call at end of stream

    void setLine(const CountingLine& line) { m_crossing.setLine(line); }
    [[nodiscard]] const CountingLine& line() const { return m_crossing.line(); }
    [[nodiscard]] const PassageCounters& counters() const { return m_counters; }

private:
    struct VehicleState {
        explicit VehicleState(const PlateFormatRegistry* formats) : voter(formats) {}
        PlateVoter voter;
        Image vehicleImage;
        Image plateImage;
        double plateScore = -1.0;       // area x confidence of the best plate view so far
        double largestArea = 0.0;
        std::int64_t lastReadFrame = -1;
        bool crossed = false;
        bool emitted = false;
        int framesSinceCrossing = 0;
        std::int64_t crossingTimestampMs = 0;
        CrossingDirection direction = CrossingDirection::Forward;
    };

    [[nodiscard]] std::vector<const Track*> tracksToRead(const std::vector<Track>& active, const Image& image) const;
    void readPlate(const Image& frameImage, const Track& track, const std::vector<Track>& active, VehicleState& state);
    void emitPassage(int trackId, VehicleState& state);

    Dependencies m_deps;
    PlateFormatRegistry m_formats;
    PipelineConfig m_config;
    LineCrossingDetector m_crossing;
    PassageCounters m_counters;
    std::int64_t m_processedFrames = 0;
    std::unordered_map<int, VehicleState> m_vehicles;
};

} // namespace vpr
