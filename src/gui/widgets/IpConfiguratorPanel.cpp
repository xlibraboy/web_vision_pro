#include "IpConfiguratorPanel.h"
#include <QTableWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QTimer>
#include <QDateTime>
#include <QHostAddress>
#include <QAbstractSocket>
#include <QFont>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QKeyEvent>

IpConfiguratorPanel::IpConfiguratorPanel(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void IpConfiguratorPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // Toolbar
    auto* toolbar = new QHBoxLayout();
    refreshBtn_ = new QPushButton("Refresh", this);
    connect(refreshBtn_, &QPushButton::clicked, this, &IpConfiguratorPanel::onRefreshClicked);
    toolbar->addWidget(refreshBtn_);
    statusLabel_ = new QLabel("No scan performed yet.", this);
    statusLabel_->setWordWrap(true);
    toolbar->addWidget(statusLabel_, 1);
    layout->addLayout(toolbar);

    // Discovery table
    table_ = new QTableWidget(this);
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels(QStringList()
        << "Friendly Name" << "User-Defined Name" << "MAC" << "IP Address"
        << "Subnet Mask" << "Gateway" << "Mode");
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &IpConfiguratorPanel::onTableSelectionChanged);
    layout->addWidget(table_, 1);

    // Edit area
    auto* editGroup = new QGroupBox("Apply IP Configuration", this);
    auto* editLayout = new QGridLayout(editGroup);
    editLayout->setSpacing(8);

    // One row per address: four 3-digit octet boxes with fixed "." labels —
    // the dots are structural and can never be deleted.
    auto createOctetRow = [this](QLineEdit* (&out)[4]) {
        auto* row = new QWidget(this);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(2);
        for (int i = 0; i < 4; ++i) {
            if (i > 0) rowLayout->addWidget(new QLabel(".", row));
            auto* box = new QLineEdit(row);
            box->setMaxLength(3);
            box->setAlignment(Qt::AlignCenter);
            box->setFixedWidth(36);
            box->setValidator(new QRegularExpressionValidator(QRegularExpression("\\d{0,3}"), box));
            box->installEventFilter(this);
            connect(box, &QLineEdit::textEdited, this, &IpConfiguratorPanel::onOctetEdited);
            rowLayout->addWidget(box);
            out[i] = box;
        }
        return row;
    };

    modeCombo_ = new QComboBox(editGroup);
    modeCombo_->addItem("Static", QStringLiteral("Static"));
    modeCombo_->addItem("DHCP", QStringLiteral("DHCP"));
    modeCombo_->addItem("AutoIP", QStringLiteral("AutoIP"));
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &IpConfiguratorPanel::onModeChanged);
    editLayout->addWidget(new QLabel("Mode:", editGroup), 0, 0);
    editLayout->addWidget(modeCombo_, 0, 1);

    editLayout->addWidget(new QLabel("IP Address:", editGroup), 1, 0);
    editLayout->addWidget(createOctetRow(ipOctets_), 1, 1);
    editLayout->addWidget(new QLabel("Subnet Mask:", editGroup), 2, 0);
    editLayout->addWidget(createOctetRow(maskOctets_), 2, 1);
    editLayout->addWidget(new QLabel("Gateway:", editGroup), 3, 0);
    editLayout->addWidget(createOctetRow(gatewayOctets_), 3, 1);

    applyBtn_ = new QPushButton("Apply to Camera", editGroup);
    applyBtn_->setEnabled(false);
    connect(applyBtn_, &QPushButton::clicked, this, &IpConfiguratorPanel::onApplyClicked);
    editLayout->addWidget(applyBtn_, 4, 0, 1, 2);

    layout->addWidget(editGroup);
    clearEditor();
}

void IpConfiguratorPanel::refresh() {
    devices_ = CameraManager::enumerateGigEDevices();
    populateTable(devices_);
    clearEditor();
    if (devices_.empty()) {
        statusLabel_->setText("No GigE devices found. Check the network connection and click Refresh.");
    } else {
        statusLabel_->setText(QString("%1 device(s) found at %2.")
            .arg(devices_.size())
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
    }
    applyBtn_->setEnabled(false);
    emit statusMessage(QString("IP Configurator: scan found %1 GigE device(s).").arg(devices_.size()));
}

void IpConfiguratorPanel::populateTable(const std::vector<GigEDeviceInfo>& devices) {
    table_->setRowCount(0);
    table_->setRowCount(static_cast<int>(devices.size()));
    for (int row = 0; row < static_cast<int>(devices.size()); ++row) {
        const GigEDeviceInfo& dev = devices[static_cast<size_t>(row)];
        const QStringList values = {
            QString::fromStdString(dev.friendlyName),
            QString::fromStdString(dev.userDefinedName),
            QString::fromStdString(dev.macAddress),
            QString::fromStdString(dev.ipAddress),
            QString::fromStdString(dev.subnetMask),
            QString::fromStdString(dev.defaultGateway),
            QString::fromStdString(dev.ipConfigMode),
        };
        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values.at(col));
            if (col == 2) item->setFont(QFont("Monospace"));
            table_->setItem(row, col, item);
        }
    }
}

void IpConfiguratorPanel::onRefreshClicked() {
    refresh();
}

void IpConfiguratorPanel::onTableSelectionChanged() {
    const int row = table_->currentRow();
    if (row < 0) {
        clearEditor();
        return;
    }
    modeCombo_->setEnabled(adminMode_);
    applyBtn_->setEnabled(adminMode_ && !applyInFlight_);
    loadRowIntoEditor(row);
    onModeChanged(0);
}

void IpConfiguratorPanel::loadRowIntoEditor(int row) {
    if (row < 0 || row >= static_cast<int>(devices_.size())) return;
    const GigEDeviceInfo& dev = devices_[static_cast<size_t>(row)];
    const int modeIndex = modeCombo_->findData(QString::fromStdString(dev.ipConfigMode));
    modeCombo_->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);

    auto fillOctets = [](QLineEdit* const octets[4], const std::string& value) {
        const QStringList parts = QString::fromStdString(value).split('.');
        for (int i = 0; i < 4; ++i) {
            octets[i]->setText(i < parts.size() ? parts.at(i).trimmed() : QString());
        }
    };
    fillOctets(ipOctets_, dev.ipAddress);
    fillOctets(maskOctets_, dev.subnetMask);
    fillOctets(gatewayOctets_, dev.defaultGateway);
}

void IpConfiguratorPanel::onModeChanged(int) {
    const bool editable = adminMode_ && table_->currentRow() >= 0;
    const bool staticMode = editable && modeCombo_->currentData().toString() == QStringLiteral("Static");
    for (auto* octets : {ipOctets_, maskOctets_, gatewayOctets_}) {
        for (int i = 0; i < 4; ++i) octets[i]->setEnabled(staticMode);
    }
}

void IpConfiguratorPanel::onApplyClicked() {
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(devices_.size())) return;
    const QString mac = QString::fromStdString(devices_[static_cast<size_t>(row)].macAddress);
    const QString mode = modeCombo_->currentData().toString();
    QString ip = octetText(ipOctets_);
    QString mask = octetText(maskOctets_);
    QString gateway = octetText(gatewayOctets_);

    if (mode == QStringLiteral("Static")) {
        // An empty gateway means "no gateway" (0.0.0.0).
        bool gatewayEmpty = true;
        for (int i = 0; i < 4; ++i) {
            if (!gatewayOctets_[i]->text().isEmpty()) { gatewayEmpty = false; break; }
        }
        if (gatewayEmpty) gateway = QStringLiteral("0.0.0.0");
        if (!isValidIpv4(ip) || !isValidIpv4(mask) || !isValidIpv4(gateway)) {
            QMessageBox::warning(this, "Apply IP",
                "IP address, subnet mask, and gateway must be complete IPv4 addresses (e.g. 192.168.1.10).\n"
                "Leave the gateway empty for no gateway (0.0.0.0).");
            return;
        }
        if (gateway == QStringLiteral("0.0.0.0")) {
            QMessageBox::information(this, "Gateway 0.0.0.0",
                "0.0.0.0 (no gateway) will be stored as the persistent setting, but this camera model "
                "keeps its previous gateway in live status \u2014 it cannot apply 0.0.0.0 to the live interface.\n\n"
                "The gateway is not used on a direct camera connection, so this has no effect on operation.");
        }
    }

    applyInFlight_ = true;
    applyBtn_->setEnabled(false);
    statusLabel_->setText(QString("Applying %1 configuration to %2 ...").arg(mode, mac));
    emit applyRequested(mac, mode, ip, mask, gateway);
}

void IpConfiguratorPanel::setApplyResult(bool ok, const QString& message) {
    applyInFlight_ = false;
    applyBtn_->setEnabled(table_->currentRow() >= 0 && adminMode_);
    statusLabel_->setText(message);
    // Refresh on success (camera restarts its network stack) and on failure
    // (the discovery list may have gone stale since the last scan).
    QTimer::singleShot(ok ? 3000 : 1500, this, &IpConfiguratorPanel::refresh);
    if (!ok) {
        QMessageBox::critical(this, "Apply IP", message);
    }
}

void IpConfiguratorPanel::onOctetEdited() {
    auto* box = qobject_cast<QLineEdit*>(sender());
    if (!box) return;
    for (auto* octets : {ipOctets_, maskOctets_, gatewayOctets_}) {
        for (int i = 0; i < 4; ++i) {
            if (octets[i] == box) {
                // Auto-advance to the next octet once this one is full.
                if (box->text().size() >= 3 && i < 3) {
                    octets[i + 1]->setFocus();
                }
                return;
            }
        }
    }
}

bool IpConfiguratorPanel::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Backspace) {
            for (auto* octets : {ipOctets_, maskOctets_, gatewayOctets_}) {
                for (int i = 0; i < 4; ++i) {
                    if (octets[i] == obj) {
                        // Backspace on an empty octet moves to the previous one.
                        if (octets[i]->text().isEmpty() && i > 0) {
                            octets[i - 1]->setFocus();
                        }
                        return QWidget::eventFilter(obj, event);
                    }
                }
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

QString IpConfiguratorPanel::octetText(QLineEdit* const octets[4]) const {
    QStringList parts;
    for (int i = 0; i < 4; ++i) {
        parts << octets[i]->text().trimmed();
    }
    return parts.join(QLatin1Char('.'));
}

void IpConfiguratorPanel::clearEditor() {
    modeCombo_->setCurrentIndex(0);
    for (auto* octets : {ipOctets_, maskOctets_, gatewayOctets_}) {
        for (int i = 0; i < 4; ++i) octets[i]->clear();
    }
    modeCombo_->setEnabled(false);
    applyBtn_->setEnabled(false);
    onModeChanged(0);
}

void IpConfiguratorPanel::setAdminMode(bool isAdmin) {
    adminMode_ = isAdmin;
    refreshBtn_->setEnabled(true);
    table_->setEnabled(isAdmin);
    modeCombo_->setEnabled(isAdmin && table_->currentRow() >= 0);
    applyBtn_->setEnabled(isAdmin && table_->currentRow() >= 0 && !applyInFlight_);
    onModeChanged(0); // re-apply field enable state
}

bool IpConfiguratorPanel::isValidIpv4(const QString& text) {
    QHostAddress addr;
    return addr.setAddress(text.trimmed()) && addr.protocol() == QAbstractSocket::IPv4Protocol;
}
