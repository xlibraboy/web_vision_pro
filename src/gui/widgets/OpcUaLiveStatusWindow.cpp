#include "OpcUaLiveStatusWindow.h"

#include "../../config/CameraConfig.h"
#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
constexpr int kStatusColumns = 5;
}

OpcUaLiveStatusWindow::OpcUaLiveStatusWindow(QWidget* parent)
    : QWidget(parent) {
    setWindowFlags(Qt::Window);
    setWindowTitle(QStringLiteral("OPC UA Live Status"));
    resize(560, 420);

    const ThemeColors tc = CameraConfig::getThemeColors();
    setStyleSheet(QStringLiteral("OpcUaLiveStatusWindow { background-color: %1; }").arg(tc.bg));

    QFormLayout* statusForm = new QFormLayout();
    statusForm->setSpacing(6);
    statusForm->setHorizontalSpacing(16);
    statusForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    QLabel* clientCaption = new QLabel("Client:", this);
    clientCaption->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(tc.text));
    clientLabel_ = new QLabel(QStringLiteral("○ Idle (OPC UA disabled)"), this);
    clientLabel_->setWordWrap(true);
    clientLabel_->setStyleSheet(QStringLiteral("color: #8B949E; font-size: 12px; font-weight: 600;"));
    statusForm->addRow(clientCaption, clientLabel_);

    QLabel* speedCaption = new QLabel("Speed:", this);
    speedCaption->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(tc.text));
    speedLabel_ = new QLabel(QStringLiteral("—"), this);
    speedLabel_->setStyleSheet(QStringLiteral("color: #8B949E; font-size: 12px;"));
    statusForm->addRow(speedCaption, speedLabel_);

    table_ = new QTableWidget(0, kStatusColumns, this);
    table_->setHorizontalHeaderLabels(QStringList()
        << "Name" << "Type" << "Value" << "State" << "Last Fired");
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(false);
    table_->setStyleSheet(QString(
        "QTableWidget { background: transparent; border: none; color: %1; font-size: 12px; } "
        "QTableWidget::item { padding: 4px 8px; border: none; } "
        "QHeaderView::section { background: transparent; color: %2; border: none; border-bottom: 1px solid %3; padding: 4px 8px; font-weight: 600; font-size: 11px; }"
    ).arg(tc.text, tc.primary, tc.border));
    QHeaderView* header = table_->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    emptyLabel_ = new QLabel(
        "No tag values yet — waiting for the OPC UA tags to publish.", this);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setStyleSheet(QStringLiteral("color: #8B949E; font-size: 12px;"));

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(14, 14, 14, 14);
    rootLayout->setSpacing(10);
    rootLayout->addLayout(statusForm);
    rootLayout->addWidget(table_, 1);
    rootLayout->addWidget(emptyLabel_, 1);
}

void OpcUaLiveStatusWindow::updateStatus(const OpcUaRuntimeStatus& status) {
    QString dot;
    QString color;
    if (status.clientConnected) {
        dot = QStringLiteral("●");
        color = QStringLiteral("#4CAF50");
    } else if (status.connecting) {
        dot = QStringLiteral("◐");
        color = QStringLiteral("#E0A800");
    } else {
        dot = QStringLiteral("○");
        color = QStringLiteral("#8B949E");
    }
    const QString clientText = status.clientStateText.isEmpty()
        ? QStringLiteral("Idle (OPC UA disabled)") : status.clientStateText;
    clientLabel_->setText(QStringLiteral("%1 %2").arg(dot, clientText));
    clientLabel_->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px; font-weight: 600;").arg(color));

    if (status.speedValid) {
        const QString staleSuffix = status.speedStale ? QStringLiteral("  (STALE)") : QString();
        speedLabel_->setText(status.speedText + staleSuffix);
        speedLabel_->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; font-weight: 600;")
            .arg(status.speedStale ? QStringLiteral("#E0A800") : QStringLiteral("#4CAF50")));
    } else {
        speedLabel_->setText(QStringLiteral("—"));
        speedLabel_->setStyleSheet(QStringLiteral("color: #8B949E; font-size: 12px;"));
    }

    if (table_->rowCount() != status.tags.size()) {
        table_->setRowCount(status.tags.size());
    }

    auto makeItem = [](const QString& text, const QString& color) {
        QTableWidgetItem* item = new QTableWidgetItem(text);
        item->setForeground(QBrush(QColor(color)));
        item->setFlags(Qt::ItemIsEnabled);
        return item;
    };

    int visibleRows = 0;
    for (int i = 0; i < status.tags.size(); ++i) {
        const OpcUaTagRuntimeStatus& tag = status.tags[i];
        // Only tags that have actually published a value are listed: enabled or
        // connected tags with nothing to show would just be empty rows.
        const bool hasValue = tag.valueText != QStringLiteral("—");
        table_->setRowHidden(i, !hasValue);
        if (!hasValue) {
            continue;
        }
        ++visibleRows;

        QString name = tag.name;
        if (name.isEmpty()) {
            name = QStringLiteral("Trigger %1").arg(i + 1);
        }

        QString stateText = QStringLiteral("Idle");
        QString stateColor = QStringLiteral("#4CAF50");
        if (tag.held || tag.active) {
            stateText = QStringLiteral("FIRING");
            stateColor = QStringLiteral("#E0A800");
        } else if (!tag.enabled) {
            stateText = QStringLiteral("Off");
            stateColor = QStringLiteral("#8B949E");
        }

        QString lastFiredText = QStringLiteral("—");
        if (tag.lastFiredMs > 0) {
            lastFiredText = QDateTime::fromMSecsSinceEpoch(tag.lastFiredMs)
                .toString(QStringLiteral("HH:mm:ss.zzz"));
        }

        table_->setItem(i, 0, makeItem(name, QStringLiteral("#E3E3E3")));
        table_->setItem(i, 1, makeItem(tag.simulated ? QStringLiteral("Sim") : QStringLiteral("Live"),
            tag.simulated ? QStringLiteral("#00E5FF") : QStringLiteral("#A9B4C2")));
        table_->setItem(i, 2, makeItem(tag.valueText, QStringLiteral("#E3E3E3")));
        table_->setItem(i, 3, makeItem(stateText, stateColor));
        table_->setItem(i, 4, makeItem(lastFiredText, QStringLiteral("#8B949E")));
    }

    table_->setVisible(visibleRows > 0);
    emptyLabel_->setVisible(visibleRows == 0);
}
