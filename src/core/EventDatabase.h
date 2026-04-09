#pragma once

#include <QString>
#include <QMap>
#include <QJsonObject>
#include <vector>

/**
 * EventDatabase - Manages event metadata and file indexing
 * Scans data directory on startup and maintains event registry
 */
class EventDatabase {
public:
    struct EventInfo {
        QString timestamp;      // "20260208_153000"
        QString videoPath;      // "../data/event_20260208_153000.mp4"
        QString metadataPath;   // "../data/event_20260208_153000.json"
        int triggerIndex;       // Frame index where trigger occurred
        int totalFrames;        // Total frames in recording
        double fps;             // Frames per second
        int width;              // Frame width
        int height;             // Frame height
        bool permanent = false; // Excluded from automatic retention cleanup
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
