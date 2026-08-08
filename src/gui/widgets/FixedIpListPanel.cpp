#include "FixedIpListPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QHostAddress>
#include <QAbstractSocket>

#include "IconManager.h"

namespace {
constexpr int kMaxCameras = 16;  // matches the GUI/camera pipeline hard limit

bool isValidIpv4(const QString& text) {
    QHostAddress addr;
    return addr.setAddress(text.trimmed()) && addr.protocol() == QAbstractSocket::IPv4Protocol;
}
}

FixedIpListPanel::FixedIpListPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // Toolbar: Add / Delete + count hint
    auto* toolbar = new QHBoxLayout();
    addBtn_ = new QPushButton("Add Camera", this);
    addBtn_->setIcon(IconManager::instance().add(16));
    addBtn_->setToolTip("Add a new fixed-IP camera (creates a matching camera card).");
    connect(addBtn_, &QPushButton::clicked, this, &FixedIpListPanel::onAddClicked);
    toolbar->addWidget(addBtn_);

    deleteBtn_ = new QPushButton("Delete Camera", this);
    deleteBtn_->setIcon(IconManager::instance().trash(16));
    deleteBtn_->setToolTip("Delete the selected camera and its camera card.");
    connect(deleteBtn_, &QPushButton::clicked, this, &FixedIpListPanel::onDeleteClicked);
    toolbar->addWidget(deleteBtn_);

    toolbar->addStretch();

    countLabel_ = new QLabel(this);
    countLabel_->setStyleSheet("color: #8B949E; font-size: 12px;");
    toolbar->addWidget(countLabel_);
    layout->addLayout(toolbar);

    // Table: ID | Name | Fixed IP | MAC
    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(QStringList() << "ID" << "Name" << "Fixed IP" << "MAC");
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    connect(table_, &QTableWidget::cellChanged, this, &FixedIpListPanel::onCellChanged);
    layout->addWidget(table_, 1);

    setAdminMode(false);
}

void FixedIpListPanel::setCameras(const std::vector<CameraInfo>& cameras) {
    populating_ = true;
    table_->setRowCount(0);
    table_->setRowCount(static_cast<int>(cameras.size()));

    for (int row = 0; row < static_cast<int>(cameras.size()); ++row) {
        const CameraInfo& cam = cameras[static_cast<size_t>(row)];

        // ID (read-only)
        QTableWidgetItem* idItem = new QTableWidgetItem(QString::number(cam.id));
        idItem->setData(Qt::UserRole, cam.id);
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 0, idItem);

        // Name (editable)
        QTableWidgetItem* nameItem = new QTableWidgetItem(cam.name);
        nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);
        table_->setItem(row, 1, nameItem);

        // Fixed IP (editable)
        QTableWidgetItem* ipItem = new QTableWidgetItem(cam.ipAddress);
        ipItem->setFlags(ipItem->flags() | Qt::ItemIsEditable);
        table_->setItem(row, 2, ipItem);
        lastValidIps_.insert(cam.id, cam.ipAddress);

        // MAC (read-only; coupling to a real camera is done on the card)
        QTableWidgetItem* macItem = new QTableWidgetItem(cam.macAddress.isEmpty() ? "-" : cam.macAddress);
        macItem->setFlags(macItem->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 3, macItem);
    }

    countLabel_->setText(QString("%1 / %2 cameras").arg(cameras.size()).arg(kMaxCameras));
    populating_ = false;
}

void FixedIpListPanel::setAdminMode(bool isAdmin) {
    adminMode_ = isAdmin;
    addBtn_->setEnabled(isAdmin);
    deleteBtn_->setEnabled(isAdmin);

    // Only the Name and Fixed IP columns become editable in admin mode.
    const QAbstractItemView::EditTriggers triggers = isAdmin
        ? (QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed)
        : QAbstractItemView::NoEditTriggers;
    table_->setEditTriggers(triggers);
}

void FixedIpListPanel::onAddClicked() {
    if (!adminMode_) return;
    if (table_->rowCount() >= kMaxCameras) {
        countLabel_->setText(QString("Camera limit reached (%1).").arg(kMaxCameras));
        return;
    }
    emit addCameraRequested();
}

void FixedIpListPanel::onDeleteClicked() {
    if (!adminMode_) return;
    const int row = table_->currentRow();
    if (row < 0 || row >= table_->rowCount()) {
        return;
    }
    const int cameraId = cameraIdAtRow(row);
    if (cameraId >= 0) {
        emit deleteCameraRequested(cameraId);
    }
}

void FixedIpListPanel::onCellChanged(int row, int column) {
    if (populating_) return;
    QTableWidgetItem* item = table_->item(row, column);
    if (!item) return;
    const int cameraId = cameraIdAtRow(row);
    if (cameraId < 0) return;

    if (column == 1) {
        emit nameEdited(cameraId, item->text().trimmed());
    } else if (column == 2) {
        const QString ip = item->text().trimmed();
        if (ip.isEmpty() || isValidIpv4(ip)) {
            lastValidIps_.insert(cameraId, ip);
            emit ipEdited(cameraId, ip);
        } else {
            // Revert the cell to the last valid value.
            populating_ = true;
            item->setText(lastValidIps_.value(cameraId, cameraIpAtRow(row)));
            populating_ = false;
        }
    }
}

int FixedIpListPanel::cameraIdAtRow(int row) const {
    if (row < 0 || row >= table_->rowCount()) return -1;
    if (QTableWidgetItem* item = table_->item(row, 0)) {
        return item->data(Qt::UserRole).toInt();
    }
    return -1;
}

QString FixedIpListPanel::cameraIpAtRow(int row) const {
    if (row < 0 || row >= table_->rowCount()) return QString();
    if (QTableWidgetItem* item = table_->item(row, 2)) {
        return item->text();
    }
    return QString();
}
