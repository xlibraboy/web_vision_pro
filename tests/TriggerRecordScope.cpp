#include "TriggerRecordScope.h"

#include "config/CameraConfig.h"
#include "core/EventController.h"
#include "core/EventDatabase.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr double kNominalFps = 55.0;
constexpr double kFrameIntervalMs = 1000.0 / kNominalFps;
constexpr int kPreFrames = 550;
constexpr int kPostFrames = 110;
constexpr int kRingCapacity = kPreFrames + kPostFrames;

QStringList eventBins(const QString& dir)
{
    return QDir(dir).entryList(QStringList() << QStringLiteral("event_*_cam*.bin"),
                               QDir::Files, QDir::Name);
}

// The event's JSON is written after every camera file, so its arrival is the
// point at which the event is complete on disk.
QStringList eventJsons(const QString& dir)
{
    return QDir(dir).entryList(QStringList() << QStringLiteral("event_*.json"),
                               QDir::Files, QDir::Name);
}

// event_<timestamp>_cam1.bin -> <timestamp>
QString timestampOf(const QString& binName)
{
    return binName.mid(QStringLiteral("event_").size(),
                       binName.size()
                           - QStringLiteral("event_").size()
                           - QStringLiteral("_cam1.bin").size());
}

std::vector<CameraInfo> twoCameras(int firstGroup, int secondGroup)
{
    std::vector<CameraInfo> cameras(2);
    cameras[0].id = 1;
    cameras[0].name = QStringLiteral("PRESS 1");
    cameras[0].group = firstGroup;
    cameras[1].id = 2;
    cameras[1].name = QStringLiteral("DRYER 1");
    cameras[1].group = secondGroup;
    return cameras;
}

} // namespace

void TriggerRecordScope::narrowedScopeRecordsOnlyItsSections()
{
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    CameraConfig::setEventStoragePath(dataDir.path());
    EventDatabase::instance().initialize(dataDir.path());

    // Camera 1 sits in the Press-Part, camera 2 downstream in the Pre-Dryer.
    CameraConfig::saveCameras(twoCameras(CameraGroup::kPressPart, CameraGroup::kPreDryer));

    EventController& recorder = EventController::instance();
    recorder.initialize(kPreFrames, kNominalFps, kPostFrames);
    recorder.setCameraFpsProvider([](int) { return kNominalFps; });

    const cv::Mat frame(8, 8, CV_8UC1, cv::Scalar(120));
    const int64_t baseNs = 3000000000LL;
    const auto stampAt = [baseNs](int index) {
        return baseNs + static_cast<int64_t>(std::llround(index * kFrameIntervalMs * 1e6));
    };
    for (int i = 0; i < kRingCapacity; ++i) {
        recorder.addFrame(1, frame, stampAt(i), i);
        recorder.addFrame(2, frame, stampAt(i), i);
    }

    // A break at the Press-Part: record that section only. Both cameras stay
    // live, so only the scope keeps camera 2 out of the event.
    EventController::TriggerContext context;
    context.group = CameraGroup::kPressPart;
    context.recordGroups = { CameraGroup::kPressPart };
    QString ignoreReason;
    QVERIFY(recorder.triggerEvent(context, &ignoreReason));
    QVERIFY2(ignoreReason.isEmpty(), qPrintable(ignoreReason));

    for (int i = 0; i < kPostFrames * 3; ++i) {
        const int index = kRingCapacity + i;
        recorder.addFrame(1, frame, stampAt(index), index);
        recorder.addFrame(2, frame, stampAt(index), index);
    }
    QTRY_VERIFY_WITH_TIMEOUT(!eventJsons(dataDir.path()).isEmpty(), 30000);

    const QStringList bins = eventBins(dataDir.path());
    QCOMPARE(bins.size(), 1);
    QVERIFY2(bins.first().contains(QStringLiteral("_cam1.bin")),
             qPrintable(QString("recorded %1, expected only the Press-Part camera").arg(bins.first())));

    // Camera 2 was never part of the event, so it must not be reported as a
    // missing participant either.
    const EventDatabase::EventInfo info =
        EventDatabase::instance().getEventInfo(timestampOf(bins.first()));
    QVERIFY2(info.missingCameraIds.empty(), "the skipped section was reported as missing");
}

void TriggerRecordScope::scopeWithNoCameraInItsSectionsIsRefused()
{
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    CameraConfig::setEventStoragePath(dataDir.path());
    EventDatabase::instance().initialize(dataDir.path());

    // Every camera is upstream; nothing is assigned to the Calender-Reel.
    CameraConfig::saveCameras(twoCameras(CameraGroup::kPressPart, CameraGroup::kPreDryer));

    EventController& recorder = EventController::instance();
    recorder.initialize(kPreFrames, kNominalFps, kPostFrames);
    recorder.setCameraFpsProvider([](int) { return kNominalFps; });

    const cv::Mat frame(8, 8, CV_8UC1, cv::Scalar(120));
    const auto stampAt = [](int index) {
        return 4000000000LL + static_cast<int64_t>(std::llround(index * kFrameIntervalMs * 1e6));
    };
    for (int i = 0; i < kRingCapacity; ++i) {
        recorder.addFrame(1, frame, stampAt(i), i);
        recorder.addFrame(2, frame, stampAt(i), i);
    }

    EventController::TriggerContext context;
    context.recordGroups = { CameraGroup::kCalenderReel };
    QString ignoreReason;
    QVERIFY(!recorder.triggerEvent(context, &ignoreReason));
    QVERIFY2(ignoreReason.contains(QStringLiteral("Calender-Reel")),
             qPrintable(QString("ignore reason was '%1'").arg(ignoreReason)));
    QVERIFY(eventBins(dataDir.path()).isEmpty());
}

void TriggerRecordScope::allSectionsScopeRecordsUnassignedCameras()
{
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    CameraConfig::setEventStoragePath(dataDir.path());
    EventDatabase::instance().initialize(dataDir.path());

    // Cameras not yet assigned to a section (a fresh lineup).
    CameraConfig::saveCameras(twoCameras(CameraGroup::kUnassigned, CameraGroup::kUnassigned));

    EventController& recorder = EventController::instance();
    recorder.initialize(kPreFrames, kNominalFps, kPostFrames);
    recorder.setCameraFpsProvider([](int) { return kNominalFps; });

    const cv::Mat frame(8, 8, CV_8UC1, cv::Scalar(120));
    const auto stampAt = [](int index) {
        return 5000000000LL + static_cast<int64_t>(std::llround(index * kFrameIntervalMs * 1e6));
    };
    for (int i = 0; i < kRingCapacity; ++i) {
        recorder.addFrame(1, frame, stampAt(i), i);
        recorder.addFrame(2, frame, stampAt(i), i);
    }

    // The default scope: every section selected, so nothing is filtered.
    EventController::TriggerContext context;
    context.recordGroups = { CameraGroup::kWire, CameraGroup::kPressPart,
                             CameraGroup::kPreDryer, CameraGroup::kAfterDryer,
                             CameraGroup::kCalenderReel };
    QVERIFY(recorder.triggerEvent(context));

    for (int i = 0; i < kPostFrames * 3; ++i) {
        const int index = kRingCapacity + i;
        recorder.addFrame(1, frame, stampAt(index), index);
        recorder.addFrame(2, frame, stampAt(index), index);
    }
    QTRY_VERIFY_WITH_TIMEOUT(!eventJsons(dataDir.path()).isEmpty(), 30000);

    QCOMPARE(eventBins(dataDir.path()).size(), 2);
}

void TriggerRecordScope::recordGroupsSurviveSaveAndReload()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    OpcUaSettings settings = CameraConfig::getDefaultOpcUaSettings();
    settings.triggerTags.resize(2);
    settings.triggerTags[0].name = QStringLiteral("PRESS-PART SB-01");
    settings.triggerTags[0].group = CameraGroup::kPressPart;
    settings.triggerTags[0].recordGroups = { CameraGroup::kPressPart };
    settings.triggerTags[1].name = QStringLiteral("Trigger 2");
    settings.triggerTags[1].group = CameraGroup::kUnassigned;
    settings.triggerTags[1].recordGroups.clear();
    CameraConfig::setOpcUaSettings(settings);

    const OpcUaSettings loaded = CameraConfig::getOpcUaSettings();
    QCOMPARE(loaded.triggerTags.size(), static_cast<size_t>(2));
    QCOMPARE(loaded.triggerTags[0].recordGroups.size(), static_cast<size_t>(1));
    QCOMPARE(loaded.triggerTags[0].recordGroups.front(), CameraGroup::kPressPart);
    // An unset scope stays unset: it means every section.
    QVERIFY(loaded.triggerTags[1].recordGroups.empty());
}
