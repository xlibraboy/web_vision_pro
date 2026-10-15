#include "OpcUaLiveStatusWindow.h"

#include "../../config/CameraConfig.h"
#include "../../config/UiTypeScale.h"
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

    QFormLayout* statusForm = new QFormLayout();
    statusForm->setSpacing(6);
    statusForm->setHorizontalSpacing(16);
    statusForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    clientCaption_ = new QLabel("Client:", this);
    clientLabel_ = new QLabel(QStringLiteral("○ Idle (OPC UA disabled)"), this);
    clientLabel_->setWordWrap(true);
    statusForm->addRow(clientCaption_, clientLabel_);

    speedCaption_ = new QLabel("Speed:", this);
    speedLabel_ = new QLabel(QStringLiteral("—"), this);
    statusForm->addRow(speedCaption_, speedLabel_);

    table_ = new QTableWidget(0, kStatusColumns, this);
    table_->setHorizontalHeaderLabels(QStringList()
        << "Name" << "Type" << "Value" << "State" << "Last Fired");
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(false);
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

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(14, 14, 14, 14);
    rootLayout->setSpacing(10);
    rootLayout->addLayout(statusForm);
    rootLayout->addWidget(table_, 1);
    rootLayout->addWidget(emptyLabel_, 1);

    updateTheme();
}

void OpcUaLiveStatusWindow::updateTheme() {
    const ThemeColors tc = CameraConfig::getThemeColors();
    const QString bodySize = UiTypeScale::px(UiTypeScale::kBody);

    setStyleSheet(QStringLiteral("OpcUaLiveStatusWindow { background-color: %1; }").arg(tc.bg));

    clientCaption_->setStyleSheet(QString("color: %1; font-size: " + bodySize + ";").arg(tc.text));
    speedCaption_->setStyleSheet(QString("color: %1; font-size: " + bodySize + ";").arg(tc.text));

    // Base state, replaced by the live colors on the next updateStatus().
    clientLabel_->setStyleSheet(QString("color: %1; font-size: " + bodySize + "; font-weight: 600;").arg(tc.muted));
    speedLabel_->setStyleSheet(QString("color: %1; font-size: " + bodySize + ";").arg(tc.muted));

    table_->setStyleSheet(QString(
        "QTableWidget { background: transparent; border: none; color: %1; font-size: " + bodySize + "; } "
        "QTableWidget::item { padding: 4px 8px; border: none; } "
        "QHeaderView::section { background: transparent; color: %2; border: none; border-bottom: 1px solid %3; padding: 4px 8px; font-weight: 600; font-size: " + UiTypeScale::px(UiTypeScale::kMeta) + "; }"
    ).arg(tc.text, tc.primary, tc.border));

    emptyLabel_->setStyleSheet(QString("color: %1; font-size: " + bodySize + ";").arg(tc.muted));
}

void OpcUaLiveStatusWindow::updateStatus(const OpcUaRuntimeStatus& status) {
    const ThemeColors tc = CameraConfig::getThemeColors();
    QString dot;
    QString color;
    if (status.clientConnected) {
        dot = QStringLiteral("●");
        color = tc.ok;
    } else if (status.connecting) {
        dot = QStringLiteral("◐");
        color = tc.warn;
    } else {
        dot = QStringLiteral("○");
        color = tc.muted;
    }
    const QString clientText = status.clientStateText.isEmpty()
        ? QStringLiteral("Idle (OPC UA disabled)") : status.clientStateText;
    clientLabel_->setText(QStringLiteral("%1 %2").arg(dot, clientText));
    clientLabel_->setStyleSheet(
        QString("color: %1; font-size: " + UiTypeScale::px(UiTypeScale::kBody) + "; font-weight: 600;").arg(color));

    if (status.speedValid) {
        const QString staleSuffix = status.speedStale ? QStringLiteral("  (STALE)") : QString();
        speedLabel_->setText(status.speedText + staleSuffix);
        speedLabel_->setStyleSheet(QString("color: %1; font-size: " + UiTypeScale::px(UiTypeScale::kBody) + "; font-weight: 600;")
            .arg(status.speedStale ? tc.warn : tc.ok));
    } else {
        speedLabel_->setText(QStringLiteral("—"));
        speedLabel_->setStyleSheet(QString("color: %1; font-size: " + UiTypeScale::px(UiTypeScale::kBody) + ";").arg(tc.muted));
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
        QString stateColor = tc.ok;
        if (tag.held || tag.active) {
            stateText = QStringLiteral("FIRING");
            stateColor = tc.warn;
        } else if (!tag.enabled) {
            stateText = QStringLiteral("Off");
            stateColor = tc.disabled;
        }

        QString lastFiredText = QStringLiteral("—");
        if (tag.lastFiredMs > 0) {
            lastFiredText = QDateTime::fromMSecsSinceEpoch(tag.lastFiredMs)
                .toString(QStringLiteral("HH:mm:ss.zzz"));
        }

        table_->setItem(i, 0, makeItem(name, tc.text));
        table_->setItem(i, 1, makeItem(tag.simulated ? QStringLiteral("Sim") : QStringLiteral("Live"),
            tag.simulated ? tc.info : tc.muted));
        table_->setItem(i, 2, makeItem(tag.valueText, tc.text));
        table_->setItem(i, 3, makeItem(stateText, stateColor));
        table_->setItem(i, 4, makeItem(lastFiredText, tc.muted));
    }

    table_->setVisible(visibleRows > 0);
    emptyLabel_->setVisible(visibleRows == 0);
}
