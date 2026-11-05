#include "qt_image.h"

#include <cstring>

namespace vpr::qt {

Image toDomainImage(const QImage& image, std::int64_t timestampMs)
{
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    Image out;
    out.width = rgb.width();
    out.height = rgb.height();
    out.timestampMs = timestampMs;
    auto data = std::make_shared<std::vector<std::uint8_t>>(
        static_cast<std::size_t>(out.width) * out.height * 3);
    for (int y = 0; y < out.height; ++y) {
        std::memcpy(data->data() + static_cast<std::size_t>(y) * out.width * 3, rgb.constScanLine(y),
                    static_cast<std::size_t>(out.width) * 3);
    }
    out.pixels = std::move(data);
    return out;
}

QImage toQImage(const Image& image)
{
    if (image.empty())
        return {};
    // The wrapped pixels are immutable and outlive this call only through copy().
    const QImage view(image.pixels->data(), image.width, image.height, image.width * 3,
                      QImage::Format_RGB888);
    return view.copy();
}

} // namespace vpr::qt
