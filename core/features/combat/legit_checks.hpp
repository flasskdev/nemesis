#pragma once

#include <algorithm>
#include <cmath>

namespace features::combat::legit_checks {

    struct point3
    {
        double x{};
        double y{};
        double z{};
    };

    [[nodiscard]] inline bool finite(const point3& p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }

    // Invalid input blocks the shot. Endpoints and tangencies count as hits.
    [[nodiscard]] inline bool segment_intersects_sphere(
        const point3& start, const point3& end, const point3& center, double radius)
    {
        if (!finite(start) || !finite(end) || !finite(center) ||
            !std::isfinite(radius) || radius < 0.0)
            return true;

        const auto dx = end.x - start.x;
        const auto dy = end.y - start.y;
        const auto dz = end.z - start.z;
        const auto length_squared = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(length_squared))
            return true;

        auto t = 0.0;
        if (length_squared > 0.0)
        {
            const auto projection = ((center.x - start.x) * dx +
                (center.y - start.y) * dy + (center.z - start.z) * dz) / length_squared;
            if (!std::isfinite(projection))
                return true;
            t = std::clamp(projection, 0.0, 1.0);
        }

        const auto ox = start.x + t * dx - center.x;
        const auto oy = start.y + t * dy - center.y;
        const auto oz = start.z + t * dz - center.z;
        const auto distance_squared = ox * ox + oy * oy + oz * oz;
        const auto radius_squared = radius * radius;
        if (!std::isfinite(distance_squared) || !std::isfinite(radius_squared))
            return true;
        return distance_squared <= radius_squared;
    }

} // namespace features::combat::legit_checks
