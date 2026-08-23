#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QFutureWatcher>
#include <atomic>

/**
 * One event's per-camera scan result (all series share sampleFrames indices).
 */
struct EventSignalData {
    QVector<int> sampleFrames;        // frame index per sample
    QVector<double> brightness;       // mean 0..255
    QVector<double> stddev;           // contrast 0..128
    QVector<double> spotPct;          // % pixels in small bright/dark spots vs local neighborhood
    QVector<int> defectBrightness;    // brightness-jump hits (DefectDetector)
    QVector<int> defectLocal;         // spot-anomaly hits (local morphology rule)
    QVector<int> defectContrast;      // low-contrast hits
    int totalFrames = 0;
    double fps = 0.0;
};

Q_DECLARE_METATYPE(EventSignalData)

/**
 * EventSignalScanner - Offline per-camera signal extraction for the event
 * dashboard.
 *
 * Decodes a RAW event file with an adaptive stride (bounded work regardless
 * of future frame counts / resolutions) and computes per sampled frame:
 *  - mean brightness and contrast (stddev)
 *  - changed-pixel ratio vs the baseline (first sampled) frame
 *  - three defect rules: brightness jump / local anomaly / low contrast
 *
 * Results are cached next to the .bin as "<bin>.signal.json", invalidated
 * automatically when the .bin size or mtime changes (or format version bumps).
 */
class EventSignalScanner : public QObject {
    Q_OBJECT
public:
    explicit EventSignalScanner(QObject* parent = nullptr);

    void scanAsync(const QString& binPath);
    void cancel();
    bool isRunning() const;

    // File currently assigned to the worker (valid even before it finishes).
    QString currentBinPath() const { return binPath_; }

    // Upper bound on decoded frames per scan.
    static constexpr int kMaxScannedFrames = 600;
    // Spot-anomaly rule: % of pixels in small bright/dark spots vs local
    // neighborhood (motion-invariant, works on a moving web).
    static constexpr double kLocalSpotMinPct = 0.05;
    // Low-contrast rule: stddev below this flags washed-out/flat frames.
    static constexpr double kLowContrastStd = 10.0;

    static QString cachePathForBin(const QString& binPath);

    // Cache I/O. loadCache returns false when absent or stale.
    static bool loadCache(const QString& binPath, EventSignalData* out);
    static void saveCache(const QString& binPath, int stride, const EventSignalData& data);

signals:
    void progress(int scanned, int totalSteps);
    void finished(const QString& binPath, const EventSignalData& data);
    void failed(const QString& binPath, const QString& reason);

private:
    bool runScan(const QString& binPath);

    QFutureWatcher<bool> watcher_;
    QString binPath_;
    QString failureReason_;
    std::atomic<bool> cancelled_{false};
};
