#pragma once

#include <QObject>

// Regression tests for the marked-defect severity heuristic
// (EventSignalScanner::classifyMarkedFrame). The verdict drives the Machine
// Layout marker encoding, so each rule edge gets its own case: no data,
// nearest-sample lookup, ok, spot-warn, contrast-warn, brightness-warn,
// two-rules critical, order-of-magnitude critical, and washed-out critical.
class DefectSeverityClassification : public QObject {
    Q_OBJECT

private slots:
    void unknownWithoutScanData();
    void nearestSamplePicksClosestSample();
    void okWhenNoRuleFires();
    void warnOnSpotRule();
    void warnOnContrastRule();
    void warnOnBrightnessJump();
    void criticalWhenTwoRulesFire();
    void criticalWhenAnOrderOfMagnitudePastSpotRule();
    void criticalWhenFullyWashedOut();
};
