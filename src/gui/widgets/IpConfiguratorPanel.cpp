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
#include <QKeyEvent>
#include <QMouseEvent>

// Single-field IPv4 editor with structural dots: the four octets are kept
// separately and the dots are regenerated on every edit, so they can never
// be deleted or moved. Typing digits fills octets; '.' advances to the next
// octet; Backspace/Delete removes the last digit only.
class FixedDotIpEdit : public QLineEdit {
public:
    explicit FixedDotIpEdit(QWidget* parent = nullptr) : QLineEdit(parent) {
        setMaxLength(15);
    }

    void setOctets(const QString& dotted) {
        for (QString& oct : octets_) oct.clear();
        const QStringList parts = dotted.split(QLatin1Char('.'));
        int idx = 0;
        for (const QString& part : parts) {
            if (idx >= 4) break;
            QString digits;
            for (const QChar c : part) {
                if (c.isDigit()) digits.append(c);
            }
            octets_[idx] = digits.left(3);
            ++idx;
        }
        cursorOctet_ = lastNonEmptyOctet();
        cursorDigit_ = octets_[cursorOctet_].size();
        refreshDisplay();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        const int key = event->key();
        if (key >= Qt::Key_0 && key <= Qt::Key_9) {
            // Typing over a selection replaces the whole value.
            if (hasSelectedText()) clearOctets();
            insertDigit(QLatin1Char('0' + (key - Qt::Key_0)));
            event->accept();
            return;
        }
        if (key == Qt::Key_Period || key == Qt::Key_Comma) {
            if (hasSelectedText()) clearOctets();
            if (!octets_[cursorOctet_].isEmpty() && cursorOctet_ < 3) {
                ++cursorOctet_;
                cursorDigit_ = 0;
                refreshCursor();
            }
            event->accept();
            return;
        }
        if (key == Qt::Key_Backspace) {
            if (hasSelectedText()) {
                clearOctets();
            } else {
                backspace();
            }
            event->accept();
            return;
        }
        if (key == Qt::Key_Delete) {
            if (hasSelectedText()) {
                clearOctets();
            } else {
                deleteAtCursor();
            }
            event->accept();
            return;
        }
        if (key == Qt::Key_Left) {
            moveCursor(-1);
            event->accept();
            return;
        }
        if (key == Qt::Key_Right) {
            moveCursor(1);
            event->accept();
            return;
        }
        if (key == Qt::Key_Home) {
            cursorOctet_ = 0;
            cursorDigit_ = 0;
            refreshCursor();
            event->accept();
            return;
        }
        if (key == Qt::Key_End) {
            cursorOctet_ = lastNonEmptyOctet();
            cursorDigit_ = octets_[cursorOctet_].size();
            refreshCursor();
            event->accept();
            return;
        }
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            event->accept();
            return;
        }
        if (key == Qt::Key_V && (event->modifiers() & Qt::ControlModifier)) {
            event->accept(); // block paste: raw text would bypass the octets
            return;
        }
        QLineEdit::keyPressEvent(event); // Tab, Ctrl+A, Ctrl+C/X, etc.
    }

    void mousePressEvent(QMouseEvent* event) override {
        QLineEdit::mousePressEvent(event);
        snapCursorFromDisplay();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        QLineEdit::mouseDoubleClickEvent(event);
        snapCursorFromDisplay();
    }

private:
    int lastNonEmptyOctet() const {
        for (int i = 3; i >= 0; --i) {
            if (!octets_[i].isEmpty()) return i;
        }
        return 0;
    }

    int displayPos() const {
        int pos = 0;
        for (int i = 0; i < cursorOctet_; ++i) pos += octets_[i].size() + 1;
        pos += cursorDigit_;
        return pos;
    }

    void refreshCursor() {
        setCursorPosition(qBound(0, displayPos(), text().size()));
    }

    void snapCursorFromDisplay() {
        int p = qBound(0, cursorPosition(), text().size());
        for (int i = 0; i < 4; ++i) {
            const int len = octets_[i].size();
            if (p <= len) {
                cursorOctet_ = i;
                cursorDigit_ = p;
                refreshCursor();
                return;
            }
            p -= len + 1;
        }
        cursorOctet_ = lastNonEmptyOctet();
        cursorDigit_ = octets_[cursorOctet_].size();
        refreshCursor();
    }

    void clearOctets() {
        for (QString& oct : octets_) oct.clear();
        cursorOctet_ = 0;
        cursorDigit_ = 0;
        refreshDisplay();
    }

    void insertDigit(const QChar digit) {
        if (octets_[cursorOctet_].size() >= 3) {
            if (cursorDigit_ == octets_[cursorOctet_].size() && cursorOctet_ < 3) {
                ++cursorOctet_;
                cursorDigit_ = 0;
            } else {
                return; // octet is full and the cursor is not at its end
            }
        }
        octets_[cursorOctet_].insert(cursorDigit_, digit);
        ++cursorDigit_;
        refreshDisplay();
    }

    void backspace() {
        if (cursorDigit_ > 0) {
            octets_[cursorOctet_].remove(cursorDigit_ - 1, 1);
            --cursorDigit_;
        } else if (cursorOctet_ > 0) {
            --cursorOctet_;
            cursorDigit_ = octets_[cursorOctet_].size();
            if (cursorDigit_ > 0) {
                octets_[cursorOctet_].remove(cursorDigit_ - 1, 1);
                --cursorDigit_;
            }
        }
        refreshDisplay();
    }

    void deleteAtCursor() {
        if (cursorDigit_ < octets_[cursorOctet_].size()) {
            octets_[cursorOctet_].remove(cursorDigit_, 1);
            refreshDisplay();
        }
    }

    void refreshDisplay() {
        const int last = lastNonEmptyOctet();
        QString text;
        if (!octets_[last].isEmpty()) {
            QStringList parts;
            for (int i = 0; i <= last; ++i) parts << octets_[i];
            text = parts.join(QLatin1Char('.'));
            if (last < 3) text.append(QLatin1Char('.'));
        }
        setText(text);
        refreshCursor();
    }

    void moveCursor(int delta) {
        if (delta < 0) {
            if (cursorDigit_ > 0) {
                --cursorDigit_;
            } else if (cursorOctet_ > 0) {
                --cursorOctet_;
                cursorDigit_ = octets_[cursorOctet_].size();
            }
        } else {
            if (cursorDigit_ < octets_[cursorOctet_].size()) {
                ++cursorDigit_;
            } else if (cursorOctet_ < 3) {
                ++cursorOctet_;
                cursorDigit_ = 0;
            }
        }
        refreshCursor();
    }

    QString octets_[4];
    int cursorOctet_ = 0;
    int cursorDigit_ = 0;
};

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

    modeCombo_ = new QComboBox(editGroup);
    modeCombo_->addItem("Static", QStringLiteral("Static"));
    modeCombo_->addItem("DHCP", QStringLiteral("DHCP"));
    modeCombo_->addItem("AutoIP", QStringLiteral("AutoIP"));
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &IpConfiguratorPanel::onModeChanged);
    editLayout->addWidget(new QLabel("Mode:", editGroup), 0, 0);
    editLayout->addWidget(modeCombo_, 0, 1);

    ipEdit_ = new FixedDotIpEdit(editGroup);
    maskEdit_ = new FixedDotIpEdit(editGroup);
    gatewayEdit_ = new FixedDotIpEdit(editGroup);
    ipEdit_->setPlaceholderText("0.0.0.0");
    maskEdit_->setPlaceholderText("255.255.255.0");
    gatewayEdit_->setPlaceholderText("0.0.0.0");
    editLayout->addWidget(new QLabel("IP Address:", editGroup), 1, 0);
    editLayout->addWidget(ipEdit_, 1, 1);
    editLayout->addWidget(new QLabel("Subnet Mask:", editGroup), 2, 0);
    editLayout->addWidget(maskEdit_, 2, 1);
    editLayout->addWidget(new QLabel("Gateway:", editGroup), 3, 0);
    editLayout->addWidget(gatewayEdit_, 3, 1);

    applyBtn_ = new QPushButton("Apply to Camera", editGroup);
    applyBtn_->setEnabled(false);
    connect(applyBtn_, &QPushButton::clicked, this, &IpConfiguratorPanel::onApplyClicked);
    editLayout->addWidget(applyBtn_, 4, 0, 1, 2);

    layout->addWidget(editGroup);
    clearEditor();
}

void IpConfiguratorPanel::refresh() {
    devices_ = CameraManager::enumerateGigEDevices(/*forceRefresh=*/true);
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
    ipEdit_->setOctets(QString::fromStdString(dev.ipAddress));
    maskEdit_->setOctets(QString::fromStdString(dev.subnetMask));
    gatewayEdit_->setOctets(QString::fromStdString(dev.defaultGateway));
}

void IpConfiguratorPanel::onModeChanged(int) {
    const bool editable = adminMode_ && table_->currentRow() >= 0;
    const bool staticMode = editable && modeCombo_->currentData().toString() == QStringLiteral("Static");
    ipEdit_->setEnabled(staticMode);
    maskEdit_->setEnabled(staticMode);
    gatewayEdit_->setEnabled(staticMode);
}

void IpConfiguratorPanel::onApplyClicked() {
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(devices_.size())) return;
    const QString mac = QString::fromStdString(devices_[static_cast<size_t>(row)].macAddress);
    const QString mode = modeCombo_->currentData().toString();
    QString ip = ipEdit_->text().trimmed();
    QString mask = maskEdit_->text().trimmed();
    QString gateway = gatewayEdit_->text().trimmed();

    if (mode == QStringLiteral("Static")) {
        // An empty gateway means "no gateway" (0.0.0.0).
        if (gateway.isEmpty()) gateway = QStringLiteral("0.0.0.0");
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

void IpConfiguratorPanel::clearEditor() {
    modeCombo_->setCurrentIndex(0);
    ipEdit_->setOctets(QString());
    maskEdit_->setOctets(QString());
    gatewayEdit_->setOctets(QString());
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
