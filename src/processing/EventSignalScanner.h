#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QFutureWatcher>
#include <QHash>
#include <QList>
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
 * Result of one lazy detail-window scan (see scanWindowAsync).
 */
struct WindowScanResult {
    bool ok = false;
    QString binPath;
    int startFrame = 0;
    int endFrame = 0;
    QString reason;
    EventSignalData data;
};
Q_DECLARE_METATYPE(WindowScanResult)

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

    // Lazy detail-window scan: decodes [startFrame-1 .. endFrame] at stride 1
    // (one lookback frame seeds the stateful brightness-jump detector) and
    // computes the same series/defect rules as a full scan, but only for the
    // window. Results are kept in a small in-memory LRU keyed by
    // (binPath, start, end) — no disk cache, windows are cheap to recompute.
    void scanWindowAsync(const QString& binPath, int startFrame, int endFrame);
    void cancelWindow();
    bool isWindowRunning() const;
    static QString windowCacheKey(const QString& binPath, int startFrame, int endFrame);

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
    void windowFinished(const QString& binPath, int startFrame, int endFrame,
                        const EventSignalData& data);
    void windowFailed(const QString& binPath, int startFrame, int endFrame,
                      const QString& reason);

private:
    bool runScan(const QString& binPath);
    WindowScanResult runWindowScan(const QString& binPath, int startFrame, int endFrame);
    bool popCachedWindow(const QString& binPath, int startFrame, int endFrame, EventSignalData* out);
    void rememberWindow(const QString& binPath, int startFrame, int endFrame, const EventSignalData& data);

    QFutureWatcher<bool> watcher_;
    QString binPath_;
    QString failureReason_;
    std::atomic<bool> cancelled_{false};

    // Lazy detail-window scan state.
    QFutureWatcher<WindowScanResult> windowWatcher_;
    std::atomic<bool> windowCancelled_{false};
    static constexpr int kWindowCacheMax = 8;
    QHash<QString, EventSignalData> windowCache_;
    QList<QString> windowCacheOrder_;
};
