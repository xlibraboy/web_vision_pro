#include "OpcUaValidation.h"

#include <QMap>

namespace {

QString anchorName(const OpcUaSpeedTagSettings& tag, int index) {
    const QString trimmed = tag.name.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("Speed %1").arg(index + 1) : trimmed;
}

QString joinNames(const QStringList& names) {
    QString joined;
    for (int i = 0; i < names.size(); ++i) {
        if (i > 0) {
            joined += (i == names.size() - 1) ? QStringLiteral(" and ") : QStringLiteral(", ");
        }
        joined += names.at(i);
    }
    return joined;
}

}  // namespace

void OpcUaValidation::checkSpeedAnchors(const std::vector<OpcUaSpeedTagSettings>& tags,
                                        QStringList* errors, QStringList* warnings) {
    if (!errors || !warnings) {
        return;
    }

    // Only enabled rows feed the speed profile. Simulated rows report a fixed
    // value but still become anchors, so they take part in both checks.
    QMap<int, QStringList> positionsByName;   // machine position -> anchor names
    QVector<QPair<int, QString>> orderedRows; // enabled positioned rows, grid order
    for (int i = 0; i < static_cast<int>(tags.size()); ++i) {
        const OpcUaSpeedTagSettings& tag = tags[static_cast<size_t>(i)];
        if (!tag.enabled) {
            continue;
        }
        const QString name = anchorName(tag, i);
        positionsByName[tag.positionMm].append(name);
        if (tag.positionMm > 0) {
            orderedRows.append({tag.positionMm, name});
        }
    }

    for (auto it = positionsByName.cbegin(); it != positionsByName.cend(); ++it) {
        const QStringList& names = it.value();
        if (names.size() < 2) {
            continue;
        }
        if (it.key() == 0) {
            *errors << QString("%1 share position 0 mm (no machine position); the global speed "
                               "is ambiguous. Set one position or keep only one anchor.")
                           .arg(joinNames(names));
        } else {
            *errors << QString("%1 share position %2 mm; the speed interpolated there is ambiguous. "
                               "Give each anchor its own position.")
                           .arg(joinNames(names))
                           .arg(it.key());
        }
    }

    // Position 0 means "position unknown / global" and takes no part in the
    // ordering; report the first inversion only, as a readability warning.
    for (int i = 0; i + 1 < orderedRows.size(); ++i) {
        if (orderedRows.at(i).first > orderedRows.at(i + 1).first) {
            *warnings << QString("Not ordered by position: %1 (%2 mm) sits above %3 (%4 mm). "
                                 "Interpolation is unaffected; ordering the rows by Pos (mm) "
                                 "reads easier.")
                             .arg(orderedRows.at(i).second)
                             .arg(orderedRows.at(i).first)
                             .arg(orderedRows.at(i + 1).second)
                             .arg(orderedRows.at(i + 1).first);
            break;
        }
    }
}
