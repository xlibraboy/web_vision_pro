#pragma once

#include <QObject>

// Regression tests for section-scoped trigger recording.
//
// A trigger records the machine sections selected in its "Records" picker:
// every section (the default) records all active cameras, unassigned ones
// included, while a narrowed selection records only cameras assigned to those
// sections - so a Press-Part break can leave Pre-Dryer, After-Dryer and
// Calender-Reel out of the event. A selection with no camera in it must be
// refused with a reason instead of silently recording nothing.
class TriggerRecordScope : public QObject {
    Q_OBJECT

private slots:
    void narrowedScopeRecordsOnlyItsSections();
    void scopeWithNoCameraInItsSectionsIsRefused();
    void allSectionsScopeRecordsUnassignedCameras();
    void recordGroupsSurviveSaveAndReload();
};
