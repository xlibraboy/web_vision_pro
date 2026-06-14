#pragma once

#include "../config/CameraConfig.h"
#include <QObject>
#include <QOpcUaClient>
#include <QOpcUaProvider>
#include <QOpcUaMonitoringParameters>

class QOpcUaNode;

class OpcUaClientService : public QObject {
    Q_OBJECT
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

    explicit OpcUaClientService(QObject* parent = nullptr);
    ~OpcUaClientService() override;

    void setSettings(const OpcUaSettings& settings);
    void start();
    void stop();
    bool isRunning() const;

signals:
    void triggerFired(const OpcUaClientService::TriggerEvent& event);
    void statusChanged(const QString& message);

private slots:
    void onConnected();
    void onDisconnected();
    void onErrorOccurred(QOpcUaClient::ClientError error);
    void onStateChanged(QOpcUaClient::ClientState state);
    void onNodeValueChanged(QOpcUa::Types type, const QVariant& value,
                            const QDateTime& serverTimestamp, QOpcUa::UaStatusCode statusCode);

private:
    void connectToServer();
    void subscribeNodes();
    void scheduleReconnect();
    QOpcUaNode* createSubscribedNode(const QString& nodeId);

    OpcUaSettings settings_;
    QOpcUaProvider* provider_ = nullptr;
    QOpcUaClient* client_ = nullptr;
    QTimer* reconnectTimer_ = nullptr;
    bool running_ = false;

    // Speed state
    struct SpeedSample {
        bool valid = false;
        double value = 0.0;
        QString sampleTimeUtc;
        qint64 receivedAtMs = 0;
    } latestSpeed_;

    // Trigger state per slot
    struct TriggerState {
        bool hasPrev = false;
        bool prevActive = false;
        qint64 lastFiredMs = 0;
        OpcUaTriggerTagSettings settings;
        QOpcUaNode* node = nullptr;
    };
    QVector<TriggerState> triggerStates_;
    QOpcUaNode* speedNode_ = nullptr;
};
