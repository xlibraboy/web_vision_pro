#pragma once

#include "../config/CameraConfig.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

class OpcUaClientService {
public:
    struct TriggerEvent {
        QString tagName;
        QString nodeId;
        QString source = QStringLiteral("opcua");
        QString speedTagName;
        QString speedTagNodeId;
        QString speedUnit = QStringLiteral("m/min");
        QString speedSampleTimeUtc;
        double speedValue = 0.0;
        bool hasSpeed = false;
        bool speedStale = false;
        int positionDirectionSign = 1;
    };

    using TriggerCallback = std::function<void(const TriggerEvent&)>;
    using StatusCallback = std::function<void(const QString&)>;

    OpcUaClientService();
    ~OpcUaClientService();

    void setSettings(const OpcUaSettings& settings);
    void setTriggerCallback(TriggerCallback callback);
    void setStatusCallback(StatusCallback callback);

    void start();
    void stop();
    bool isRunning() const;

    void dispatchTriggerEvent(const TriggerEvent& event);
    void emitStatus(const QString& message);

private:
    void run();


    mutable std::mutex mutex_;
    OpcUaSettings settings_;
    TriggerCallback triggerCallback_;
    StatusCallback statusCallback_;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
};
