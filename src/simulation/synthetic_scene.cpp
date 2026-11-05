#include "synthetic_scene.h"

#include "domain/plate_format.h"

#include <algorithm>

namespace vpr::sim {

namespace {
constexpr double kCarWidth = 0.16;
constexpr double kCarHeight = 0.24;

const VehiclePose* poseContaining(const std::vector<VehiclePose>& poses, const Image& crop)
{
    const double cx = crop.originX + crop.width / 2.0;
    const double cy = crop.originY + crop.height / 2.0;
    for (const VehiclePose& pose : poses) {
        const RectF& b = pose.box;
        if (cx >= b.x && cx <= b.x + b.w && cy >= b.y && cy <= b.y + b.h)
            return &pose;
    }
    return nullptr;
}
} // namespace

SyntheticScene::SyntheticScene(int width, int height, std::vector<SceneVehicle> vehicles)
    : m_width(width), m_height(height), m_vehicles(std::move(vehicles))
{
}

std::int64_t SyntheticScene::durationMs() const
{
    std::int64_t end = 0;
    for (const SceneVehicle& v : m_vehicles)
        end = std::max(end, v.enterMs + static_cast<std::int64_t>((1.0 + kCarHeight) / v.speedPerMs));
    return end;
}

std::vector<VehiclePose> SyntheticScene::posesAt(std::int64_t timestampMs) const
{
    std::vector<VehiclePose> poses;
    const double carW = kCarWidth * m_width;
    const double carH = kCarHeight * m_height;
    for (const SceneVehicle& v : m_vehicles) {
        const double travelled = static_cast<double>(timestampMs - v.enterMs) * v.speedPerMs * m_height;
        const double top = travelled - carH;
        if (timestampMs < v.enterMs || top >= m_height)
            continue;
        VehiclePose pose;
        pose.vehicle = &v;
        pose.box = {v.laneX * m_width, top, carW, carH};
        pose.plateBox = {pose.box.x + carW * 0.3, top + carH * 0.78, carW * 0.4, carH * 0.14};
        poses.push_back(pose);
    }
    return poses;
}

std::shared_ptr<SyntheticScene> SyntheticScene::demo(int width, int height)
{
    const std::string iranA = encodeUtf8(U"12ب۳۴۵۶۷");
    const std::string iranB = encodeUtf8(U"77م۱۲۳۱۳");
    return std::make_shared<SyntheticScene>(width, height, std::vector<SceneVehicle>{
        {"34AB567", 0, 0.20, 0.00030, true},
        {iranA, 1500, 0.60, 0.00034, true},
        {"70XY123", 4000, 0.20, 0.00028, true},
        {iranB, 5200, 0.60, 0.00030, true},
        {"05KM909", 8000, 0.40, 0.00036, false},
    });
}

std::vector<Detection> SimVehicleDetector::detect(const Image& frame)
{
    std::vector<Detection> out;
    for (const VehiclePose& pose : m_scene->posesAt(frame.timestampMs))
        out.push_back({pose.box, 0.9F});
    return out;
}

std::vector<Detection> SimPlateDetector::detect(const Image& vehicleCrop)
{
    const auto poses = m_scene->posesAt(vehicleCrop.timestampMs);
    const VehiclePose* pose = poseContaining(poses, vehicleCrop);
    if (!pose || !pose->vehicle->plateVisible)
        return {};
    const RectF box{pose->plateBox.x - vehicleCrop.originX, pose->plateBox.y - vehicleCrop.originY,
                    pose->plateBox.w, pose->plateBox.h};
    return {Detection{box, 0.9F}};
}

SimPlateReader::SimPlateReader(std::shared_ptr<const SyntheticScene> scene, double errorRate, unsigned seed)
    : m_scene(std::move(scene)), m_errorRate(errorRate), m_random(seed)
{
}

std::optional<PlateReading> SimPlateReader::read(const Image& plateCrop)
{
    const auto poses = m_scene->posesAt(plateCrop.timestampMs);
    const VehiclePose* pose = poseContaining(poses, plateCrop);
    if (!pose)
        return std::nullopt;

    std::u32string text = PlateFormatRegistry::normalize(pose->vehicle->plate);
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    if (!text.empty() && chance(m_random) < m_errorRate) {
        std::uniform_int_distribution<std::size_t> position(0, text.size() - 1);
        text[position(m_random)] = U'8';
    }
    return PlateReading{encodeUtf8(text), 0.8F};
}

} // namespace vpr::sim
