#include "SpeedProfile.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace SpeedProfile {

namespace {
bool anchorUsable(const EventDatabase::SpeedAnchorSnapshot& anchor) {
    return std::isfinite(anchor.speedValue) && anchor.speedValue > 0.0;
}
} // namespace

bool hasValidAnchors(const std::vector<EventDatabase::SpeedAnchorSnapshot>& anchors) {
    for (const auto& anchor : anchors) {
        if (anchorUsable(anchor)) {
            return true;
        }
    }
    return false;
}

double speedAt(int positionMm,
               const std::vector<EventDatabase::SpeedAnchorSnapshot>& anchors,
               double fallbackSpeed) {
    // Collect usable anchors and sort them by machine position.
    std::vector<std::pair<int, double>> points;
    points.reserve(anchors.size());
    for (const auto& anchor : anchors) {
        if (anchorUsable(anchor)) {
            points.emplace_back(anchor.positionMm, anchor.speedValue);
        }
    }
    if (points.empty()) {
        return fallbackSpeed;
    }
    std::sort(points.begin(), points.end(),
              [](const std::pair<int, double>& a, const std::pair<int, double>& b) {
                  return a.first < b.first;
              });

    // Single anchor (or legacy tag with no position): the value applies
    // everywhere, matching the old single-speed behavior.
    if (points.size() == 1) {
        return points.front().second;
    }

    const double mm = static_cast<double>(positionMm);
    // Flat extension outside the outermost anchors.
    if (mm <= points.front().first) {
        return points.front().second;
    }
    if (mm >= points.back().first) {
        return points.back().second;
    }

    // Linear interpolation between the bracketing pair.
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const double lo = static_cast<double>(points[i].first);
        const double hi = static_cast<double>(points[i + 1].first);
        if (mm >= lo && mm <= hi) {
            const double t = hi > lo ? (mm - lo) / (hi - lo) : 0.0;
            return points[i].second + t * (points[i + 1].second - points[i].second);
        }
    }
    return points.back().second;
}

} // namespace SpeedProfile
