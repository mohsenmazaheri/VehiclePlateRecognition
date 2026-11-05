#pragma once
#include "geometry.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace vpr {

// Framework independent RGB24 image. Pixels are shared and immutable, so copies are cheap.
// originX/originY give the position of this image inside the source frame (0,0 for a full frame).
struct Image {
    int width = 0;
    int height = 0;
    int originX = 0;
    int originY = 0;
    std::int64_t timestampMs = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> pixels;

    [[nodiscard]] bool empty() const { return width <= 0 || height <= 0 || !pixels; }
    [[nodiscard]] Image crop(const RectF& box) const;
};

[[nodiscard]] Image blankImage(int width, int height, std::int64_t timestampMs = 0);

} // namespace vpr
