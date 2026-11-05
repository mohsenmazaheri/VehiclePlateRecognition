#pragma once
// Bilinear resizing with OpenCV's pixel centre mapping, which is what the published models were
// prepared with. No model runtime here.
#include "domain/image.h"

#include <cstdint>
#include <vector>

namespace vpr::onnx {

struct ResizeSample {
    int low = 0;
    int high = 0;
    float weight = 0.0F;
};

// One entry per output position. `scale` is output size divided by source size.
[[nodiscard]] std::vector<ResizeSample> resizeSamples(int outputSize, int sourceSize, double scale);

// Writes width x height x 3 bytes, RGB, to `out`.
void resizeRgb(const Image& image, int width, int height, std::uint8_t* out);

} // namespace vpr::onnx
