#pragma once

#include <QWidget>
#include <QString>
#include <vector>
#include "../../core/CameraManager.h"

class QTableWidget;
class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;

class IpConfiguratorPanel : public QWidget {
    Q_OBJECT

public:
    explicit IpConfiguratorPanel(QWidget* parent = nullptr);

    void refresh();
    void setAdminMode(bool isAdmin);
    void setApplyResult(bool ok, const QString& message);

signals:
    void applyRequested(const QString& mac, const QString& mode, const QString& ip,
                        const QString& mask, const QString& gateway);
    void statusMessage(const QString& message);

private slots:
    void onRefreshClicked();
    void onTableSelectionChanged();
    void onModeChanged(int index);
    void onApplyClicked();

private:
    void setupUI();
    void populateTable(const std::vector<GigEDeviceInfo>& devices);
    void loadRowIntoEditor(int row);
    static bool isValidIpv4(const QString& text);

    QTableWidget* table_;
    QLabel* statusLabel_;
    QComboBox* modeCombo_;
    QLineEdit* ipEdit_;
    QLineEdit* maskEdit_;
    QLineEdit* gatewayEdit_;
    QPushButton* applyBtn_;
    QPushButton* refreshBtn_;

    std::vector<GigEDeviceInfo> devices_;
    bool adminMode_ = true;
    bool applyInFlight_ = false;
};
