#pragma once
#include <algorithm>

namespace vpr {

struct PointF {
    double x = 0.0;
    double y = 0.0;
};

struct RectF {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;

    [[nodiscard]] double area() const { return std::max(0.0, w) * std::max(0.0, h); }
    [[nodiscard]] PointF bottomCentre() const { return {x + w / 2.0, y + h}; }
    [[nodiscard]] PointF centre() const { return {x + w / 2.0, y + h / 2.0}; }
};

[[nodiscard]] inline double intersectionOverUnion(const RectF& a, const RectF& b)
{
    const double left = std::max(a.x, b.x);
    const double top = std::max(a.y, b.y);
    const double right = std::min(a.x + a.w, b.x + b.w);
    const double bottom = std::min(a.y + a.h, b.y + b.h);
    const double inter = std::max(0.0, right - left) * std::max(0.0, bottom - top);
    const double uni = a.area() + b.area() - inter;
    return uni > 0.0 ? inter / uni : 0.0;
}

} // namespace vpr
