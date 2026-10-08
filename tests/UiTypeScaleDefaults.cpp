#include "UiTypeScaleDefaults.h"

#include "config/CameraConfig.h"
#include "config/UiTypeScale.h"

#include <QFont>
#include <QFontInfo>
#include <QtTest>

void UiTypeScaleDefaults::stepsFormAnOrderedLadder()
{
    QVERIFY(UiTypeScale::kOverlayMono < UiTypeScale::kMicro);
    QVERIFY(UiTypeScale::kMicro < UiTypeScale::kMeta);
    QVERIFY(UiTypeScale::kMeta < UiTypeScale::kBody);
    QVERIFY(UiTypeScale::kBody < UiTypeScale::kTitle);
    QVERIFY(UiTypeScale::kTitle < UiTypeScale::kDisplay);
}

void UiTypeScaleDefaults::namedFamiliesAreInstalled()
{
    // A family that silently falls back is not a type choice: the scale's
    // families must resolve to themselves in the runtime image.
    const QFontInfo ui(QFont(UiTypeScale::uiFamily()));
    QCOMPARE(ui.family(), UiTypeScale::uiFamily());

    const QFontInfo mono(QFont(UiTypeScale::monoFamily()));
    QCOMPARE(mono.family(), UiTypeScale::monoFamily());
}

void UiTypeScaleDefaults::preferenceDefaultsUseTheScale()
{
    const LiveViewCardStyle live = CameraConfig::getDefaultLiveViewCardStyle();
    QCOMPARE(live.gridTitleFontFamily, UiTypeScale::uiFamily());
    QCOMPARE(live.gridTitleFontSize, UiTypeScale::kTitle);
    QCOMPARE(live.detailTitleFontSize, UiTypeScale::kTitle);
    QCOMPARE(live.detailSectionFontSize, UiTypeScale::kBody);

    const AnalysisViewStyle analysis = CameraConfig::getDefaultAnalysisViewStyle();
    QCOMPARE(analysis.videoTitleFontFamily, UiTypeScale::uiFamily());
    QCOMPARE(analysis.videoTitleFontSize, UiTypeScale::kMicro);
    QCOMPARE(analysis.tabFontSize, UiTypeScale::kBody);
    QCOMPARE(analysis.timestampFontFamily, UiTypeScale::monoFamily());
    QCOMPARE(analysis.timestampFontSize, UiTypeScale::kOverlayMono);
    QCOMPARE(analysis.defaultMetadataFontFamily, UiTypeScale::monoFamily());
    QCOMPARE(analysis.defaultMetadataFontSize, UiTypeScale::kOverlayMono);
}

void UiTypeScaleDefaults::pxHelperFormatsCssValues()
{
    QCOMPARE(UiTypeScale::px(UiTypeScale::kBody), QStringLiteral("12px"));
    QCOMPARE(UiTypeScale::px(UiTypeScale::kMicro), QStringLiteral("10px"));
}
