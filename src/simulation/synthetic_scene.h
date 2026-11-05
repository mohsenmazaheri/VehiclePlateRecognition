#pragma once
// A synthetic road with scripted vehicles. It drives tests and the UI demo without any model.
// It says nothing about real accuracy: its detectors return the scripted truth.
#include "domain/ports.h"

#include <memory>
#include <random>
#include <string>
#include <vector>

namespace vpr::sim {

struct SceneVehicle {
    std::string plate;
    std::int64_t enterMs = 0;
    double laneX = 0.0;          // left edge, normalized
    double speedPerMs = 0.0003;  // normalized frame heights per millisecond
    bool plateVisible = true;
};

struct VehiclePose {
    const SceneVehicle* vehicle = nullptr;
    RectF box;
    RectF plateBox;
};

class SyntheticScene {
public:
    SyntheticScene(int width, int height, std::vector<SceneVehicle> vehicles);

    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }
    [[nodiscard]] const std::vector<SceneVehicle>& vehicles() const { return m_vehicles; }
    [[nodiscard]] std::int64_t durationMs() const;
    [[nodiscard]] std::vector<VehiclePose> posesAt(std::int64_t timestampMs) const;

    // Two lanes with Armenian and Iranian plates, and one vehicle with a hidden plate.
    [[nodiscard]] static std::shared_ptr<SyntheticScene> demo(int width = 960, int height = 540);

private:
    int m_width;
    int m_height;
    std::vector<SceneVehicle> m_vehicles;
};

class SimVehicleDetector final : public IVehicleDetector {
public:
    explicit SimVehicleDetector(std::shared_ptr<const SyntheticScene> scene) : m_scene(std::move(scene)) {}
    [[nodiscard]] std::vector<Detection> detect(const Image& frame) override;

private:
    std::shared_ptr<const SyntheticScene> m_scene;
};

class SimPlateDetector final : public IPlateDetector {
public:
    explicit SimPlateDetector(std::shared_ptr<const SyntheticScene> scene) : m_scene(std::move(scene)) {}
    [[nodiscard]] std::vector<Detection> detect(const Image& vehicleCrop) override;

private:
    std::shared_ptr<const SyntheticScene> m_scene;
};

// errorRate: chance that one character of a reading is replaced, to exercise the voter.
class SimPlateReader final : public IPlateReader {
public:
    SimPlateReader(std::shared_ptr<const SyntheticScene> scene, double errorRate = 0.0, unsigned seed = 7);
    [[nodiscard]] std::optional<PlateReading> read(const Image& plateCrop) override;

private:
    std::shared_ptr<const SyntheticScene> m_scene;
    double m_errorRate;
    std::mt19937 m_random;
};

} // namespace vpr::sim
