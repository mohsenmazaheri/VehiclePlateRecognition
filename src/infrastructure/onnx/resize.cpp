#include "resize.h"

#include <algorithm>
#include <stdexcept>

namespace vpr::onnx {

std::vector<ResizeSample> resizeSamples(int outputSize, int sourceSize, double scale)
{
    std::vector<ResizeSample> samples(static_cast<std::size_t>(outputSize));
    for (int i = 0; i < outputSize; ++i) {
        const double position = std::clamp((i + 0.5) / scale - 0.5, 0.0, static_cast<double>(sourceSize - 1));
        const int low = static_cast<int>(position);
        samples[static_cast<std::size_t>(i)] = {low, std::min(low + 1, sourceSize - 1),
                                                static_cast<float>(position - low)};
    }
    return samples;
}

void resizeRgb(const Image& image, int width, int height, std::uint8_t* out)
{
    if (image.empty() || width <= 0 || height <= 0)
        throw std::invalid_argument("resizeRgb: empty image or size");

    const auto columns = resizeSamples(width, image.width, static_cast<double>(width) / image.width);
    const auto rows = resizeSamples(height, image.height, static_cast<double>(height) / image.height);
    const std::uint8_t* pixels = image.pixels->data();
    const auto stride = static_cast<std::size_t>(image.width) * 3;

    for (const ResizeSample& row : rows) {
        const std::uint8_t* top = pixels + static_cast<std::size_t>(row.low) * stride;
        const std::uint8_t* bottom = pixels + static_cast<std::size_t>(row.high) * stride;
        for (const ResizeSample& column : columns) {
            const auto left = static_cast<std::size_t>(column.low) * 3;
            const auto right = static_cast<std::size_t>(column.high) * 3;
            for (std::size_t c = 0; c < 3; ++c) {
                const float upper = top[left + c] + (top[right + c] - top[left + c]) * column.weight;
                const float lower = bottom[left + c] + (bottom[right + c] - bottom[left + c]) * column.weight;
                *out++ = static_cast<std::uint8_t>(upper + (lower - upper) * row.weight + 0.5F);
            }
        }
    }
}

} // namespace vpr::onnx
