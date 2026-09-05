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
// Bump whenever the computed per-frame metrics change meaning (e.g. the
// ROI-restricted scan introduced in this version) so stale caches recompute.
constexpr int kCacheVersion = 4;
constexpr int kSpotKernel = 31;        // local-neighborhood size
constexpr int kSpotDelta = 50;         // gray levels vs neighborhood

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

// Fraction of pixels forming small bright or dark anomalies relative to
// their LOCAL neighborhood (morphological top-hat / black-hat). Motion of
// the underlying web cancels out, so this works on continuously moving
// paper where a fixed-baseline comparison would saturate.
double spotPixelPct(const cv::Mat& gray) {
    if (gray.empty()) {
        return 0.0;
    }
    const cv::Mat kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE, cv::Size(kSpotKernel, kSpotKernel));
    cv::Mat tophat, blackhat;
    cv::morphologyEx(gray, tophat, cv::MORPH_TOPHAT, kernel);
    cv::morphologyEx(gray, blackhat, cv::MORPH_BLACKHAT, kernel);
    const int total = gray.rows * gray.cols;
    if (total <= 0) {
        return 0.0;
    }
    const int bright = cv::countNonZero(tophat > kSpotDelta);
    const int dark = cv::countNonZero(blackhat > kSpotDelta);
    return 100.0 * (bright + dark) / total;
}

// Same spot anomaly metric, but only counting pixels inside the ROI mask
// (restricted analysis region). Returns 0 when the mask is empty.
double spotPixelPctMasked(const cv::Mat& gray, const cv::Mat& mask) {
    if (gray.empty() || mask.empty() || mask.size() != gray.size()) {
        return 0.0;
    }
    const cv::Mat kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE, cv::Size(kSpotKernel, kSpotKernel));
    cv::Mat tophat, blackhat;
    cv::morphologyEx(gray, tophat, cv::MORPH_TOPHAT, kernel);
    cv::morphologyEx(gray, blackhat, cv::MORPH_BLACKHAT, kernel);
    const int total = cv::countNonZero(mask);
    if (total <= 0) {
        return 0.0;
    }
    cv::Mat brightMask, darkMask;
    cv::bitwise_and(tophat > kSpotDelta, mask, brightMask);
    cv::bitwise_and(blackhat > kSpotDelta, mask, darkMask);
    const int bright = cv::countNonZero(brightMask);
    const int dark = cv::countNonZero(darkMask);
    return 100.0 * (bright + dark) / total;
}

// Build a CV_8U analysis mask (255 inside) from a normalized (0..1) polygon
// for a frame of the given size. Returns empty when the polygon is invalid so
// callers treat it as "no restriction".
cv::Mat buildRoiMask(const QVector<QPointF>& roi, int cols, int rows) {
    if (roi.size() < 3 || cols <= 0 || rows <= 0) {
        return cv::Mat();
    }
    std::vector<cv::Point> pts;
    pts.reserve(roi.size());
    for (const QPointF& p : roi) {
        pts.emplace_back(static_cast<int>(p.x() * cols),
                         static_cast<int>(p.y() * rows));
    }
    cv::Mat mask = cv::Mat::zeros(rows, cols, CV_8U);
    cv::fillConvexPoly(mask, pts, cv::Scalar(255));
    return mask;
}

} // namespace

// Canonical signature of an ROI restriction; part of the scan cache key so
// changing the region (or which metrics it gates) invalidates stale results.
QString EventSignalScanner::roiSignature(const QVector<QPointF>& roi,
                                         bool roiCurves, bool roiHits) {
    if (roi.size() < 3) {
        return QStringLiteral("none");
    }
    QString sig;
    for (const QPointF& p : roi) {
        if (!sig.isEmpty()) sig += QLatin1Char(';');
        sig += QString::number(p.x(), 'f', 4) + QLatin1Char(',') + QString::number(p.y(), 'f', 4);
    }
    sig += QStringLiteral("|c%1|h%2").arg(roiCurves ? 1 : 0).arg(roiHits ? 1 : 0);
    return sig;
}

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
        if (!loadCache(path, &data, lastRoiSig_)) {
            emit failed(path, QStringLiteral("cache write failed"));
            return;
        }
        emit finished(path, data);
    });

    static bool windowMetaRegistered = []() {
        qRegisterMetaType<WindowScanResult>("WindowScanResult");
        return true;
    }();
    Q_UNUSED(windowMetaRegistered);

    connect(&windowWatcher_, &QFutureWatcher<WindowScanResult>::finished, this, [this]() {
        const WindowScanResult r = windowWatcher_.result();
        if (r.ok) {
            rememberWindow(r.binPath, r.startFrame, r.endFrame, lastWindowRoiSig_, r.data);
            emit windowFinished(r.binPath, r.startFrame, r.endFrame, r.data);
        } else if (!windowCancelled_.load()) {
            emit windowFailed(r.binPath, r.startFrame, r.endFrame,
                              r.reason.isEmpty() ? QStringLiteral("window scan failed") : r.reason);
        }
    });
}

void EventSignalScanner::scanAsync(const QString& binPath,
                                   const QVector<QPointF>& roi,
                                   bool roiCurves, bool roiHits) {
    if (isRunning()) {
        return;
    }
    cancelled_.store(false);
    failureReason_.clear();
    binPath_ = binPath;
    lastRoiSig_ = roiSignature(roi, roiCurves, roiHits);

    EventSignalData cached;
    if (loadCache(binPath, &cached, lastRoiSig_)) {
        emit finished(binPath, cached);
        return;
    }

    watcher_.setFuture(QtConcurrent::run([this, binPath, roi, roiCurves, roiHits]() {
        return runScan(binPath, roi, roiCurves, roiHits);
    }));
}

void EventSignalScanner::cancel() {
    cancelled_.store(true);
}

bool EventSignalScanner::isRunning() const {
    return watcher_.isRunning();
}

QString EventSignalScanner::windowCacheKey(const QString& binPath, int startFrame, int endFrame,
                                           const QString& roiSig) {
    return QStringLiteral("%1|%2|%3|%4").arg(binPath).arg(startFrame).arg(endFrame).arg(roiSig);
}

bool EventSignalScanner::popCachedWindow(const QString& binPath, int startFrame, int endFrame,
                                         const QString& roiSig, EventSignalData* out) {
    const QString key = windowCacheKey(binPath, startFrame, endFrame, roiSig);
    auto it = windowCache_.find(key);
    if (it == windowCache_.end()) {
        return false;
    }
    *out = it.value();
    // LRU refresh.
    windowCacheOrder_.removeAll(key);
    windowCacheOrder_.append(key);
    return true;
}

void EventSignalScanner::rememberWindow(const QString& binPath, int startFrame, int endFrame,
                                        const QString& roiSig, const EventSignalData& data) {
    const QString key = windowCacheKey(binPath, startFrame, endFrame, roiSig);
    windowCache_.insert(key, data);
    windowCacheOrder_.removeAll(key);
    windowCacheOrder_.append(key);
    while (windowCacheOrder_.size() > kWindowCacheMax) {
        windowCache_.remove(windowCacheOrder_.takeFirst());
    }
}

void EventSignalScanner::scanWindowAsync(const QString& binPath, int startFrame, int endFrame,
                                         const QVector<QPointF>& roi,
                                         bool roiCurves, bool roiHits) {
    if (isWindowRunning()) {
        return;
    }
    const QString roiSig = roiSignature(roi, roiCurves, roiHits);
    EventSignalData cached;
    if (popCachedWindow(binPath, startFrame, endFrame, roiSig, &cached)) {
        emit windowFinished(binPath, startFrame, endFrame, cached);
        return;
    }
    lastWindowRoiSig_ = roiSig;
    windowCancelled_.store(false);
    windowWatcher_.setFuture(QtConcurrent::run(
        [this, binPath, startFrame, endFrame, roi, roiCurves, roiHits]() {
        return runWindowScan(binPath, startFrame, endFrame, roi, roiCurves, roiHits);
    }));
}

void EventSignalScanner::cancelWindow() {
    windowCancelled_.store(true);
}

bool EventSignalScanner::isWindowRunning() const {
    return windowWatcher_.isRunning();
}

WindowScanResult EventSignalScanner::runWindowScan(const QString& binPath, int startFrame,
                                                   int endFrame,
                                                   const QVector<QPointF>& roi,
                                                   bool roiCurves, bool roiHits) {
    WindowScanResult result;
    result.binPath = binPath;
    result.startFrame = startFrame;
    result.endFrame = endFrame;

    VideoStreamReader reader;
    if (!reader.open(binPath)) {
        result.reason = QStringLiteral("cannot open RAW file");
        return result;
    }
    const int totalFrames = reader.getTotalFrames();
    if (totalFrames <= 0) {
        result.reason = QStringLiteral("no frames in file");
        return result;
    }
    // Clamp to the event; require a sane span.
    const int start = std::max(0, startFrame);
    const int end = std::min(endFrame, totalFrames - 1);
    result.startFrame = start;
    result.endFrame = end;
    if (start > end) {
        result.reason = QStringLiteral("empty window");
        return result;
    }

    DefectDetector detector;
    // Seed the stateful brightness-jump detector with the frame before the
    // window so the first in-window sample has a fair baseline.
    if (start > 0) {
        const cv::Mat lookback = reader.getFrame(start - 1);
        if (!lookback.empty()) {
            detector.detect(lookback);
        }
    }

    EventSignalData out;
    out.totalFrames = totalFrames;
    out.fps = reader.getFps();
    // ROI scopes: restrict the signal curves and/or the defect-hit rules to
    // the drawn inspection region (empty polygon = whole frame, no gate).
    const bool maskCurves = roiCurves && roi.size() >= 3;
    const bool maskHits = roiHits && roi.size() >= 3;
    for (int frameIndex = start; frameIndex <= end; ++frameIndex) {
        if (windowCancelled_.load()) {
            result.reason = QStringLiteral("cancelled");
            return result;
        }
        const cv::Mat frame = reader.getFrame(frameIndex);
        if (!frame.empty()) {
            cv::Mat gray;
            if (frame.channels() == 1) {
                gray = frame;
            } else if (frame.channels() == 3) {
                cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            } else {
                cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
            }
            cv::Mat mask;
            if (maskCurves || maskHits) {
                mask = buildRoiMask(roi, gray.cols, gray.rows);
            }
            cv::Scalar m, s;
            if (maskCurves && !mask.empty()) {
                cv::meanStdDev(gray, m, s, mask);
                out.spotPct.push_back(spotPixelPctMasked(gray, mask));
            } else {
                cv::meanStdDev(gray, m, s);
                out.spotPct.push_back(spotPixelPct(gray));
            }
            out.brightness.push_back(m[0]);
            out.stddev.push_back(s[0]);
            out.sampleFrames.push_back(frameIndex);

            if (detector.detect(frame, (maskHits && !mask.empty()) ? mask : cv::Mat())) {
                out.defectBrightness.push_back(frameIndex);
            }
            if (out.spotPct.back() >= kLocalSpotMinPct) {
                out.defectLocal.push_back(frameIndex);
            }
            if (s[0] < kLowContrastStd) {
                out.defectContrast.push_back(frameIndex);
            }
        } else {
            out.brightness.push_back(out.brightness.isEmpty() ? 0.0 : out.brightness.back());
            out.stddev.push_back(out.stddev.isEmpty() ? 0.0 : out.stddev.back());
            out.spotPct.push_back(out.spotPct.isEmpty() ? 0.0 : out.spotPct.back());
            out.sampleFrames.push_back(frameIndex);
        }
    }

    result.data = out;
    result.ok = true;
    return result;
}

bool EventSignalScanner::runScan(const QString& binPath,
                                 const QVector<QPointF>& roi,
                                 bool roiCurves, bool roiHits) {
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
    // ROI scopes: restrict the signal curves and/or the defect-hit rules to
    // the drawn inspection region (empty polygon = whole frame, no gate).
    const bool maskCurves = roiCurves && roi.size() >= 3;
    const bool maskHits = roiHits && roi.size() >= 3;

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
            cv::Mat mask;
            if (maskCurves || maskHits) {
                mask = buildRoiMask(roi, gray.cols, gray.rows);
            }
            cv::Scalar m, s;
            if (maskCurves && !mask.empty()) {
                cv::meanStdDev(gray, m, s, mask);
                out.spotPct.push_back(spotPixelPctMasked(gray, mask));
            } else {
                cv::meanStdDev(gray, m, s);
                out.spotPct.push_back(spotPixelPct(gray));
            }
            out.brightness.push_back(m[0]);
            out.stddev.push_back(s[0]);
            out.sampleFrames.push_back(frameIndex);

            if (detector.detect(frame, (maskHits && !mask.empty()) ? mask : cv::Mat())) {
                out.defectBrightness.push_back(frameIndex);
            }
            if (out.spotPct.back() >= kLocalSpotMinPct) {
                out.defectLocal.push_back(frameIndex);
            }
            if (s[0] < kLowContrastStd) {
                out.defectContrast.push_back(frameIndex);
            }
        } else {
            // Keep series aligned with the sample grid.
            out.brightness.push_back(out.brightness.isEmpty() ? 0.0 : out.brightness.back());
            out.stddev.push_back(out.stddev.isEmpty() ? 0.0 : out.stddev.back());
            out.spotPct.push_back(out.spotPct.isEmpty() ? 0.0 : out.spotPct.back());
            out.sampleFrames.push_back(frameIndex);
        }
        const int percent = (step * 100) / steps;
        if (percent != lastPercent && percent % 5 == 0) {
            lastPercent = percent;
            emit progress(step + 1, steps);
        }
    }

    saveCache(binPath, stride, out, roiSignature(roi, roiCurves, roiHits));
    return true;
}

QString EventSignalScanner::cachePathForBin(const QString& binPath) {
    return binPath + QStringLiteral(".signal.json");
}

bool EventSignalScanner::loadCache(const QString& binPath, EventSignalData* out,
                                   const QString& roiSig) {
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
    // A different inspection region (or scope) invalidates the cached scan.
    if (obj.value(QStringLiteral("roi")).toString() != roiSig) {
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
    out->spotPct = readDoubles("spotPct");
    out->defectBrightness = readInts("defectFrames");
    out->defectLocal = readInts("localFrames");
    out->defectContrast = readInts("contrastFrames");
    if (out->sampleFrames.isEmpty()
            || out->brightness.size() != out->sampleFrames.size()
            || out->stddev.size() != out->sampleFrames.size()
            || out->spotPct.size() != out->sampleFrames.size()) {
        return false;
    }
    out->totalFrames = obj.value(QStringLiteral("totalFrames")).toInt();
    out->fps = obj.value(QStringLiteral("fps")).toDouble();
    return true;
}

void EventSignalScanner::saveCache(const QString& binPath, int stride, const EventSignalData& data,
                                   const QString& roiSig) {
    qint64 size = 0;
    qint64 mtime = 0;
    if (!statBinFile(binPath, &size, &mtime)) {
        return;
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("version"), kCacheVersion);
    obj.insert(QStringLiteral("binSize"), static_cast<double>(size));
    obj.insert(QStringLiteral("binMtime"), static_cast<double>(mtime));
    obj.insert(QStringLiteral("roi"), roiSig);
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
    for (double v : data.spotPct) cArr.append(v);
    obj.insert(QStringLiteral("spotPct"), cArr);

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
