#pragma once

#include "EventDatabase.h"
#include <vector>

/**
 * SpeedProfile - resolves the paper's local speed (m/min) at any machine
 * position (mm) from the speed anchors snapshotted on an event.
 *
 * A paper machine's drive groups run at slightly different speeds (draw), so a
 * single speed value misplaces defects on the mm ruler. Each speed anchor
 * reports the ACTUAL speed of one drive at its machine position; the local
 * speed anywhere between anchors is linearly interpolated.
 */
namespace SpeedProfile {

// True when `anchors` contains at least one usable (finite, positive) sample.
bool hasValidAnchors(const std::vector<EventDatabase::SpeedAnchorSnapshot>& anchors);

// Local speed (m/min) at positionMm:
//  - interpolates linearly between the anchors (sorted by position), flat
//    outside the outermost anchors,
//  - returns the single anchor's value when only one anchor is valid,
//  - returns fallbackSpeed when no valid anchors exist (legacy events carry
//    only EventInfo.speedValue and no anchors).
double speedAt(int positionMm,
               const std::vector<EventDatabase::SpeedAnchorSnapshot>& anchors,
               double fallbackSpeed);

} // namespace SpeedProfile
