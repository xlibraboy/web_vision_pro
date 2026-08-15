#pragma once

#include "../config/CameraConfig.h"
#include "OpcUaRuntimeStatus.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QMutex>
#include <QVector>
#include <QtOpcUa/QOpcUaClient>
#include <QtOpcUa/QOpcUaNode>

class QTimer;

class OpcUaClientService : public QObject {
    Q_OBJECT

public:
    // One machine-speed anchor captured at trigger time: the actual local
    // speed (m/min) at a machine position (mm).
    struct SpeedSample {
        int positionMm = 0;
        QString tagName;
        QString nodeId;
        double value = 0.0;
    };

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
        // Camera group to record (CameraGroup::k*), or -1 for all cameras.
        int group = CameraGroup::kUnassigned;
        // Machine position (mm) of the trigger sensor (0 = no spatial alignment).
        int positionMm = 0;
        // Every fresh speed anchor snapshot (position mm + actual local speed),
        // so the recorded event can reproduce the machine's speed profile.
        QVector<SpeedSample> speedAnchors;
    };

    explicit OpcUaClientService(QObject* parent = nullptr);
    ~OpcUaClientService() override;

    void setSettings(const OpcUaSettings& settings);
    void start();
    void stop();
    bool isRunning() const;

    // Manual push-hold trigger: while held==true the tag fires a trigger every
    // Cooldown ms (works for Simulated tags without a server, and as a manual
    // override for Live tags). held==false stops it. The tagSettings snapshot
    // comes from the config UI row so the button works even before settings are
    // saved.
    void setManualTriggerHeld(int tagIndex, bool held,
                              const OpcUaTriggerTagSettings& tagSettings);
    // Release every held manual trigger (e.g. the config dialog was destroyed).
    void releaseAllManualTriggers();
    // Latest machine speed sample in m/min (primary anchor; for spatial
    // trigger alignment). Returns false when no valid speed sample is available.
    bool currentSpeedMperMin(double* mPerMin) const;
    // Latest valid & fresh speed anchors (position mm + value). Returns false
    // when no anchor is usable. Used to snapshot the machine's speed profile
    // onto events at trigger time.
    bool currentSpeedAnchors(QVector<SpeedSample>* out) const;
    // Configured unit of the primary Machine Speed tag (defaults to "m/min").
    QString speedUnit() const;
    // (Re)publish the simulated Machine Speed sample even when the OPC UA
    // service is not running, so standalone manual/Live triggers still capture
    // a speed. No-op unless the speed tag is enabled and simulated.
    void refreshSimulatedSpeed();

signals:
    void triggerReceived(const OpcUaClientService::TriggerEvent& event);
    void statusChanged(const QString& message);
    // Live snapshot for the OPC UA configuration UI (client state, speed, and
    // per-tag status), emitted on every push-hold timer tick.
    void runtimeStatusChanged(const OpcUaRuntimeStatus& status);

private slots:
    void connectClient();
    void reconnectLater();
    void handleClientStateChanged(QOpcUaClient::ClientState state);
    void handleClientErrorChanged(QOpcUaClient::ClientError error);
    void handleTriggerValueChanged(const QVariant& value);
    void handleAttributeUpdated(QOpcUa::NodeAttribute attribute, const QVariant& value);
    void onPushHoldTimerTick();

private:
    struct TriggerMonitorState {
        OpcUaTriggerTagSettings settings;
        QPointer<QOpcUaNode> node;
        // Config array index of the tag (for status reporting).
        int tagIndex = -1;
        // Cached latest value so the push-hold timer can re-evaluate a held tag
        // even when the server only pushed it once.
        bool hasLastValue = false;
        QVariant lastValue;
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

    // One monitored speed node per configured anchor.
    struct SpeedMonitorState {
        OpcUaSpeedTagSettings settings;
        QPointer<QOpcUaNode> node;
        // Index into settings_.speedTags (for status/processing routing).
        int anchorIndex = -1;
    };

    // Latest per-anchor sample, indexed by anchorIndex (parallel to settings_.
    // speedTags). Written on the GUI thread (OPC UA callbacks) and read from
    // the camera acquisition thread (spatial trigger alignment), so guarded.
    struct SpeedAnchorSample {
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
    void monitorTriggerTag(const OpcUaTriggerTagSettings& triggerSettings, int tagIndex);
    void monitorSpeedAnchors();
    void monitorSpeedTag(const OpcUaSpeedTagSettings& speedSettings, int anchorIndex);
    // Index of the primary speed anchor (first enabled; legacy single-tag API
    // keeps mirroring it). -1 when no anchors are configured.
    int primarySpeedIndex() const;
    OpcUaRuntimeStatus currentRuntimeStatus() const;
    bool hasRealMonitoredTags() const;
    bool hasSimulatedTags() const;
    void synthesizeSimulatedSpeed();
    void emitStatus(const QString& message);
    void dispatchTriggerEvent(const TriggerEvent& event);
    void dispatchTriggerFor(const OpcUaTriggerTagSettings& tagSettings);
    void dispatchManualTrigger(const OpcUaTriggerTagSettings& tagSettings, int tagIndex);
    // emitWarnings=false is used by the push-hold timer: it re-evaluates the
    // cached value every tick, and a per-tick warning for an unchanged invalid
    // value would flood the status bar (only new values warrant warnings).
    void processTriggerValue(TriggerMonitorState& state, const QVariant& value, bool emitWarnings = true);
    void processSpeedValue(int anchorIndex, const QVariant& value);
    bool extractBooleanValue(const QVariant& value, bool* result) const;
    bool extractNumericValue(const QVariant& value, double* result) const;
    QString clientErrorText(QOpcUaClient::ClientError error) const;
    QString backendName() const;

    OpcUaSettings settings_;
    QOpcUaClient* client_ = nullptr;
    QList<TriggerMonitorState> triggerStates_;
    QList<SpeedMonitorState> speedStates_;
    // speedSamples_/latestSpeed_ are written on the GUI thread (OPC UA
    // callbacks) and read from the camera acquisition thread (spatial trigger
    // alignment), so they are guarded. speedSamples_ is index-parallel to
    // settings_.speedTags; latestSpeed_ mirrors the primary anchor for the
    // legacy single-speed API.
    mutable QMutex speedMutex_;
    QVector<SpeedAnchorSample> speedSamples_;
    LatestSpeedSample latestSpeed_;
    QTimer* pushHoldTimer_ = nullptr;
    // Tag index -> config snapshot of the button currently held (presence = held).
    QHash<int, OpcUaTriggerTagSettings> manualTriggerHeld_;
    QHash<int, qint64> manualLastFiredMs_;
    bool shouldReconnect_ = false;
    bool connectAttemptInFlight_ = false;
};
