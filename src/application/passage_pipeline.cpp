#include "passage_pipeline.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace vpr {

namespace {

constexpr std::size_t kVehiclesTakingTurns = 3;  // per allowed read

// Vehicle boxes overlap in traffic, so a crop often shows the plate of the vehicle behind or beside.
// A plate sits low on its vehicle, so it belongs to the box whose bottom edge it is relatively nearest.
bool belongsTo(const PointF& plateCentre, const Track& track, const std::vector<Track>& active)
{
    const auto heightAboveBottom = [&plateCentre](const RectF& box) {
        return (box.y + box.h - plateCentre.y) / box.h;
    };
    const double own = heightAboveBottom(track.box);
    for (const Track& other : active) {
        if (other.id == track.id || other.box.h <= 0.0)
            continue;
        const RectF& box = other.box;
        const bool inside = plateCentre.x >= box.x && plateCentre.x <= box.x + box.w && plateCentre.y >= box.y
                            && plateCentre.y <= box.y + box.h;
        if (inside && heightAboveBottom(box) < own)
            return false;
    }
    return true;
}

} // namespace

PassagePipeline::PassagePipeline(Dependencies deps, PlateFormatRegistry formats, PipelineConfig config)
    : m_deps(std::move(deps))
    , m_formats(std::move(formats))
    , m_config(config)
    , m_crossing(config.line)
{
    if (!m_deps.vehicleDetector || !m_deps.plateDetector || !m_deps.plateReader || !m_deps.tracker
        || !m_deps.sink) {
        throw std::invalid_argument("PassagePipeline: every dependency is required");
    }
    m_config.readEveryNFrames = std::max(1, m_config.readEveryNFrames);
}

TrackUpdate PassagePipeline::process(const Frame& frame)
{
    const Image& image = frame.image;
    TrackUpdate update = m_deps.tracker->update(m_deps.vehicleDetector->detect(image), image.timestampMs);

    for (const Track& track : update.active) {
        VehicleState& state = m_vehicles.try_emplace(track.id, &m_formats).first->second;
        // Until a plate is seen, the photo is the largest view of the vehicle so far.
        if (!state.emitted && state.plateScore < 0.0 && track.box.area() > state.largestArea) {
            state.largestArea = track.box.area();
            state.vehicleImage = image.crop(track.box);
        }
    }

    if (m_processedFrames++ % m_config.readEveryNFrames == 0) {
        for (const Track* track : tracksToRead(update.active, image))
            readPlate(image, *track, update.active, m_vehicles.at(track->id));
    }

    for (const LineCrossing& crossing : m_crossing.update(update.active, image.width, image.height)) {
        VehicleState& state = m_vehicles.at(crossing.trackId);
        state.crossed = true;
        state.direction = crossing.direction;
        state.crossingTimestampMs = image.timestampMs;
        ++m_counters.crossed;
    }

    for (const Track& track : update.active) {
        VehicleState& state = m_vehicles.at(track.id);
        if (!state.crossed || state.emitted)
            continue;
        const bool readAlready = m_config.reportWhenRead && state.voter.hasReadings() && state.voter.result()->valid;
        if (readAlready || state.framesSinceCrossing++ >= m_config.graceFrames)
            emitPassage(track.id, state);
    }

    for (const int id : update.removed) {
        const auto it = m_vehicles.find(id);
        if (it != m_vehicles.end()) {
            if (it->second.crossed && !it->second.emitted)
                emitPassage(id, it->second);
            m_vehicles.erase(it);
        }
        m_crossing.forget(id);
    }
    return update;
}

void PassagePipeline::flush()
{
    for (auto& [id, state] : m_vehicles) {
        if (state.crossed && !state.emitted)
            emitPassage(id, state);
    }
}

std::vector<const Track*> PassagePipeline::tracksToRead(const std::vector<Track>& active, const Image& image) const
{
    std::vector<const Track*> waiting;
    for (const Track& track : active) {
        if (!m_vehicles.at(track.id).emitted)
            waiting.push_back(&track);
    }
    const auto limit = static_cast<std::size_t>(m_config.maxPlateReadsPerFrame);
    if (limit == 0 || waiting.size() <= limit)
        return waiting;

    // Plates are largest near the line, and that is where the answer is due. The nearest vehicles
    // take turns, so that a queue standing at the line cannot keep one vehicle from being read.
    const CountingLine& line = m_crossing.line();
    const double ax = line.a.x * image.width;
    const double ay = line.a.y * image.height;
    const double dx = (line.b.x - line.a.x) * image.width;
    const double dy = (line.b.y - line.a.y) * image.height;
    const auto distance = [&](const Track* track) {
        const PointF p = track->box.bottomCentre();
        return std::abs(dx * (p.y - ay) - dy * (p.x - ax));
    };
    std::sort(waiting.begin(), waiting.end(),
              [&](const Track* a, const Track* b) { return distance(a) < distance(b); });

    const std::size_t nearest = std::min(waiting.size(), limit * kVehiclesTakingTurns);
    std::stable_sort(waiting.begin(), waiting.begin() + static_cast<std::ptrdiff_t>(nearest),
                     [this](const Track* a, const Track* b) {
                         return m_vehicles.at(a->id).lastReadFrame < m_vehicles.at(b->id).lastReadFrame;
                     });
    waiting.resize(limit);
    return waiting;
}

void PassagePipeline::readPlate(const Image& frameImage, const Track& track, const std::vector<Track>& active,
                                VehicleState& state)
{
    state.lastReadFrame = m_processedFrames;
    const Image vehicle = frameImage.crop(track.box);
    if (vehicle.width <= 0 || vehicle.height <= 0)
        return;

    const Detection* best = nullptr;
    const auto plates = m_deps.plateDetector->detect(vehicle);
    for (const Detection& candidate : plates) {
        if (candidate.confidence < m_config.minPlateConfidence)
            continue;
        const PointF centre{vehicle.originX - frameImage.originX + candidate.box.centre().x,
                            vehicle.originY - frameImage.originY + candidate.box.centre().y};
        if (belongsTo(centre, track, active) && (!best || candidate.confidence > best->confidence))
            best = &candidate;
    }
    if (!best)
        return;

    const Image plate = vehicle.crop(best->box);
    if (plate.width <= 0 || plate.height <= 0)
        return;

    // The largest clear view of the plate also gives the vehicle photo, which then shows the plate.
    const double score = best->box.area() * best->confidence;
    if (score > state.plateScore) {
        state.plateScore = score;
        state.plateImage = plate;
        state.vehicleImage = vehicle;
    }
    if (const auto reading = m_deps.plateReader->read(plate); reading && reading->confidence >= m_config.minReadingConfidence)
        state.voter.add(*reading);
}

void PassagePipeline::emitPassage(int trackId, VehicleState& state)
{
    state.emitted = true;

    PassageEvent event;
    event.timestampMs = state.crossingTimestampMs;
    event.trackId = trackId;
    event.direction = state.direction;
    event.vehicleImage = state.vehicleImage;
    event.plateImage = state.plateImage;

    if (const auto vote = state.voter.result(); vote && vote->valid) {
        event.status = PassageStatus::Read;
        event.plateText = vote->canonical;
        event.displayText = vote->display;
        event.countryCode = vote->countryCode;
        event.confidence = vote->confidence;
        ++m_counters.read;
    } else if (vote) {
        event.status = PassageStatus::UnknownFormat;
        event.plateText = vote->canonical;
        event.displayText = vote->display;
        event.confidence = vote->confidence;
        ++m_counters.unknownFormat;
    } else {
        event.status = PassageStatus::NotVisible;
        ++m_counters.notVisible;
    }
    m_deps.sink->onPassage(event);
}

} // namespace vpr
