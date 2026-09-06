#pragma once

#include <QObject>

// Regression test: entering System Configuration (ConfigDialog show) and the
// OPC UA "Detect Server" button must never block the GUI thread on a network
// probe. OPC UA server discovery is driven by a 750ms per-candidate timeout;
// if it ever ran synchronously again, opening the dialog while the configured
// endpoint is silent (no answer) would freeze the UI for that full timeout.
class ConfigDialogNetworkRegression : public QObject {
    Q_OBJECT

private slots:
    void entryAndDetectPathsDoNotBlockOnOpcUaProbe();
};
