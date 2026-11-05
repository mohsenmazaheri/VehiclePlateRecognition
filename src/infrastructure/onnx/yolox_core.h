#pragma once
// YOLOX input and output handling. No model runtime here, so it can be tested with hand made tensors.
#include "domain/types.h"

#include <cstddef>
#include <vector>

namespace vpr::yolox {

struct DecodeOptions {
    float scoreThreshold = 0.1F;  // low on purpose: the tracker needs weak boxes to hold a vehicle
    float nmsThreshold = 0.45F;
    std::vector<int> classes;     // classes to keep, merged into one group
};

// Fills `tensor` with 3 x height x width floats: BGR, 0 to 255, padded with 114 on the right and
// bottom, as the published YOLOX models expect. Returns the scale from image to model input.
double prepareInput(const Image& image, int inputWidth, int inputHeight, std::vector<float>& tensor);

// Number of output rows of a model with this input size (strides 8, 16 and 32).
[[nodiscard]] std::size_t expectedRows(int inputWidth, int inputHeight);

// `output` holds rows x columns floats: 4 raw box values, objectness, then one score per class.
// Boxes come back in image coordinates.
[[nodiscard]] std::vector<Detection> decode(const float* output, std::size_t rows, std::size_t columns,
                                            int inputWidth, int inputHeight, double scale,
                                            int imageWidth, int imageHeight, const DecodeOptions& options);

} // namespace vpr::yolox
