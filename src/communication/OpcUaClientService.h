#pragma once

#include "../config/CameraConfig.h"
#include <QObject>
#include <QPointer>
#include <QtOpcUa/QOpcUaClient>
#include <QtOpcUa/QOpcUaNode>

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
    void triggerReceived(const OpcUaClientService::TriggerEvent& event);
    void statusChanged(const QString& message);

private slots:
    void connectClient();
    void reconnectLater();
    void handleClientStateChanged(QOpcUaClient::ClientState state);
    void handleClientErrorChanged(QOpcUaClient::ClientError error);
    void handleTriggerValueChanged(const QVariant& value);
    void handleSpeedValueChanged(const QVariant& value);
    void handleAttributeUpdated(QOpcUa::NodeAttribute attribute, const QVariant& value);

private:
    struct TriggerMonitorState {
        OpcUaTriggerTagSettings settings;
        QPointer<QOpcUaNode> node;
        bool hasPreviousValue = false;
        bool previousActive = false;
        qint64 lastTriggeredMs = 0;
    };

    struct LatestSpeedSample {
        bool valid = false;
        QString tagName;
        QString nodeId;
        QString unit;
        QString sampleTimeUtc;
        double value = 0.0;
        qint64 receivedAtMs = 0;
    };

    void resetRuntimeState();
    void clearMonitoredNodes();
    void monitorConfiguredNodes();
    void monitorTriggerTag(const OpcUaTriggerTagSettings& triggerSettings);
    void monitorSpeedTag(const OpcUaSpeedTagSettings& speedSettings);
    void emitStatus(const QString& message);
    void dispatchTriggerEvent(const TriggerEvent& event);
    void processTriggerValue(TriggerMonitorState& state, const QVariant& value);
    void processSpeedValue(const QVariant& value);
    bool extractBooleanValue(const QVariant& value, bool* result) const;
    bool extractNumericValue(const QVariant& value, double* result) const;
    QString clientErrorText(QOpcUaClient::ClientError error) const;
    QString backendName() const;

    OpcUaSettings settings_;
    QOpcUaClient* client_ = nullptr;
    QList<TriggerMonitorState> triggerStates_;
    QPointer<QOpcUaNode> speedNode_;
    LatestSpeedSample latestSpeed_;
    bool shouldReconnect_ = false;
    bool connectAttemptInFlight_ = false;
};
