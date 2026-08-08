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
class FixedDotIpEdit;

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
    // Assign a temporary IP ("Force IP") to the camera so it becomes reachable;
    // no camera-card sync is performed for this temporary assignment.
    void forceIpRequested(const QString& mac, const QString& tempIp,
                          const QString& mask, const QString& gateway);
    void statusMessage(const QString& message);

private slots:
    void onRefreshClicked();
    void onTableSelectionChanged();
    void onModeChanged(int index);
    void onApplyClicked();
    void onForceIpClicked();

private:
    void setupUI();
    void populateTable(const std::vector<GigEDeviceInfo>& devices);
    void loadRowIntoEditor(int row);
    void clearEditor();
    static bool isValidIpv4(const QString& text);
    static bool ipv4OnLocalSubnet(const QString& ip);
    static QStringList hostIpv4Addresses();

    QTableWidget* table_;
    QLabel* statusLabel_;
    QComboBox* modeCombo_;
    FixedDotIpEdit* ipEdit_ = nullptr;
    FixedDotIpEdit* maskEdit_ = nullptr;
    FixedDotIpEdit* gatewayEdit_ = nullptr;
    FixedDotIpEdit* forceIpEdit_ = nullptr;
    QPushButton* applyBtn_;
    QPushButton* forceIpBtn_ = nullptr;
    QPushButton* refreshBtn_;

    std::vector<GigEDeviceInfo> devices_;
    bool adminMode_ = true;
    bool applyInFlight_ = false;
};
