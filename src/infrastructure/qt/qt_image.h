#pragma once
#include "domain/image.h"

#include <QImage>

namespace vpr::qt {

[[nodiscard]] Image toDomainImage(const QImage& image, std::int64_t timestampMs);
[[nodiscard]] QImage toQImage(const Image& image);

} // namespace vpr::qt
