#include "EventCaptureTiming.h"

#include "config/CameraConfig.h"
#include "core/EventController.h"
#include "core/EventDatabase.h"
#include "core/VideoStreamReader.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// Both cameras REPORT the nominal rate — ResultingFrameRateAbs keeps saying 55
// even while the host is dropping half the frames in transit. That lie is the
// whole point: the recorder must not turn it into a capture window.
constexpr double kNominalFps = 55.0;
constexpr double kHealthyIntervalMs = 1000.0 / kNominalFps;   // ~18.2 ms
constexpr double kDroppingIntervalMs = 2000.0 / kNominalFps;  // ~36.4 ms: every 2nd frame lost
constexpr int kPreFrames = 550;                               // 10 s at the nominal rate
constexpr int kPostFrames = 110;                              // 2 s at the nominal rate
constexpr int kRingCapacity = kPreFrames + kPostFrames;

// Tolerance for windows that can only close on a frame boundary: one interval
// of the slowest camera plus rounding.
constexpr double kWindowToleranceSeconds = 0.06;

struct CaptureInfo {
    bool opened = false;
    double fps = 0.0;
    int totalFrames = 0;
    int triggerIndex = 0;

    int postFrames() const { return totalFrames - 1 - triggerIndex; }
    double postSeconds() const { return fps > 0.0 ? postFrames() / fps : 0.0; }
};

CaptureInfo readCapture(const QString& path)
{
    CaptureInfo info;
    VideoStreamReader reader;
    if (!reader.open(path)) {
        return info;
    }
    info.opened = true;
    info.fps = reader.getFps();
    info.totalFrames = reader.getTotalFrames();
    info.triggerIndex = reader.getTriggerIndex();
    return info;
}

QStringList eventBins(const QString& dir)
{
    return QDir(dir).entryList(QStringList() << QStringLiteral("event_*_cam*.bin"),
                               QDir::Files, QDir::Name);
}

// The event's JSON is written after every camera file, so its arrival is the
// point at which the event is complete on disk. isSaving() cannot be used for
// that: it is already false while the save worker is still writing.
QStringList eventJsons(const QString& dir)
{
    return QDir(dir).entryList(QStringList() << QStringLiteral("event_*.json"),
                               QDir::Files, QDir::Name);
}

// EventController's private kCameraLiveWindowMs: a camera silent for longer
// than this stops counting as streaming.
constexpr qint64 kLiveWindowMs = 4000;

} // namespace

void EventCaptureTiming::droppingCameraRecordsTheSamePostWindowInSeconds()
{
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());

    // Isolate every path the recorder touches: camera config and event storage.
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    CameraConfig::setEventStoragePath(dataDir.path());
    EventDatabase::instance().initialize(dataDir.path());

    EventController& recorder = EventController::instance();
    recorder.initialize(kPreFrames, kNominalFps, kPostFrames);
    recorder.setCameraFpsProvider([](int) { return kNominalFps; });

    const cv::Mat frame(8, 8, CV_8UC1, cv::Scalar(120));
    const int64_t baseNs = 1000000000LL;
    const auto stampAt = [baseNs](double intervalMs, int index) {
        return baseNs + static_cast<int64_t>(std::llround(index * intervalMs * 1e6));
    };

    // Fill both ring buffers: camera 1 every frame, camera 2 every second frame.
    for (int i = 0; i < kRingCapacity; ++i) {
        recorder.addFrame(1, frame, stampAt(kHealthyIntervalMs, i), i);
        recorder.addFrame(2, frame, stampAt(kDroppingIntervalMs, i), i);
    }

    EventController::TriggerContext context;  // no group: every camera records
    QVERIFY(recorder.triggerEvent(context));

    // Run both cameras well past their windows.
    for (int i = 0; i < kPostFrames * 3; ++i) {
        const int index = kRingCapacity + i;
        recorder.addFrame(1, frame, stampAt(kHealthyIntervalMs, index), index);
        recorder.addFrame(2, frame, stampAt(kDroppingIntervalMs, index), index);
    }
    QTRY_VERIFY_WITH_TIMEOUT(!eventJsons(dataDir.path()).isEmpty(), 30000);

    const QStringList bins = eventBins(dataDir.path());
    QCOMPARE(bins.size(), 2);

    QString cam1Path;
    QString cam2Path;
    for (const QString& bin : bins) {
        if (bin.contains(QStringLiteral("_cam1.bin"))) {
            cam1Path = QDir(dataDir.path()).filePath(bin);
        } else if (bin.contains(QStringLiteral("_cam2.bin"))) {
            cam2Path = QDir(dataDir.path()).filePath(bin);
        }
    }
    QVERIFY(!cam1Path.isEmpty());
    QVERIFY(!cam2Path.isEmpty());

    // The full ring was captured by both: the window is what happens AFTER the
    // trigger, so it is the part that has to match across cameras.
    const CaptureInfo healthy = readCapture(cam1Path);
    const CaptureInfo dropping = readCapture(cam2Path);
    QVERIFY(healthy.opened);
    QVERIFY(dropping.opened);
    QCOMPARE(healthy.totalFrames, kRingCapacity);
    QCOMPARE(dropping.totalFrames, kRingCapacity);

    // Each .bin reports the rate its camera really delivered, so the post
    // window can be read back in seconds.
    QVERIFY2(std::abs(healthy.fps - kNominalFps) < 1.0,
             qPrintable(QString("healthy camera header fps %1, expected ~%2")
                            .arg(healthy.fps).arg(kNominalFps)));
    QVERIFY2(std::abs(dropping.fps - kNominalFps / 2.0) < 1.0,
             qPrintable(QString("dropping camera header fps %1, expected ~%2")
                            .arg(dropping.fps).arg(kNominalFps / 2.0)));

    const double expectedSeconds = static_cast<double>(kPostFrames) / kNominalFps;
    QVERIFY2(std::abs(healthy.postSeconds() - expectedSeconds) < kWindowToleranceSeconds,
             qPrintable(QString("healthy camera recorded %1 s of post-trigger, expected %2 s")
                            .arg(healthy.postSeconds()).arg(expectedSeconds)));
    // The regression: this used to be ~4 s — the frame target was reached at
    // the camera's own (delivered) pace instead of the configured window.
    QVERIFY2(std::abs(dropping.postSeconds() - expectedSeconds) < kWindowToleranceSeconds,
             qPrintable(QString("dropping camera recorded %1 s of post-trigger, expected %2 s")
                            .arg(dropping.postSeconds()).arg(expectedSeconds)));
}

void EventCaptureTiming::silentCameraIsRecordedAsMissing()
{
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    CameraConfig::setEventStoragePath(dataDir.path());
    EventDatabase::instance().initialize(dataDir.path());

    EventController& recorder = EventController::instance();
    recorder.initialize(kPreFrames, kNominalFps, kPostFrames);
    recorder.setCameraFpsProvider([](int) { return kNominalFps; });

    const cv::Mat frame(8, 8, CV_8UC1, cv::Scalar(120));
    const int64_t baseNs = 2000000000LL;
    const auto stampAt = [baseNs](int index) {
        return baseNs + static_cast<int64_t>(std::llround(index * kHealthyIntervalMs * 1e6));
    };

    for (int i = 0; i < kRingCapacity; ++i) {
        recorder.addFrame(1, frame, stampAt(i), i);
        recorder.addFrame(2, frame, stampAt(i), i);
    }

    EventController::TriggerContext context;
    QVERIFY(recorder.triggerEvent(context));

    // Camera 2 goes silent from here on; camera 1 keeps running, which is what
    // the line looks like with one dead camera. The recorder must wait camera 2
    // out on the wall clock (the live window), then complete on camera 1 alone
    // and say on the event that camera 2 was missing.
    int index = kRingCapacity;
    QElapsedTimer timer;
    timer.start();
    while (eventJsons(dataDir.path()).isEmpty() && timer.elapsed() < kLiveWindowMs + 6000) {
        recorder.addFrame(1, frame, stampAt(index), index);
        ++index;
        QTest::qWait(10);
    }
    QVERIFY2(!eventJsons(dataDir.path()).isEmpty(),
             "the event never completed while one camera stayed silent");

    const QStringList bins = eventBins(dataDir.path());
    QCOMPARE(bins.size(), 1);
    QVERIFY(bins.first().contains(QStringLiteral("_cam1.bin")));

    // event_<timestamp>_cam1.bin
    const QString timestamp = bins.first().mid(QStringLiteral("event_").size(),
                                               bins.first().size()
                                                   - QStringLiteral("event_").size()
                                                   - QStringLiteral("_cam1.bin").size());
    QVERIFY(!timestamp.isEmpty());

    bool registered = false;
    std::vector<int> missing;
    try {
        missing = EventDatabase::instance().getEventInfo(timestamp).missingCameraIds;
        registered = true;
    } catch (...) {
    }
    QVERIFY2(registered, "the recorded event was not registered in the database");
    QCOMPARE(missing.size(), static_cast<size_t>(1));
    QVERIFY2(missing.front() == 2, "the silent participating camera was not recorded as missing");

    // The metadata has to survive a reload, so the trace is still there when the
    // event list is rebuilt from disk.
    const QString jsonPath = QDir(dataDir.path())
                                 .filePath(QStringLiteral("event_%1.json").arg(timestamp));
    QVERIFY(QFileInfo::exists(jsonPath));
    const EventDatabase::EventInfo reloaded = EventDatabase::loadMetadata(jsonPath);
    QCOMPARE(reloaded.missingCameraIds.size(), static_cast<size_t>(1));
    QCOMPARE(reloaded.missingCameraIds.front(), 2);
}
