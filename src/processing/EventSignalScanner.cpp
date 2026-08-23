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
constexpr int kCacheVersion = 2;
constexpr int kChangePixelThreshold = 25; // gray levels

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

double changedPixelPct(const cv::Mat& cur, const cv::Mat& baseline) {
    if (cur.empty() || baseline.empty() || cur.size() != baseline.size()) {
        return 0.0;
    }
    cv::Mat diff;
    cv::absdiff(cur, baseline, diff);
    // Count pixels whose absolute difference exceeds the threshold.
    const int total = diff.rows * diff.cols;
    if (total <= 0) {
        return 0.0;
    }
    int changed = 0;
    if (diff.channels() == 1) {
        changed = cv::countNonZero(diff > kChangePixelThreshold);
    } else {
        // Grayscale-project multi-channel frames before comparing.
        cv::Mat gCur, gBase, gDiff;
        if (cur.channels() == 3) cv::cvtColor(cur, gCur, cv::COLOR_BGR2GRAY);
        else cv::cvtColor(cur, gCur, cv::COLOR_BGRA2GRAY);
        if (baseline.channels() == 3) cv::cvtColor(baseline, gBase, cv::COLOR_BGR2GRAY);
        else cv::cvtColor(baseline, gBase, cv::COLOR_BGRA2GRAY);
        cv::absdiff(gCur, gBase, gDiff);
        changed = cv::countNonZero(gDiff > kChangePixelThreshold);
    }
    return 100.0 * changed / total;
}

} // namespace

EventSignalScanner::EventSignalScanner(QObject* parent)
    : QObject(parent) {
    static bool metaRegistered = []() {
        qRegisterMetaType<EventSignalData>("EventSignalData");
        return true;
    }();
    Q_UNUSED(metaRegistered);

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
        EventSignalData data;
        if (!loadCache(path, &data)) {
            emit failed(path, QStringLiteral("cache write failed"));
            return;
        }
        emit finished(path, data);
    });
}

void EventSignalScanner::scanAsync(const QString& binPath) {
    if (isRunning()) {
        return;
    }
    cancelled_.store(false);
    failureReason_.clear();
    binPath_ = binPath;

    EventSignalData cached;
    if (loadCache(binPath, &cached)) {
        emit finished(binPath, cached);
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
    EventSignalData out;
    out.totalFrames = totalFrames;
    out.fps = fps;

    cv::Mat baseline; // first sampled frame — reference for local anomalies
    int lastPercent = -1;
    for (int step = 0; step < steps; ++step) {
        if (cancelled_.load()) {
            return false; // Cancelled: no cache write, no finished signal.
        }
        const int frameIndex = step * stride;
        cv::Mat frame = reader.getFrame(frameIndex);
        if (!frame.empty()) {
            cv::Mat gray;
            if (frame.channels() == 1) {
                gray = frame;
            } else if (frame.channels() == 3) {
                cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            } else {
                cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
            }
            const cv::Scalar m, s;
            cv::meanStdDev(gray, m, s);
            out.brightness.push_back(m[0]);
            out.stddev.push_back(s[0]);
            out.sampleFrames.push_back(frameIndex);

            if (baseline.empty()) {
                baseline = gray.clone();
                out.changePct.push_back(0.0);
            } else {
                out.changePct.push_back(changedPixelPct(gray, baseline));
            }

            if (detector.detect(frame)) {
                out.defectBrightness.push_back(frameIndex);
            }
            if (out.changePct.back() >= kLocalChangeMinPct) {
                out.defectLocal.push_back(frameIndex);
            }
            if (s[0] < kLowContrastStd) {
                out.defectContrast.push_back(frameIndex);
            }
        } else {
            // Keep series aligned with the sample grid.
            out.brightness.push_back(out.brightness.isEmpty() ? 0.0 : out.brightness.back());
            out.stddev.push_back(out.stddev.isEmpty() ? 0.0 : out.stddev.back());
            out.changePct.push_back(out.changePct.isEmpty() ? 0.0 : out.changePct.back());
            out.sampleFrames.push_back(frameIndex);
        }
        const int percent = (step * 100) / steps;
        if (percent != lastPercent && percent % 5 == 0) {
            lastPercent = percent;
            emit progress(step + 1, steps);
        }
    }

    saveCache(binPath, stride, out);
    return true;
}

QString EventSignalScanner::cachePathForBin(const QString& binPath) {
    return binPath + QStringLiteral(".signal.json");
}

bool EventSignalScanner::loadCache(const QString& binPath, EventSignalData* out) {
    *out = EventSignalData();

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

    auto readDoubles = [&obj](const char* key) {
        QVector<double> v;
        const QJsonArray arr = obj.value(QLatin1String(key)).toArray();
        v.reserve(arr.size());
        for (const auto& x : arr) v.append(x.toDouble());
        return v;
    };
    auto readInts = [&obj](const char* key) {
        QVector<int> v;
        const QJsonArray arr = obj.value(QLatin1String(key)).toArray();
        v.reserve(arr.size());
        for (const auto& x : arr) v.append(x.toInt());
        return v;
    };

    out->sampleFrames = readInts("sampleFrames");
    out->brightness = readDoubles("brightness");
    out->stddev = readDoubles("stddev");
    out->changePct = readDoubles("changePct");
    out->defectBrightness = readInts("defectFrames");
    out->defectLocal = readInts("localFrames");
    out->defectContrast = readInts("contrastFrames");
    if (out->sampleFrames.isEmpty()
            || out->brightness.size() != out->sampleFrames.size()
            || out->stddev.size() != out->sampleFrames.size()
            || out->changePct.size() != out->sampleFrames.size()) {
        return false;
    }
    out->totalFrames = obj.value(QStringLiteral("totalFrames")).toInt();
    out->fps = obj.value(QStringLiteral("fps")).toDouble();
    return true;
}

void EventSignalScanner::saveCache(const QString& binPath, int stride, const EventSignalData& data) {
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
    obj.insert(QStringLiteral("totalFrames"), data.totalFrames);
    obj.insert(QStringLiteral("fps"), data.fps);

    QJsonArray sArr;
    for (int v : data.sampleFrames) sArr.append(v);
    obj.insert(QStringLiteral("sampleFrames"), sArr);

    QJsonArray bArr;
    for (double v : data.brightness) bArr.append(v);
    obj.insert(QStringLiteral("brightness"), bArr);

    QJsonArray sdArr;
    for (double v : data.stddev) sdArr.append(v);
    obj.insert(QStringLiteral("stddev"), sdArr);

    QJsonArray cArr;
    for (double v : data.changePct) cArr.append(v);
    obj.insert(QStringLiteral("changePct"), cArr);

    QJsonArray dArr;
    for (int v : data.defectBrightness) dArr.append(v);
    obj.insert(QStringLiteral("defectFrames"), dArr);

    QJsonArray lArr;
    for (int v : data.defectLocal) lArr.append(v);
    obj.insert(QStringLiteral("localFrames"), lArr);

    QJsonArray kArr;
    for (int v : data.defectContrast) kArr.append(v);
    obj.insert(QStringLiteral("contrastFrames"), kArr);

    QFile f(cachePathForBin(binPath));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        f.close();
    }
}
