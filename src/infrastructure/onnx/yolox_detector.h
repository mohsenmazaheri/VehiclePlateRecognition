#pragma once
#include "domain/ports.h"
#include "yolox_core.h"

#include <filesystem>
#include <memory>
#include <string>

namespace vpr {

// Runs a published or self trained YOLOX ONNX model. Throws std::runtime_error when the model
// cannot be loaded or does not have the expected shape.
class YoloxEngine {
public:
    YoloxEngine(const std::filesystem::path& modelPath, yolox::DecodeOptions options);
    ~YoloxEngine();
    YoloxEngine(const YoloxEngine&) = delete;
    YoloxEngine& operator=(const YoloxEngine&) = delete;

    [[nodiscard]] std::vector<Detection> detect(const Image& image);
    [[nodiscard]] std::string executionProvider() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// A COCO trained YOLOX model, keeping car, motorcycle, bus and truck.
class YoloxVehicleDetector final : public IVehicleDetector {
public:
    explicit YoloxVehicleDetector(const std::filesystem::path& modelPath);
    [[nodiscard]] std::vector<Detection> detect(const Image& frame) override;
    [[nodiscard]] std::string executionProvider() const { return m_engine.executionProvider(); }

private:
    YoloxEngine m_engine;
};

} // namespace vpr
