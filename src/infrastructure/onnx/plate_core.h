#pragma once
// Input and output handling of the plate models. No model runtime here, so it can be tested with
// hand made tensors.
#include "domain/types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vpr::plate {

struct Letterbox {
    double scale = 1.0;
    int padX = 0;
    int padY = 0;
};

// Fills `tensor` with 3 x height x width floats: RGB, 0 to 1, centred and padded with grey 114.
Letterbox prepareDetectorInput(const Image& image, int inputWidth, int inputHeight, std::vector<float>& tensor);

// `rows` holds count x 7 floats: batch index, x1, y1, x2, y2, class, score, in model input pixels.
// Boxes come back in image coordinates.
[[nodiscard]] std::vector<Detection> decodeDetections(const float* rows, std::size_t count, std::size_t columns,
                                                      const Letterbox& letterbox, int imageWidth, int imageHeight,
                                                      float scoreThreshold);

struct ReaderConfig {
    std::u32string alphabet;
    char32_t padChar = U'_';
    int plateSlots = 0;
    int inputWidth = 0;
    int inputHeight = 0;
};

// Reads the plate config published with a reader model. Throws std::runtime_error if a value is missing.
[[nodiscard]] ReaderConfig parseReaderConfig(std::string_view yaml);

// Fills `tensor` with height x width x 3 bytes, RGB. The plate is stretched to the input size.
void prepareReaderInput(const Image& plate, const ReaderConfig& config, std::vector<std::uint8_t>& tensor);

// `probabilities` holds slots x alphabet floats. Trailing padding is removed. The confidence is the
// lowest score of a kept character, because one unsure character makes the whole plate unsure.
[[nodiscard]] std::optional<PlateReading> decodeText(const float* probabilities, std::size_t values,
                                                     const ReaderConfig& config);

} // namespace vpr::plate
