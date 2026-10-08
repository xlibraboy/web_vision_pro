#pragma once

#include "CameraConfig.h"

#include <QStringList>
#include <vector>

/**
 * Validation for the OPC UA machine-speed anchors, shared by the settings
 * dialog's inline note and its save path.
 *
 * - Duplicate positions are errors: two enabled anchors at the same machine
 *   position make the interpolated speed there ambiguous (SpeedProfile sorts
 *   equal positions arbitrarily).
 * - Out-of-order rows are warnings only: SpeedProfile sorts by position
 *   internally, so interpolation is unaffected; the row order is a
 *   readability concern, not a correctness one.
 */
namespace OpcUaValidation {

void checkSpeedAnchors(const std::vector<OpcUaSpeedTagSettings>& tags,
                       QStringList* errors, QStringList* warnings);

}  // namespace OpcUaValidation
