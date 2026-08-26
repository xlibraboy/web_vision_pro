#pragma once

#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>
#include <vector>
#include <map>
#include <cstdint>
#include <limits>
#include "../gui/CameraInfo.h"


/**
 * EventDatabase - Manages event metadata and file indexing
 * Scans data directory on startup and maintains event registry
 */
class EventDatabase {
public:
    // One machine-speed sample captured at trigger time: the actual local speed
    // (m/min) at a specific machine position (mm). A vector of these lets the
    // defect projector interpolate the paper's local speed anywhere along the
    // machine, reflecting the draw between drive groups.
    struct SpeedAnchorSnapshot {
        int positionMm = 0;
        double speedValue = std::numeric_limits<double>::quiet_NaN();
        QString tagName;
        QString nodeId;
    };

    struct EventInfo {
        QString timestamp;      // "20260208_153000"
        QString videoPath;      // "../data/event_20260208_153000.mp4"
        QString metadataPath;   // "../data/event_20260208_153000.json"
        int triggerIndex;       // Frame index where trigger occurred
        int totalFrames;        // Total frames in recording
        double fps;             // Frames per second
        int width;              // Frame width
        int height;             // Frame height
        QStringList cameraLabels; // Per-camera labels captured at record time
        std::vector<int> cameraPositionsMm; // Per-camera machine positions captured at record time
        QString triggerReason = QStringLiteral("Triggered");
        QString triggerSource = QStringLiteral("unknown");
        QString triggerTagName;
        QString triggerTagNodeId;
        QString speedTagName;
        QString speedTagNodeId;
        double speedValue = std::numeric_limits<double>::quiet_NaN();
        QString speedUnit;
        QString speedSampleTimeUtc;
        bool speedStale = false;
        // All machine-speed anchors snapshotted when the trigger fired
        // (position mm + actual local speed). Empty for legacy events; the
        // single speedValue above then serves as the global fallback.
        std::vector<SpeedAnchorSnapshot> speedAnchors;
        int positionDirectionSign = 1;
        int triggerPositionMm = 0;  // Machine position (mm) where the trigger fired
                                    // (e.g. the sheetbreak sensor's position).
        // Camera group the trigger was wired to (CameraGroup::k*). A negative
        // value means the trigger recorded all cameras.
        int triggerGroup = CameraGroup::kUnassigned;
        bool permanent = false; // Excluded from automatic retention cleanup
        // Per-camera grayscale histogram at the trigger frame.
        // Key = 1-based camera ID, value = 256-bin uint32 pixel counts.
        // Empty for legacy events or when histogram was not computed.
        std::map<int, std::vector<uint32_t>> histograms;
    };
    
    // Singleton access
    static EventDatabase& instance();
    
    // Initialize and scan data directory
    void initialize(const QString& dataPath);
    
    // Get all events (sorted newest first)
    std::vector<EventInfo> getAllEvents() const;
    
    // Get specific event info by timestamp
    // Get specific event info by timestamp
    EventInfo getEventInfo(const QString& timestamp) const;

    // Delete event (files and registry)
    bool deleteEvent(const QString& timestamp);

    // Bulk-delete all non-permanent events, keeping only the `keep` most recent
    // (0 = delete every non-permanent event). Permanent records are untouched.
    // Returns the number of events deleted.
    int clearNonPermanentEvents(int keep);

    // Update event retention mode and persist it to metadata.
    bool setPermanent(const QString& timestamp, bool permanent);
    
    // Register new event (called after recording completes)
    void registerEvent(const EventInfo& event);
    
    // Save event metadata to JSON
    static void saveMetadata(const QString& filepath, const EventInfo& event);
    
    // Load event metadata from JSON
    static EventInfo loadMetadata(const QString& filepath);

private:
    EventDatabase() = default;
    ~EventDatabase() = default;
    
    EventDatabase(const EventDatabase&) = delete;
    EventDatabase& operator=(const EventDatabase&) = delete;

    void scanDirectory();
    void trimNonPermanentEvents();

    QMap<QString, EventInfo> events_;  // timestamp -> EventInfo
    QString dataPath_;
};
