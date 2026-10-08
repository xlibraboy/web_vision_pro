#include "DefectSeverityClassification.h"

#include "processing/EventSignalScanner.h"

#include <QtTest>

namespace {

// One sample series at frames 0/10/20/30, so each test states only the values
// it cares about. Contrast stays healthy unless a test overrides it.
EventSignalData makeData(const QVector<double>& spots,
                         const QVector<double>& contrast,
                         const QVector<int>& brightnessHits = QVector<int>()) {
    EventSignalData data;
    data.sampleFrames = {0, 10, 20, 30};
    data.spotPct = spots;
    data.stddev = contrast;
    data.brightness = {100.0, 100.0, 100.0, 100.0};
    data.defectBrightness = brightnessHits;
    data.totalFrames = 40;
    data.fps = 10.0;
    return data;
}

int classify(const EventSignalData& data, int frame) {
    return static_cast<int>(EventSignalScanner::classifyMarkedFrame(data, frame));
}

}  // namespace

// No scan result at all: unknown, never ok (an unanalyzed event must not read
// as healthy).
void DefectSeverityClassification::unknownWithoutScanData()
{
    EventSignalData empty;
    QCOMPARE(EventSignalScanner::nearestSampleIndex(empty, 5), -1);
    QCOMPARE(classify(empty, 5), static_cast<int>(EventSignalScanner::SeverityUnknown));
}

void DefectSeverityClassification::nearestSamplePicksClosestSample()
{
    const EventSignalData data = makeData({0, 0, 0, 0}, {30, 30, 30, 30});
    QCOMPARE(EventSignalScanner::nearestSampleIndex(data, 0), 0);
    QCOMPARE(EventSignalScanner::nearestSampleIndex(data, 12), 1);
    QCOMPARE(EventSignalScanner::nearestSampleIndex(data, 19), 2);
    QCOMPARE(EventSignalScanner::nearestSampleIndex(data, 27), 3);
}

void DefectSeverityClassification::okWhenNoRuleFires()
{
    const EventSignalData data = makeData({0.02, 0.01, 0.02, 0.01}, {30, 28, 31, 29});
    QCOMPARE(classify(data, 10), static_cast<int>(EventSignalScanner::SeverityOk));
}

void DefectSeverityClassification::warnOnSpotRule()
{
    const EventSignalData data = makeData({0.01, 0.06, 0.01, 0.01}, {30, 30, 30, 30});
    QCOMPARE(classify(data, 10), static_cast<int>(EventSignalScanner::SeverityWarn));
}

void DefectSeverityClassification::warnOnContrastRule()
{
    const EventSignalData data = makeData({0.01, 0.01, 0.01, 0.01}, {30, 9.5, 30, 30});
    QCOMPARE(classify(data, 10), static_cast<int>(EventSignalScanner::SeverityWarn));
}

void DefectSeverityClassification::warnOnBrightnessJump()
{
    const EventSignalData data = makeData({0.01, 0.01, 0.01, 0.01}, {30, 30, 30, 30}, {10});
    QCOMPARE(classify(data, 10), static_cast<int>(EventSignalScanner::SeverityWarn));
}

void DefectSeverityClassification::criticalWhenTwoRulesFire()
{
    // Spot rule + contrast rule at the same sample.
    const EventSignalData data = makeData({0.01, 0.06, 0.01, 0.01}, {30, 9.0, 30, 30});
    QCOMPARE(classify(data, 10), static_cast<int>(EventSignalScanner::SeverityCritical));
}

void DefectSeverityClassification::criticalWhenAnOrderOfMagnitudePastSpotRule()
{
    // 10 x kLocalSpotMinPct with only one rule firing.
    const EventSignalData data = makeData({0.01, 0.5, 0.01, 0.01}, {30, 30, 30, 30});
    QCOMPARE(classify(data, 10), static_cast<int>(EventSignalScanner::SeverityCritical));
}

void DefectSeverityClassification::criticalWhenFullyWashedOut()
{
    // Contrast below half the floor: critical even with no other rule.
    const EventSignalData data = makeData({0.01, 0.01, 0.01, 0.01}, {30, 4.0, 30, 30});
    QCOMPARE(classify(data, 10), static_cast<int>(EventSignalScanner::SeverityCritical));
}
