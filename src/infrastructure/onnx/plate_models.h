#pragma once
#include "domain/ports.h"

#include <filesystem>
#include <memory>

namespace vpr {

// Finds plates with a YOLOv9 model exported with its own box filtering (the "end2end" files).
// Throws std::runtime_error when the model cannot be loaded.
class YoloV9PlateDetector final : public IPlateDetector {
public:
    explicit YoloV9PlateDetector(const std::filesystem::path& modelPath, float scoreThreshold = 0.25F);
    ~YoloV9PlateDetector() override;
    [[nodiscard]] std::vector<Detection> detect(const Image& vehicleCrop) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Reads a plate crop with a fixed slot model, one classifier per character position, in the format
// of fast-plate-ocr. A self trained model with another alphabet only needs its own config file.
// Throws std::runtime_error when the model or its config cannot be loaded.
class SlotPlateReader final : public IPlateReader {
public:
    SlotPlateReader(const std::filesystem::path& modelPath, const std::filesystem::path& configPath);
    ~SlotPlateReader() override;
    [[nodiscard]] std::optional<PlateReading> read(const Image& plateCrop) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vpr
