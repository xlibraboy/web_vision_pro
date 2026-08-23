#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QFutureWatcher>
#include <atomic>

/**
 * EventSignalScanner - Offline per-camera signal extraction for the event
 * dashboard.
 *
 * Decodes a RAW event file with an adaptive stride (bounded work regardless
 * of future frame counts / resolutions) and computes:
 *  - mean brightness per sampled frame (the time-series curve)
 *  - exact frames where DefectDetector reports a hit
 *
 * Results are cached next to the .bin as "<bin>.signal.json", invalidated
 * automatically when the .bin size or mtime changes.
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

    static QString cachePathForBin(const QString& binPath);

    // Cache I/O. loadCache returns false when absent or stale.
    static bool loadCache(const QString& binPath,
                          QVector<int>* sampleFrames,
                          QVector<double>* brightness,
                          QVector<int>* defectFrames,
                          int* totalFrames,
                          double* fps);
    static void saveCache(const QString& binPath, int stride,
                          const QVector<int>& sampleFrames,
                          const QVector<double>& brightness,
                          const QVector<int>& defectFrames,
                          int totalFrames, double fps);

signals:
    void progress(int scanned, int totalSteps);
    void finished(const QString& binPath,
                  const QVector<int>& sampleFrames,
                  const QVector<double>& brightness,
                  const QVector<int>& defectFrames,
                  int totalFrames,
                  double fps);
    void failed(const QString& binPath, const QString& reason);

private:
    bool runScan(const QString& binPath);

    QFutureWatcher<bool> watcher_;
    QString binPath_;
    QString failureReason_;
    std::atomic<bool> cancelled_{false};
};
