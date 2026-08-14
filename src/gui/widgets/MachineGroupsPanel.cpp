#include "MachineGroupsPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QFont>

namespace {
const char kMutedLabelStyle[] = "color: #8B949E; font-size: 12px;";
}

MachineGroupsPanel::MachineGroupsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // Header row: title hint + per-group counts.
    auto* header = new QHBoxLayout();
    auto* hintLabel = new QLabel(
        "Camera groups - fixed paper-machine sections. Assign each camera to a "
        "group on its Camera Card. A trigger wired to a group records only that "
        "group's cameras.", this);
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet(kMutedLabelStyle);
    header->addWidget(hintLabel, 1);

    summaryLabel_ = new QLabel(this);
    QFont summaryFont = summaryLabel_->font();
    summaryFont.setBold(true);
    summaryLabel_->setFont(summaryFont);
    summaryLabel_->setStyleSheet(kMutedLabelStyle);
    summaryLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(summaryLabel_);
    layout->addLayout(header);

    // Table: Group | ID | Camera | Location (always read-only)
    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(QStringList()
        << "Group" << "ID" << "Camera" << "Location");
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    layout->addWidget(table_, 1);
}

void MachineGroupsPanel::setCameras(const std::vector<CameraInfo>& cameras) {
    table_->setRowCount(0);
    table_->setRowCount(static_cast<int>(cameras.size()));

    int groupCounts[CameraGroup::kCount] = {0, 0, 0, 0, 0};
    int unassignedCount = 0;

    for (int row = 0; row < static_cast<int>(cameras.size()); ++row) {
        const CameraInfo& cam = cameras[static_cast<size_t>(row)];

        const int group = cam.group;
        if (group >= 0 && group < CameraGroup::kCount) {
            groupCounts[group]++;
        } else {
            unassignedCount++;
        }

        table_->setItem(row, 0, new QTableWidgetItem(CameraGroup::name(group)));
        table_->setItem(row, 1, new QTableWidgetItem(QString::number(cam.id)));
        table_->setItem(row, 2, new QTableWidgetItem(cam.name));
        table_->setItem(row, 3, new QTableWidgetItem(cam.location));
    }

    QStringList counts;
    counts << QString("%1: %2")
                  .arg(CameraGroup::name(CameraGroup::kWire))
                  .arg(groupCounts[CameraGroup::kWire]);
    counts << QString("%1: %2")
                  .arg(CameraGroup::name(CameraGroup::kPressPart))
                  .arg(groupCounts[CameraGroup::kPressPart]);
    counts << QString("%1: %2")
                  .arg(CameraGroup::name(CameraGroup::kPreDryer))
                  .arg(groupCounts[CameraGroup::kPreDryer]);
    counts << QString("%1: %2")
                  .arg(CameraGroup::name(CameraGroup::kAfterDryer))
                  .arg(groupCounts[CameraGroup::kAfterDryer]);
    counts << QString("%1: %2")
                  .arg(CameraGroup::name(CameraGroup::kCalenderReel))
                  .arg(groupCounts[CameraGroup::kCalenderReel]);
    counts << QString("Unassigned: %1").arg(unassignedCount);
    summaryLabel_->setText(counts.join("   "));
}
