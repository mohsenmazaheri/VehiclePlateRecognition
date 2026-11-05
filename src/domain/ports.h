#pragma once
// Ports: the only way the application layer reaches the outside world.
// Each interface has one job (ISP), and the core depends on these, never on an adapter (DIP).
#include "types.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vpr {

class IVehicleDetector {
public:
    virtual ~IVehicleDetector() = default;
    [[nodiscard]] virtual std::vector<Detection> detect(const Image& frame) = 0;
};

class IPlateDetector {
public:
    virtual ~IPlateDetector() = default;
    // Boxes are relative to the given vehicle crop.
    [[nodiscard]] virtual std::vector<Detection> detect(const Image& vehicleCrop) = 0;
};

class IPlateReader {
public:
    virtual ~IPlateReader() = default;
    [[nodiscard]] virtual std::optional<PlateReading> read(const Image& plateCrop) = 0;
};

class ITracker {
public:
    virtual ~ITracker() = default;
    [[nodiscard]] virtual TrackUpdate update(const std::vector<Detection>& detections,
                                             std::int64_t timestampMs) = 0;
};

// Any camera or video: file, webcam, RTSP, vendor SDK. Implement this to add a new one.
class IFrameSource {
public:
    using FrameHandler = std::function<void(const Frame&)>;
    using ErrorHandler = std::function<void(const std::string&)>;
    using FinishedHandler = std::function<void()>;

    virtual ~IFrameSource() = default;
    // onFrame may run on any thread. onFinished runs when a finite source (a file) ends.
    virtual void start(FrameHandler onFrame, ErrorHandler onError, FinishedHandler onFinished) = 0;
    virtual void stop() = 0;
};

class IPassageSink {
public:
    virtual ~IPassageSink() = default;
    virtual void onPassage(const PassageEvent& event) = 0;
};

struct PassageQuery {
    std::string plateContains;
    int limit = 200;
};

struct StoredPassage {
    std::int64_t id = 0;
    std::int64_t timestampMs = 0;
    PassageStatus status = PassageStatus::NotVisible;
    std::string plateText;
    std::string displayText;
    std::string countryCode;
    float confidence = 0.0F;
    std::string vehicleImagePath;
};

class IPassageRepository {
public:
    virtual ~IPassageRepository() = default;
    [[nodiscard]] virtual std::vector<StoredPassage> search(const PassageQuery& query) = 0;
    // Plates are personal data: retention is a first class operation. Returns rows removed.
    virtual int purgeOlderThan(std::int64_t timestampMs) = 0;
};

} // namespace vpr
