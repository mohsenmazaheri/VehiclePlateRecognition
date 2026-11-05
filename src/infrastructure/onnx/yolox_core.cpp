#include "yolox_core.h"

#include "resize.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vpr::yolox {

namespace {

constexpr int kStrides[] = {8, 16, 32};
constexpr float kPadding = 114.0F;
constexpr std::size_t kClassOffset = 5;

} // namespace

double prepareInput(const Image& image, int inputWidth, int inputHeight, std::vector<float>& tensor)
{
    if (image.empty())
        throw std::invalid_argument("prepareInput: empty image");

    const double scale = std::min(static_cast<double>(inputWidth) / image.width,
                                  static_cast<double>(inputHeight) / image.height);
    const int width = std::clamp(static_cast<int>(image.width * scale), 1, inputWidth);
    const int height = std::clamp(static_cast<int>(image.height * scale), 1, inputHeight);

    const auto planeSize = static_cast<std::size_t>(inputWidth) * inputHeight;
    tensor.assign(planeSize * 3, kPadding);
    float* blue = tensor.data();
    float* green = blue + planeSize;
    float* red = green + planeSize;

    const auto columns = onnx::resizeSamples(width, image.width, scale);
    const auto rows = onnx::resizeSamples(height, image.height, scale);
    const std::uint8_t* pixels = image.pixels->data();
    const auto stride = static_cast<std::size_t>(image.width) * 3;

    for (int y = 0; y < height; ++y) {
        const onnx::ResizeSample& row = rows[static_cast<std::size_t>(y)];
        const std::uint8_t* top = pixels + static_cast<std::size_t>(row.low) * stride;
        const std::uint8_t* bottom = pixels + static_cast<std::size_t>(row.high) * stride;
        const std::size_t offset = static_cast<std::size_t>(y) * inputWidth;
        for (int x = 0; x < width; ++x) {
            const onnx::ResizeSample& column = columns[static_cast<std::size_t>(x)];
            const auto left = static_cast<std::size_t>(column.low) * 3;
            const auto right = static_cast<std::size_t>(column.high) * 3;
            const auto channel = [&](std::size_t c) {
                const float upper = top[left + c] + (top[right + c] - top[left + c]) * column.weight;
                const float lower = bottom[left + c] + (bottom[right + c] - bottom[left + c]) * column.weight;
                return upper + (lower - upper) * row.weight;
            };
            const std::size_t at = offset + static_cast<std::size_t>(x);
            red[at] = channel(0);
            green[at] = channel(1);
            blue[at] = channel(2);
        }
    }
    return scale;
}

std::size_t expectedRows(int inputWidth, int inputHeight)
{
    std::size_t rows = 0;
    for (const int stride : kStrides)
        rows += static_cast<std::size_t>(inputWidth / stride) * (inputHeight / stride);
    return rows;
}

std::vector<Detection> decode(const float* output, std::size_t rows, std::size_t columns,
                              int inputWidth, int inputHeight, double scale,
                              int imageWidth, int imageHeight, const DecodeOptions& options)
{
    if (rows != expectedRows(inputWidth, inputHeight))
        throw std::runtime_error("YOLOX output has an unexpected number of rows");
    for (const int cls : options.classes) {
        if (cls < 0 || kClassOffset + static_cast<std::size_t>(cls) >= columns)
            throw std::runtime_error("YOLOX class index is outside the model output");
    }

    std::vector<Detection> candidates;
    const float* row = output;
    for (const int stride : kStrides) {
        const int gridWidth = inputWidth / stride;
        const int gridHeight = inputHeight / stride;
        for (int gy = 0; gy < gridHeight; ++gy) {
            for (int gx = 0; gx < gridWidth; ++gx, row += columns) {
                float best = 0.0F;
                for (const int cls : options.classes)
                    best = std::max(best, row[kClassOffset + static_cast<std::size_t>(cls)]);
                const float score = row[4] * best;
                if (score < options.scoreThreshold)
                    continue;

                const double centreX = (row[0] + gx) * stride / scale;
                const double centreY = (row[1] + gy) * stride / scale;
                const double width = std::exp(static_cast<double>(row[2])) * stride / scale;
                const double height = std::exp(static_cast<double>(row[3])) * stride / scale;

                const double left = std::clamp(centreX - width / 2.0, 0.0, static_cast<double>(imageWidth));
                const double top = std::clamp(centreY - height / 2.0, 0.0, static_cast<double>(imageHeight));
                const double right = std::clamp(centreX + width / 2.0, 0.0, static_cast<double>(imageWidth));
                const double bottom = std::clamp(centreY + height / 2.0, 0.0, static_cast<double>(imageHeight));
                if (right > left && bottom > top)
                    candidates.push_back({{left, top, right - left, bottom - top}, score});
            }
        }
    }

    // One group across classes: the same vehicle often scores as both car and truck, and the
    // tracker must see one box for it.
    std::sort(candidates.begin(), candidates.end(),
              [](const Detection& a, const Detection& b) { return a.confidence > b.confidence; });
    std::vector<Detection> kept;
    for (const Detection& candidate : candidates) {
        const bool overlaps = std::any_of(kept.begin(), kept.end(), [&](const Detection& other) {
            return intersectionOverUnion(candidate.box, other.box) > options.nmsThreshold;
        });
        if (!overlaps)
            kept.push_back(candidate);
    }
    return kept;
}

} // namespace vpr::yolox
