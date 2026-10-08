#include "OpcUaSpeedAnchorValidation.h"

#include "config/OpcUaValidation.h"

#include <QtTest>

namespace {

OpcUaSpeedTagSettings anchor(const QString& name, int positionMm, bool enabled = true,
                             bool simulated = false) {
    OpcUaSpeedTagSettings tag;
    tag.name = name;
    tag.enabled = enabled;
    tag.simulated = simulated;
    tag.positionMm = positionMm;
    return tag;
}

struct Result {
    QStringList errors;
    QStringList warnings;
};

Result check(const std::vector<OpcUaSpeedTagSettings>& tags) {
    Result result;
    OpcUaValidation::checkSpeedAnchors(tags, &result.errors, &result.warnings);
    return result;
}

}  // namespace

void OpcUaSpeedAnchorValidation::orderedAnchorsProduceNothing()
{
    const Result result = check({anchor("Wet end", 1200), anchor("Dry end", 8400)});
    QVERIFY(result.errors.isEmpty());
    QVERIFY(result.warnings.isEmpty());
}

void OpcUaSpeedAnchorValidation::duplicatePositionsAreErrors()
{
    const Result result = check({anchor("Wet end", 4500), anchor("Dry end", 4500)});
    QCOMPARE(result.errors.size(), 1);
    QVERIFY(result.errors.first().contains("Wet end"));
    QVERIFY(result.errors.first().contains("Dry end"));
    QVERIFY(result.errors.first().contains("4500"));
    QVERIFY(result.warnings.isEmpty());
}

void OpcUaSpeedAnchorValidation::duplicatesAmongDisabledRowsAreIgnored()
{
    const Result result = check({anchor("Wet end", 4500), anchor("Dry end", 4500, false)});
    QVERIFY(result.errors.isEmpty());
    QVERIFY(result.warnings.isEmpty());
}

void OpcUaSpeedAnchorValidation::simulatedRowsStillCount()
{
    // A simulated row reports a fixed value but still becomes an anchor.
    const Result result = check({anchor("Wet end", 3000, true, true), anchor("Dry end", 3000)});
    QCOMPARE(result.errors.size(), 1);
}

void OpcUaSpeedAnchorValidation::duplicateGlobalPositionsAreErrors()
{
    // Position 0 means "no machine position": two of them make the global
    // speed itself ambiguous.
    const Result result = check({anchor("Speed 1", 0), anchor("Speed 2", 0)});
    QCOMPARE(result.errors.size(), 1);
    QVERIFY(result.errors.first().contains("Speed 1"));
    QVERIFY(result.errors.first().contains("Speed 2"));
    QVERIFY(result.errors.first().contains("global"));
}

void OpcUaSpeedAnchorValidation::unorderedRowsWarnWithoutBlocking()
{
    const Result result = check({anchor("Dry end", 8400), anchor("Wet end", 1200)});
    QVERIFY(result.errors.isEmpty());
    QCOMPARE(result.warnings.size(), 1);
    QVERIFY(result.warnings.first().contains("Dry end"));
    QVERIFY(result.warnings.first().contains("Wet end"));
}

void OpcUaSpeedAnchorValidation::zeroPositionsTakeNoPartInOrdering()
{
    // A single global anchor before positioned anchors is not an inversion.
    const Result result = check({anchor("Global", 0), anchor("Wet end", 1200), anchor("Dry end", 8400)});
    QVERIFY(result.errors.isEmpty());
    QVERIFY(result.warnings.isEmpty());
}
