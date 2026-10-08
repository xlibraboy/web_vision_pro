#include "FixedIpListPanel.h"
#include "../../config/CameraConfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QFont>
#include <QColor>

namespace {
constexpr int kMaxCameras = 16;  // matches the GUI/camera pipeline hard limit

QString mutedLabelStyle() {
    return QString("color: %1; font-size: 12px;").arg(CameraConfig::getThemeColors().muted);
}

// Semantic colors matching the camera cards' status colors. Resolved per call
// so the registry follows a theme switch.
QColor onlineColor() { return QColor(CameraConfig::getThemeColors().ok); }
QColor emulatedColor() { return QColor(CameraConfig::getThemeColors().info); }
QColor offlineColor() { return QColor(CameraConfig::getThemeColors().muted); }
}

FixedIpListPanel::FixedIpListPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // Header row: title hint + total count (the camera-ID counting lives here).
    auto* header = new QHBoxLayout();
    auto* hintLabel = new QLabel("Fixed IP registry - set once at initial install, read-only here.", this);
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet(mutedLabelStyle());
    header->addWidget(hintLabel, 1);

    countLabel_ = new QLabel(this);
    QFont countFont = countLabel_->font();
    countFont.setBold(true);
    countLabel_->setFont(countFont);
    countLabel_->setStyleSheet(mutedLabelStyle());
    header->addWidget(countLabel_);
    layout->addLayout(header);

    // Table: ID | Name | Fixed IP | Detected IP | MAC (always read-only)
    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(QStringList()
        << "ID" << "Name" << "Fixed IP" << "Detected IP" << "MAC");
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    layout->addWidget(table_, 1);
}

void FixedIpListPanel::setCameras(const std::vector<CameraInfo>& cameras,
                                  const std::vector<QString>& detectedIps) {
    table_->setRowCount(0);
    table_->setRowCount(static_cast<int>(cameras.size()));

    for (int row = 0; row < static_cast<int>(cameras.size()); ++row) {
        const CameraInfo& cam = cameras[static_cast<size_t>(row)];

        table_->setItem(row, 0, new QTableWidgetItem(QString::number(cam.id)));
        table_->setItem(row, 1, new QTableWidgetItem(cam.name));
        table_->setItem(row, 2, new QTableWidgetItem(cam.ipAddress));

        // Detected IP — live camera state, colored by reachability.
        const QString detected = row < static_cast<int>(detectedIps.size())
            ? detectedIps[static_cast<size_t>(row)]
            : QString();
        const QString display = detected.isEmpty() ? QStringLiteral("Offline") : detected;
        auto* detectedItem = new QTableWidgetItem(display);
        QColor fg = offlineColor();
        if (display == QLatin1String("Emulated")) {
            fg = emulatedColor();
        } else if (!display.startsWith(QLatin1String("Offline"))) {
            // A real IP means the camera is reachable (green); "Offline",
            // "Offline - no hardware", etc. stay muted gray.
            fg = onlineColor();
        }
        detectedItem->setForeground(fg);
        QFont mono = detectedItem->font();
        mono.setFamily(QStringLiteral("SF Mono"));
        mono.setStyleHint(QFont::Monospace);
        detectedItem->setFont(mono);
        table_->setItem(row, 3, detectedItem);

        // MAC (coupling to a real camera is done on the card)
        table_->setItem(row, 4, new QTableWidgetItem(cam.macAddress.isEmpty() ? "-" : cam.macAddress));
    }

    countLabel_->setText(QString("%1 / %2 cameras").arg(cameras.size()).arg(kMaxCameras));
}
