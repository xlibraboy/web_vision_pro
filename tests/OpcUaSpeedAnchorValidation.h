#pragma once

#include <QObject>

// Regression tests for the machine-speed anchor validation shown inline under
// the OPC UA speed grid and enforced at save: duplicate enabled positions are
// errors (the save is blocked); out-of-order rows are warnings only, because
// SpeedProfile sorts by position internally.
class OpcUaSpeedAnchorValidation : public QObject {
    Q_OBJECT

private slots:
    void orderedAnchorsProduceNothing();
    void duplicatePositionsAreErrors();
    void duplicatesAmongDisabledRowsAreIgnored();
    void simulatedRowsStillCount();
    void duplicateGlobalPositionsAreErrors();
    void unorderedRowsWarnWithoutBlocking();
    void zeroPositionsTakeNoPartInOrdering();
};
