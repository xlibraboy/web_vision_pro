#include "AnalysisReviewTiles.h"

#include "config/CameraConfig.h"
#include "core/RawFormat.h"
#include "gui/AnalysisView.h"
#include "gui/widgets/AnalysisVideoWidget.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <cstring>
#include <vector>

namespace {

// A minimal but valid per-camera recording: the 1024-byte header, then for every
// frame its pixel data followed by the 64-byte metadata - the order
// EventController::saveAsRaw and VideoStreamReader agree on.
bool writeEventBin(const QString& path, int frames)
{
    const int width = 16;
    const int height = 12;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    RawFileHeader header = {};
    std::memcpy(header.magic, RAW_FILE_MAGIC, 4);
    header.version = RAW_FILE_VERSION;
    header.width = static_cast<uint32_t>(width);
    header.height = static_cast<uint32_t>(height);
    header.pixelFormat = 0;  // Mono8
    header.fps = 25.0;
    header.totalFrames = static_cast<uint32_t>(frames);
    header.triggerIndex = static_cast<uint32_t>(frames / 2);
    if (file.write(reinterpret_cast<const char*>(&header), sizeof(header)) != sizeof(header)) {
        return false;
    }
    const std::vector<char> pixels(static_cast<size_t>(width) * height, 128);
    for (int i = 0; i < frames; ++i) {
        FrameMetadata meta = {};
        meta.timestamp = static_cast<uint64_t>(i) * 40000000ULL;  // 25 fps
        meta.frameId = static_cast<uint64_t>(i);
        meta.flags = (i == frames / 2) ? 1u : 0u;
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
