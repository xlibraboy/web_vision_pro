#include "EventSignalScanner.h"

#include "DefectDetector.h"
#include "../core/VideoStreamReader.h"

#include <QtConcurrent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

constexpr int kCacheVersion = 1;

int strideForTotalFrames(int totalFrames) {
    if (totalFrames <= EventSignalScanner::kMaxScannedFrames) {
        return 1;
    }
    return static_cast<int>((totalFrames + EventSignalScanner::kMaxScannedFrames - 1)
                            / EventSignalScanner::kMaxScannedFrames);
}

bool statBinFile(const QString& binPath, qint64* sizeOut, qint64* mtimeOut) {
    const QFileInfo fi(binPath);
    if (!fi.exists() || fi.size() <= 0) {
        return false;
    }
    *sizeOut = fi.size();
    *mtimeOut = static_cast<qint64>(fi.lastModified().toSecsSinceEpoch());
    return true;
}

} // namespace

EventSignalScanner::EventSignalScanner(QObject* parent)
    : QObject(parent) {
    connect(&watcher_, &QFutureWatcher<bool>::finished, this, [this]() {
        const QString path = binPath_;
        const bool ok = watcher_.result();
        if (!ok) {
            if (!cancelled_.load()) {
                emit failed(path, failureReason_.isEmpty() ? QStringLiteral("scan failed")
                                                           : failureReason_);
            }
            return;
        }
        QVector<int> samples;
        QVector<double> brightness;
        QVector<int> defects;
        int totalFrames = 0;
        double fps = 0.0;
        if (!loadCache(path, &samples, &brightness, &defects, &totalFrames, &fps)) {
            emit failed(path, QStringLiteral("cache write failed"));
            return;
        }
        emit finished(path, samples, brightness, defects, totalFrames, fps);
    });
}

void EventSignalScanner::scanAsync(const QString& binPath) {
    if (isRunning()) {
        return;
    }
    cancelled_.store(false);
    failureReason_.clear();
    binPath_ = binPath;

    QVector<int> samples;
    QVector<double> brightness;
    QVector<int> defects;
    int totalFrames = 0;
    double fps = 0.0;
    if (loadCache(binPath, &samples, &brightness, &defects, &totalFrames, &fps)) {
        emit finished(binPath, samples, brightness, defects, totalFrames, fps);
        return;
    }

    watcher_.setFuture(QtConcurrent::run([this, binPath]() {
        QVector<int> unused;
        Q_UNUSED(unused);
        return runScan(binPath);
    }));
}

void EventSignalScanner::cancel() {
    cancelled_.store(true);
}

bool EventSignalScanner::isRunning() const {
    return watcher_.isRunning();
}

bool EventSignalScanner::runScan(const QString& binPath) {
    qint64 size = 0;
    qint64 mtime = 0;
    if (!statBinFile(binPath, &size, &mtime)) {
        failureReason_ = QStringLiteral("file missing or empty");
        return false;
    }

    VideoStreamReader reader;
    if (!reader.open(binPath)) {
        failureReason_ = QStringLiteral("cannot open RAW file");
        return false;
    }

    const int totalFrames = reader.getTotalFrames();
    if (totalFrames <= 0) {
        failureReason_ = QStringLiteral("no frames in file");
        return false;
    }

    const double fps = reader.getFps();
    const int stride = strideForTotalFrames(totalFrames);
    const int steps = (totalFrames + stride - 1) / stride;

    DefectDetector detector;
    QVector<int> sampleFrames;
    QVector<double> brightness;
    QVector<int> defectFrames;
    sampleFrames.reserve(steps);
    brightness.reserve(steps);

    int lastPercent = -1;
    for (int step = 0; step < steps; ++step) {
        if (cancelled_.load()) {
            return false; // Cancelled: no cache write, no finished signal.
        }
        const int frameIndex = step * stride;
        cv::Mat frame = reader.getFrame(frameIndex);
        if (!frame.empty()) {
            const cv::Scalar mean = cv::mean(frame);
            double m = 0.0;
            for (int c = 0; c < frame.channels() && c < 4; ++c) {
                m += mean[c];
            }
            brightness.push_back(m / std::max(1, frame.channels()));
            sampleFrames.push_back(frameIndex);
            if (detector.detect(frame)) {
                defectFrames.push_back(frameIndex);
            }
        } else {
            // Keep the curve aligned with the sample grid.
            brightness.push_back(brightness.isEmpty() ? 0.0 : brightness.back());
            sampleFrames.push_back(frameIndex);
        }
        const int percent = (step * 100) / steps;
        if (percent != lastPercent && percent % 5 == 0) {
            lastPercent = percent;
            emit progress(step + 1, steps);
        }
    }

    saveCache(binPath, stride, sampleFrames, brightness, defectFrames, totalFrames, fps);
    return true;
}

QString EventSignalScanner::cachePathForBin(const QString& binPath) {
    return binPath + QStringLiteral(".signal.json");
}

bool EventSignalScanner::loadCache(const QString& binPath,
                                   QVector<int>* sampleFrames,
                                   QVector<double>* brightness,
                                   QVector<int>* defectFrames,
                                   int* totalFrames,
                                   double* fps) {
    sampleFrames->clear();
    brightness->clear();
    defectFrames->clear();
    *totalFrames = 0;
    *fps = 0.0;

    QFile f(cachePathForBin(binPath));
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("version")).toInt() != kCacheVersion) {
        return false;
    }
    qint64 size = 0;
    qint64 mtime = 0;
    if (!statBinFile(binPath, &size, &mtime)) {
        return false;
    }
    if (obj.value(QStringLiteral("binSize")).toDouble() != static_cast<double>(size)
            || obj.value(QStringLiteral("binMtime")).toDouble() != static_cast<double>(mtime)) {
        return false;
    }

    const QJsonArray sArr = obj.value(QStringLiteral("sampleFrames")).toArray();
    const QJsonArray bArr = obj.value(QStringLiteral("brightness")).toArray();
    if (sArr.size() != bArr.size() || sArr.isEmpty()) {
        return false;
    }
    sampleFrames->reserve(sArr.size());
    brightness->reserve(bArr.size());
    for (int i = 0; i < sArr.size(); ++i) {
        sampleFrames->push_back(sArr.at(i).toInt());
        brightness->push_back(bArr.at(i).toDouble());
    }
    const QJsonArray dArr = obj.value(QStringLiteral("defectFrames")).toArray();
    for (const auto& v : dArr) {
        defectFrames->push_back(v.toInt());
    }
    *totalFrames = obj.value(QStringLiteral("totalFrames")).toInt();
    *fps = obj.value(QStringLiteral("fps")).toDouble();
    return true;
}

void EventSignalScanner::saveCache(const QString& binPath, int stride,
                                   const QVector<int>& sampleFrames,
                                   const QVector<double>& brightness,
                                   const QVector<int>& defectFrames,
                                   int totalFrames, double fps) {
    qint64 size = 0;
    qint64 mtime = 0;
    if (!statBinFile(binPath, &size, &mtime)) {
        return;
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("version"), kCacheVersion);
    obj.insert(QStringLiteral("binSize"), static_cast<double>(size));
    obj.insert(QStringLiteral("binMtime"), static_cast<double>(mtime));
    obj.insert(QStringLiteral("stride"), stride);
    obj.insert(QStringLiteral("totalFrames"), totalFrames);
    obj.insert(QStringLiteral("fps"), fps);
    QJsonArray sArr;
    for (int v : sampleFrames) {
        sArr.append(v);
    }
    obj.insert(QStringLiteral("sampleFrames"), sArr);
    QJsonArray bArr;
    for (double v : brightness) {
        bArr.append(v);
    }
    obj.insert(QStringLiteral("brightness"), bArr);
    QJsonArray dArr;
    for (int v : defectFrames) {
        dArr.append(v);
    }
    obj.insert(QStringLiteral("defectFrames"), dArr);

    QFile f(cachePathForBin(binPath));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        f.close();
    }
}
