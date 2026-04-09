#include "EventDatabase.h"
#include "../config/CameraConfig.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <iostream>
#include <algorithm>

EventDatabase& EventDatabase::instance() {
    static EventDatabase instance;
    return instance;
}

void EventDatabase::initialize(const QString& dataPath) {
    dataPath_ = dataPath;
    std::cout << "[EventDatabase] Initializing with path: " << dataPath.toStdString() << std::endl;
    scanDirectory();
    std::cout << "[EventDatabase] Found " << events_.size() << " historical events." << std::endl;
}

void EventDatabase::scanDirectory() {
    events_.clear();
    
    QDir dir(dataPath_);
    if (!dir.exists()) {
        std::cout << "[EventDatabase] Data directory does not exist: " << dataPath_.toStdString() << std::endl;
        return;
    }
    
    // 1. Load existing JSON metadata
    QStringList filters;
    filters << "event_*.json";
    QFileInfoList metadataFiles = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    
    for (const QFileInfo& fileInfo : metadataFiles) {
        try {
            EventInfo event = loadMetadata(fileInfo.absoluteFilePath());
            
            // Verify video file exists
            if (QFile::exists(event.videoPath)) {
                events_[event.timestamp] = event;
            } else {
                std::cout << "[EventDatabase] Warning: Video file missing for event " << event.timestamp.toStdString() 
                          << ". Skipping." << std::endl;
                // Optional: Consider deleting stale JSON
                // QFile::remove(fileInfo.absoluteFilePath());
            }
        } catch (const std::exception& e) {
            std::cerr << "[EventDatabase] Failed to load metadata: " << fileInfo.fileName().toStdString() 
                      << " - " << e.what() << std::endl;
        }
    }

    // 2. Discover orphan .bin files (Legacy/Recovered) - catch whatever primary camera triggered first
    QStringList binFilters;
    binFilters << "event_*.bin"; 
    QFileInfoList binFiles = dir.entryInfoList(binFilters, QDir::Files, QDir::Name);

    for (const QFileInfo& fileInfo : binFiles) {
        QString baseName = fileInfo.baseName(); 
        if (!baseName.startsWith("event_")) continue;
        
        // Handle old format: event_YYYYMMDD_HHMMSS_ZZZ
        // Handle new format: event_YYYYMMDD_HHMMSS_ZZZ_camX
        QString timestamp;
        if (baseName.contains("_cam")) {
            int camIdx = baseName.lastIndexOf("_cam");
            timestamp = baseName.mid(6, camIdx - 6); // remove "event_" and "_cam..."
        } else {
            timestamp = baseName.mid(6); // old format
        }
        
        // If we already have metadata for this timestamp, skip (prevents duplicates for multi-camera events)
        if (events_.contains(timestamp)) continue;

        // Otherwise, create default metadata from BIN header
        QFile f(fileInfo.absoluteFilePath());
        if (f.open(QIODevice::ReadOnly)) {
            char header[28]; // Magic(8) + Ver(4) + W(4) + H(4) + Type(4) + Count(4)
            if (f.read(header, 28) == 28) {
                int32_t width = *reinterpret_cast<int32_t*>(header + 12);
                int32_t height = *reinterpret_cast<int32_t*>(header + 16);
                int32_t count = *reinterpret_cast<int32_t*>(header + 24);

                EventInfo event;
                event.timestamp = timestamp;
                event.videoPath = fileInfo.absoluteFilePath();
                event.metadataPath = dir.filePath("event_" + timestamp + ".json");
                event.triggerIndex = 0; // Unknown, default to start
                event.totalFrames = count;
                event.fps = 10.0; // Default
                event.width = width;
                event.height = height;
                event.permanent = false;

                events_[timestamp] = event;
                
                // Auto-generate the missing JSON for next time
                saveMetadata(event.metadataPath, event); 
                std::cout << "[EventDatabase] Recovered orphan event: " << timestamp.toStdString() << std::endl;
            }
            f.close();
        }
    }
}

std::vector<EventDatabase::EventInfo> EventDatabase::getAllEvents() const {
    std::vector<EventInfo> result;
    result.reserve(events_.size());
    
    for (const auto& event : events_) {
        result.push_back(event);
    }
    
    // Sort by timestamp (newest first)
    std::sort(result.begin(), result.end(), [](const EventInfo& a, const EventInfo& b) {
        return a.timestamp > b.timestamp;
    });
    
    return result;
}

EventDatabase::EventInfo EventDatabase::getEventInfo(const QString& timestamp) const {
    auto it = events_.find(timestamp);
    if (it != events_.end()) {
        return it.value();
    }
    throw std::runtime_error("Event not found: " + timestamp.toStdString());
}

void EventDatabase::registerEvent(const EventInfo& event) {
    events_[event.timestamp] = event;
    
    // Auto-save metadata to disk
    QString jsonPath = QDir(dataPath_).filePath("event_" + event.timestamp + ".json");
    EventInfo eventToSave = event;
    eventToSave.metadataPath = jsonPath;
    
    saveMetadata(jsonPath, eventToSave);
    trimNonPermanentEvents();
    
    std::cout << "[EventDatabase] Registered new event: " << event.timestamp.toStdString() << std::endl;
}

void EventDatabase::saveMetadata(const QString& filepath, const EventInfo& event) {
    QJsonObject meta;
    meta["timestamp"] = event.timestamp;
    meta["videoPath"] = event.videoPath;
    meta["triggerIndex"] = event.triggerIndex;
    meta["totalFrames"] = event.totalFrames;
    meta["fps"] = event.fps;
    meta["width"] = event.width;
    meta["height"] = event.height;
    meta["permanent"] = event.permanent;
    
    QJsonDocument doc(meta);
    QFile file(filepath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
        std::cout << "[EventDatabase] Saved metadata: " << filepath.toStdString() << std::endl;
    } else {
        std::cerr << "[EventDatabase] Failed to save metadata: " << filepath.toStdString() << std::endl;
    }
}

EventDatabase::EventInfo EventDatabase::loadMetadata(const QString& filepath) {
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Cannot open metadata file");
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject meta = doc.object();
    
    EventInfo event;
    event.timestamp = meta["timestamp"].toString();
    event.videoPath = meta["videoPath"].toString();
    event.metadataPath = filepath;
    event.triggerIndex = meta["triggerIndex"].toInt();
    event.totalFrames = meta["totalFrames"].toInt();
    event.fps = meta["fps"].toDouble();
    event.width = meta["width"].toInt();
    event.height = meta["height"].toInt();
    event.permanent = meta["permanent"].toBool(false);
    
    return event;
}

bool EventDatabase::setPermanent(const QString& timestamp, bool permanent) {
    auto it = events_.find(timestamp);
    if (it == events_.end()) {
        return false;
    }

    it->permanent = permanent;
    saveMetadata(it->metadataPath, it.value());
    if (!permanent) {
        trimNonPermanentEvents();
    }
    return true;
}

void EventDatabase::trimNonPermanentEvents() {
    const int maxRecords = std::max(0, CameraConfig::getEventRetentionCount());
    if (maxRecords <= 0) {
        return;
    }

    std::vector<EventInfo> nonPermanent;
    nonPermanent.reserve(events_.size());
    for (auto it = events_.cbegin(); it != events_.cend(); ++it) {
        if (!it.value().permanent) {
            nonPermanent.push_back(it.value());
        }
    }

    if (static_cast<int>(nonPermanent.size()) <= maxRecords) {
        return;
    }

    std::sort(nonPermanent.begin(), nonPermanent.end(), [](const EventInfo& a, const EventInfo& b) {
        return a.timestamp > b.timestamp;
    });

    for (int i = maxRecords; i < static_cast<int>(nonPermanent.size()); ++i) {
        deleteEvent(nonPermanent[i].timestamp);
    }
}

bool EventDatabase::deleteEvent(const QString& timestamp) {
    if (!events_.contains(timestamp)) {
        std::cerr << "[EventDatabase] Cannot delete event: " << timestamp.toStdString() << " (Not found)" << std::endl;
        return false;
    }

    EventInfo info = events_[timestamp];
    
    // Delete video files for all cameras (looping up to an arbitrary reasonable max, like 16)
    for (int i = 1; i <= 16; ++i) {
        QString camPath = QString("../data/event_%1_cam%2.bin").arg(timestamp).arg(i);
        if (QFile::exists(camPath)) {
            QFile::remove(camPath);
        }
    }
    
    // Also try removing old legacy formats
    if (QFile::exists(info.videoPath)) {
        QFile::remove(info.videoPath);
    }
    
    QString otherPath = info.videoPath;
    if (otherPath.endsWith(".bin")) {
        otherPath.replace(".bin", ".mp4");
    } else {
        otherPath.replace(".mp4", ".bin");
    }
    if (QFile::exists(otherPath)) {
        QFile::remove(otherPath);
    }
    
    // Delete metadata file
    if (QFile::exists(info.metadataPath)) {
        QFile::remove(info.metadataPath);
    }
    
    // Remove from registry
    events_.remove(timestamp);
    std::cout << "[EventDatabase] Deleted event: " << timestamp.toStdString() << std::endl;
    
    return true;
}
