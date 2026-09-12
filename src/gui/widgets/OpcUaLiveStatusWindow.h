#pragma once

#include <QWidget>
#include "../../communication/OpcUaRuntimeStatus.h"

class QLabel;
class QTableWidget;

// Detached live status view: client state, machine speed, and one row per
// trigger tag that has actually published a value. Tags that are merely enabled
// or connected (no value received yet) stay hidden, so the window lists only
// the tags the server is really updating.
class OpcUaLiveStatusWindow : public QWidget {
    Q_OBJECT

public:
    explicit OpcUaLiveStatusWindow(QWidget* parent = nullptr);

public slots:
    void updateStatus(const OpcUaRuntimeStatus& status);

private:
    QLabel* clientLabel_ = nullptr;
    QLabel* speedLabel_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
};
