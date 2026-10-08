#include "AccessibilitySummary.h"

#include "gui/widgets/AccessibleTextSummary.h"
#include "gui/widgets/EventDashboard.h"
#include "gui/widgets/MachineLayoutPanel.h"
#include "gui/widgets/ToggleSwitch.h"

#include <QAccessible>
#include <QApplication>
#include <QtTest>

#include <vector>

namespace {

QString accessibleText(QWidget* widget) {
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(widget);
    if (!iface) {
        return QString();
    }
    QAccessibleTextInterface* text = iface->textInterface();
    return text ? text->text(0, text->characterCount()) : QString();
}

}  // namespace

void AccessibilitySummary::initTestCase() {
    // The app installs this factory in main(); the suite installs it here so
    // the summaries can be queried without launching the application.
    QAccessible::installFactory(accessibleTextSummaryFactory);
}

void AccessibilitySummary::dashboardSummaryDescribesEvent() {
    EventDashboard dashboard;
    dashboard.setEventData(QStringLiteral("CAM-02"), 150, 99, 20.0,
                           QVector<int>({0, 10, 20}),           // sample frames
                           QVector<double>({100.0, 101.0, 102.0}),  // brightness
                           QVector<double>({30.0, 31.0, 32.0}),     // contrast
                           QVector<double>({0.02, 0.30, 0.01}),     // spots %
                           QVector<int>({12, 50}),              // brightness-jump hits
                           QVector<int>({60}),                  // local-spot hits
                           QVector<int>());                     // low-contrast hits
    dashboard.setCurrentFrame(120);
    dashboard.setBrightnessVisible(true);
    dashboard.setRegionsVisible(false, false, false, true);

    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(&dashboard);
    QVERIFY(iface);
    QCOMPARE(static_cast<int>(iface->role()), static_cast<int>(QAccessible::Chart));
    QAccessibleTextInterface* text = iface->textInterface();
    QVERIFY2(text, "the event dashboard must expose a text summary");

    const QString summary = text->text(0, text->characterCount());
    QCOMPARE(text->characterCount(), summary.size());
    QVERIFY(summary.contains(QStringLiteral("CAM-02")));
    QVERIFY(summary.contains(QStringLiteral("150 frames at 20.0 FPS")));
    QVERIFY(summary.contains(QStringLiteral("Trigger at frame 99")));
    QVERIFY(summary.contains(QStringLiteral("playhead at frame 120")));
    QVERIFY(summary.contains(QStringLiteral("2 brightness jump")));
    QVERIFY(summary.contains(QStringLiteral("1 local spot")));
    QVERIFY(summary.contains(QStringLiteral("0 low contrast")));
    QVERIFY(summary.contains(QStringLiteral("Peak spot area 0.30 percent")));
    QVERIFY(summary.contains(QStringLiteral("brightness chart")));
    QVERIFY(summary.contains(QStringLiteral("thumbnails")));
}

void AccessibilitySummary::dashboardSummaryStatesMissingRecording() {
    EventDashboard dashboard;
    dashboard.setNotRecorded(QStringLiteral("Camera did not record this event."));

    const QString summary = accessibleText(&dashboard);
    QVERIFY(summary.contains(QStringLiteral("no recording in the event")));
    QVERIFY(summary.contains(QStringLiteral("Camera did not record this event.")));
}

void AccessibilitySummary::machineLayoutSummaryCountsMarkers() {
    MachineLayoutPanel panel;
    std::vector<CameraInfo> cameras(2);
    cameras[0].id = 1;
    cameras[0].name = QStringLiteral("Wet end");
    cameras[0].machinePosition = 1200;
    cameras[1].id = 2;
    cameras[1].name = QStringLiteral("Dry end");
    cameras[1].machinePosition = 0;
    panel.setCameras(cameras);
    panel.refresh();

    QWidget* canvas = panel.findChild<QWidget*>(QStringLiteral("machineLayoutCanvas"));
    QVERIFY2(canvas, "the machine layout canvas must carry its object name");
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(canvas);
    QVERIFY(iface);
    QCOMPARE(static_cast<int>(iface->role()), static_cast<int>(QAccessible::Graphic));
    QAccessibleTextInterface* text = iface->textInterface();
    QVERIFY2(text, "the machine layout must expose a text summary");

    const QString summary = text->text(0, -1);
    QVERIFY(summary.contains(QStringLiteral("2 cameras")));
    QVERIFY(summary.contains(QStringLiteral("1 with a machine position")));
    QVERIFY(summary.contains(QStringLiteral("No marked and aligned defects")));
}

void AccessibilitySummary::deletionToggleAnnouncesItsName() {
    ToggleSwitch toggle;
    toggle.setAccessibleName(QStringLiteral("Enable Event Deletion"));

    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(&toggle);
    QVERIFY(iface);
    QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("Enable Event Deletion"));
}

void AccessibilitySummary::focusRingRendersOnTheDeletionToggle() {
    ToggleSwitch toggle;
    toggle.resize(toggle.sizeHint());
    const QImage unfocused = toggle.grab().toImage();

    toggle.show();
    toggle.activateWindow();
    toggle.setFocus(Qt::TabFocusReason);
    QCoreApplication::processEvents();
    if (!toggle.hasFocus()) {
        QSKIP("the offscreen platform did not grant the toggle keyboard focus");
    }

    const QImage focused = toggle.grab().toImage();
    QVERIFY2(focused != unfocused,
             "a focused toggle must render a visible focus indication");
    toggle.close();
}
