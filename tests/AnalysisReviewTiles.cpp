#include "AnalysisReviewTiles.h"

#include "config/CameraConfig.h"
#include "config/UiTypeScale.h"
#include "core/RawFormat.h"
#include "gui/AnalysisView.h"
#include "gui/widgets/AnalysisVideoWidget.h"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

// A minimal but valid per-camera recording: the 1024-byte header, then for every
// frame its pixel data followed by the 64-byte metadata - the order
// EventController::saveAsRaw and VideoStreamReader agree on. Version and both
// clock series are parameterized so tests can build cross-comparable and
// camera-local (PTP-less) recordings alike.
bool writeEventBin(const QString& path, int frames, uint32_t version = RAW_FILE_VERSION,
                   int64_t clockBaseNs = 0, int64_t clockIntervalNs = 40000000LL,
                   int64_t hostBaseNs = 0, int64_t hostIntervalNs = 40000000LL)
{
    const int width = 16;
    const int height = 12;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    RawFileHeader header = {};
    std::memcpy(header.magic, RAW_FILE_MAGIC, 4);
    header.version = version;
    header.width = static_cast<uint32_t>(width);
    header.height = static_cast<uint32_t>(height);
    header.pixelFormat = 0;  // Mono8
    header.fps = 25.0;
    header.totalFrames = static_cast<uint32_t>(frames);
    header.triggerIndex = static_cast<uint32_t>(frames / 2);
    if (file.write(reinterpret_cast<const char*>(&header), sizeof(header)) != sizeof(header)) {
        return false;
    }
    const auto at = [](int64_t base, int64_t interval, int i) {
        return static_cast<uint64_t>(std::max<int64_t>(0, base + interval * i));
    };
    const std::vector<char> pixels(static_cast<size_t>(width) * height, 128);
    for (int i = 0; i < frames; ++i) {
        FrameMetadata meta = {};
        meta.timestamp = at(clockBaseNs, clockIntervalNs, i);
        meta.frameId = static_cast<uint64_t>(i);
        meta.flags = (i == frames / 2) ? 1u : 0u;
        meta.hostTimestamp = at(hostBaseNs, hostIntervalNs, i);
        if (file.write(pixels.data(), static_cast<qint64>(pixels.size()))
                != static_cast<qint64>(pixels.size())) {
            return false;
        }
        if (file.write(reinterpret_cast<const char*>(&meta), sizeof(meta)) != sizeof(meta)) {
            return false;
        }
    }
    return true;
}

// The tile the view holds for a 0-based camera index, or nullptr.
AnalysisVideoWidget* tileFor(AnalysisView& view, int camIdx)
{
    const auto tiles = view.findChildren<AnalysisVideoWidget*>();
    for (AnalysisVideoWidget* tile : tiles) {
        if (tile->getCameraId() == camIdx) {
            return tile;
        }
    }
    return nullptr;
}

// Camera 1 has no section (as in the machine reference), cameras 2-6 cover the
// five sections in order.
std::vector<CameraInfo> cameraLineup()
{
    std::vector<CameraInfo> cameras(6);
    for (int i = 0; i < 6; ++i) {
        cameras[static_cast<size_t>(i)].id = i + 1;
        cameras[static_cast<size_t>(i)].name = QString("DRYER %1").arg(i + 1);
        cameras[static_cast<size_t>(i)].group = (i == 0)
            ? CameraGroup::kUnassigned
            : i - 1;
        cameras[static_cast<size_t>(i)].machinePosition = 16600 + i * 300;
    }
    return cameras;
}

} // namespace

void AnalysisReviewTiles::camerasLeftOutOfTheEventAreMarked()
{
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    CameraConfig::setEventStoragePath(dataDir.path());
    CameraConfig::saveCameras(cameraLineup());

    // An event with files for cameras 2 (Wire) and 3 (Press-Part) only - what a
    // Press-Part trigger narrowed to Wire + Press-Part records.
    const QString timestamp = QStringLiteral("20260101_120000_000");
    const QString eventBase = QDir(dataDir.path()).filePath(QString("event_%1").arg(timestamp));
    QVERIFY(writeEventBin(QString("%1_cam2.bin").arg(eventBase), 8));
    QVERIFY(writeEventBin(QString("%1_cam3.bin").arg(eventBase), 8));

    AnalysisView view(6);
    view.startReviewFromFile(QString("%1_cam2.bin").arg(eventBase), 4);

    // Every configured camera gets a tile; the four the event did not record say
    // so, the two it did stay playable.
    for (int camIdx = 0; camIdx < 6; ++camIdx) {
        AnalysisVideoWidget* tile = tileFor(view, camIdx);
        QVERIFY2(tile != nullptr, qPrintable(QString("no tile for camera index %1").arg(camIdx)));
        const bool recorded = (camIdx == 1 || camIdx == 2);
        QVERIFY2(tile->isNotRecorded() == !recorded,
                 qPrintable(QString("camera index %1: not-recorded marker %2, expected %3")
                                .arg(camIdx)
                                .arg(tile->isNotRecorded() ? "shown" : "missing")
                                .arg(recorded ? "missing" : "shown")));
    }
}

// A PTP-less fleet stamps every frame from its own camera counter, so the
// sensor clocks are not cross-comparable. Review must then align through the
// shared host arrival clock stored alongside the camera stamp (v2 .bin), not
// through the old shared index.
void AnalysisReviewTiles::hostClockMapsWhenCameraClocksDisagree()
{
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    CameraConfig::setEventStoragePath(dataDir.path());
    CameraConfig::saveCameras(cameraLineup());

    const QString timestamp = QStringLiteral("20260102_120000_000");
    const QString eventBase = QDir(dataDir.path()).filePath(QString("event_%1").arg(timestamp));

    // cam2: 25 fps, camera clock from 0. cam3: 12.5 fps, camera clock from an
    // unrelated camera-local epoch (1e15 ns ≈ 11.6 days) - both stamped host
    // arrival on the one PC clock.
    const int64_t hostBase = 1700000000000000000LL;  // arbitrary Unix ns
    QVERIFY(writeEventBin(QString("%1_cam2.bin").arg(eventBase), 8,
                          RAW_FILE_VERSION, 0, 40000000LL, hostBase, 40000000LL));
    QVERIFY(writeEventBin(QString("%1_cam3.bin").arg(eventBase), 8,
                          RAW_FILE_VERSION, 1000000000000000LL, 80000000LL,
                          hostBase, 80000000LL));

    AnalysisView view(6);
    view.startReviewFromFile(QString("%1_cam2.bin").arg(eventBase), 4);

    // Timeline = cam2 (equal length, lowest index). Timeline frame 4 is 160 ms
    // in; cam3's nearest own frame on the host clock is frame 2 (2 × 80 ms) -
    // the sensor clocks disagree, so the shared index would have said frame 4.
    QCOMPARE(view.displayedFrameIndexForCamera(2, 4), 2);
    // Mark alignment maps the other way and must follow the same clock.
    QCOMPARE(view.tlIndexOfOwnFrame(2, 2), 4);

    // A v1 recording has no host stamp (the same bytes were zeroed padding):
    // the mapping must fall back to the shared index, not read padding.
    const QString v1Base = QDir(dataDir.path()).filePath(
        QStringLiteral("event_20260102_130000_000"));
    QVERIFY(writeEventBin(QString("%1_cam2.bin").arg(v1Base), 8,
                          1, 0, 40000000LL, hostBase, 40000000LL));
    QVERIFY(writeEventBin(QString("%1_cam3.bin").arg(v1Base), 8,
                          1, 1000000000000000LL, 80000000LL, hostBase, 80000000LL));

    AnalysisView v1View(6);
    v1View.startReviewFromFile(QString("%1_cam2.bin").arg(v1Base), 4);
    QCOMPARE(v1View.displayedFrameIndexForCamera(2, 4), 4);
}

// The metadata HUD is one flat "TS: … | FC: … | Time: …" string. A camera tile
// in the 4-column All Camera grid is too narrow to hold it beside the playback
// info, and the unclamped text used to run under the right-aligned playback
// numbers. It must stack at its separators instead, with the groups intact and
// in order, while the wide single-camera view keeps the one-line strip.
void AnalysisReviewTiles::metadataStacksWhenTheTileIsNarrow()
{
    QFont font(UiTypeScale::monoFamily());
    font.setPixelSize(UiTypeScale::kOverlayMono);
    font.setBold(true);
    const QFontMetrics fm(font);

    const QString metadata = QStringLiteral(
        "TS: 123456.789123456 s | FC: 1234567 | Time: 12:34:56.789");

    // Wide enough for the whole strip: the single line is kept.
    const int oneLineW = fm.horizontalAdvance(metadata);
    QCOMPARE(AnalysisVideoWidget::metadataLines(metadata, font, oneLineW + 1).size(), 1);

    // A tile narrower than the strip: the groups stack instead of overprinting.
    const int narrowW = oneLineW * 2 / 3;
    const QStringList lines = AnalysisVideoWidget::metadataLines(metadata, font, narrowW);
    QVERIFY2(lines.size() > 1, qPrintable(QString("got %1 line(s)").arg(lines.size())));
    for (const QString& line : lines) {
        QVERIFY2(fm.horizontalAdvance(line) <= narrowW, qPrintable(line));
    }

    // No group is dropped or reordered by the break-up.
    QStringList stackedGroups;
    for (const QString& line : lines) {
        stackedGroups += line.split(QStringLiteral(" | "));
    }
    QCOMPARE(stackedGroups, metadata.split(QStringLiteral(" | ")));
}
