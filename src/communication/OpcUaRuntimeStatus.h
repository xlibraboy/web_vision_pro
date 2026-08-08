#pragma once

#include <QString>
#include <QVector>

// Lightweight live-status snapshot emitted by OpcUaClientService for the
// OPC UA configuration UI. Kept free of QtOpcUa dependencies so the GUI can
// include it without pulling in the OPC UA stack.

struct OpcUaTagRuntimeStatus {
    int tagIndex = -1;
    QString name;
    QString nodeId;
    bool simulated = false;
    bool enabled = false;
    bool held = false;   // manual push-hold button currently pressed
    bool active = false; // Live: server value True; Simulated: button held
    QString valueText;   // "True" / "False" / "—"
    qint64 lastFiredMs = 0;
};

struct OpcUaRuntimeStatus {
    bool serviceRunning = false;
    bool clientConnected = false;
    bool connecting = false;
    QString clientStateText;
    QString speedText;
    bool speedValid = false;
    bool speedStale = false;
    QVector<OpcUaTagRuntimeStatus> tags;
};
