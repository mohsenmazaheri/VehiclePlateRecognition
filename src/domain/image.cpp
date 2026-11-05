#include "image.h"

#include <algorithm>
#include <cstring>

namespace vpr {

Image Image::crop(const RectF& box) const
{
    Image out;
    out.timestampMs = timestampMs;
    const int left = std::clamp(static_cast<int>(box.x), 0, std::max(0, width));
    const int top = std::clamp(static_cast<int>(box.y), 0, std::max(0, height));
    const int right = std::clamp(static_cast<int>(box.x + box.w), 0, std::max(0, width));
    const int bottom = std::clamp(static_cast<int>(box.y + box.h), 0, std::max(0, height));
    out.width = right - left;
    out.height = bottom - top;
    out.originX = originX + left;
    out.originY = originY + top;
    if (out.width <= 0 || out.height <= 0) {
        out.width = out.height = 0;
        return out;
    }
    if (!pixels)
        return out;

    auto data = std::make_shared<std::vector<std::uint8_t>>(
        static_cast<std::size_t>(out.width) * out.height * 3);
    for (int y = 0; y < out.height; ++y) {
        const auto* src = pixels->data() + (static_cast<std::size_t>(top + y) * width + left) * 3;
        std::memcpy(data->data() + static_cast<std::size_t>(y) * out.width * 3, src,
                    static_cast<std::size_t>(out.width) * 3);
    }
    out.pixels = std::move(data);
    return out;
}

Image blankImage(int width, int height, std::int64_t timestampMs)
{
    Image out;
    out.width = width;
    out.height = height;
    out.timestampMs = timestampMs;
    out.pixels = std::make_shared<std::vector<std::uint8_t>>(
        static_cast<std::size_t>(width) * height * 3, std::uint8_t{0});
    return out;
}

} // namespace vpr
