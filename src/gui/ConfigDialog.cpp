#include "ConfigDialog.h"
#include "widgets/CameraCard.h"
#include "widgets/CameraWidget.h"
#include "widgets/AnalysisVideoWidget.h"
#include "widgets/CameraDeviceSettingsDialog.h"
#include "widgets/NetworkSummaryHeader.h"
#include "widgets/DeleteConfirmationDialog.h"
#include "widgets/IpConfiguratorPanel.h"
#include "widgets/FixedIpListPanel.h"
#include "widgets/MachineGroupsPanel.h"
#include "widgets/MachineLayoutPanel.h"
#include "widgets/IconManager.h"
#include "../config/CameraConfig.h"
#include "../core/CameraManager.h"
#include "../processing/EventSignalScanner.h"
#include "../communication/OpcUaClientService.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFormLayout>
#include <QPushButton>
#include <QListWidget>
#include <QStackedWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QApplication>
#include <QScrollArea>
#include <QGroupBox>
#include <QFrame>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <cstdlib>
#include <QEvent>
#include <QDateTime>
#include <QDebug>
#include <QMap>
#include <QSet>
#include <QGridLayout>
#include <QFont>
#include <QBrush>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QEventLoop>
#include <QTableWidget>
#include <QHeaderView>
#include <QOpcUaClient>
#include <QOpcUaEndpointDescription>
#include <QOpcUaProvider>
#include <QTimer>
#include <utility>
#include <algorithm>
namespace {
    struct CuratedFontOption {
        const char* family;
        const char* label;
    };

    const CuratedFontOption kLiveViewFonts[] = {
        {"Aptos", "Aptos | CAM-01: DRYER"},
        {"Inter", "Inter | CAM-01: DRYER"},
        {"Segoe UI", "Segoe UI | CAM-01: DRYER"},
        {"Roboto", "Roboto | CAM-01: DRYER"},
        {"Noto Sans", "Noto Sans | CAM-01: DRYER"}
    };

    void populateCuratedFontCombo(QComboBox* combo) {
        if (!combo) {
            return;
        }
        combo->clear();
        for (const auto& option : kLiveViewFonts) {
            combo->addItem(QString::fromLatin1(option.label), QString::fromLatin1(option.family));
        }
    }

    void selectCuratedFont(QComboBox* combo, const QString& family) {
        if (!combo) {
            return;
        }
        const int index = combo->findData(family);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    }

    QString currentCuratedFontFamily(const QComboBox* combo) {
        if (!combo) {
            return QStringLiteral("Aptos");
        }
        const QVariant family = combo->currentData();
        return family.isValid() ? family.toString() : QStringLiteral("Aptos");
    }

    QString settingsTooltip(const QString& text) {
        return text;
    }

    void updateFontPreviewLabel(QLabel* label, const QString& family, int pixelSize, const QString& sampleText) {
        if (!label) {
            return;
        }
        QFont font(family);
        font.setPixelSize(pixelSize);
        font.setBold(true);
        label->setFont(font);
        label->setText(sampleText);
        label->setToolTip(QString("%1, %2 px").arg(family).arg(pixelSize));
    }

    QString normalizeLiveViewBackgroundStyle(const QString& backgroundStyle) {
        if (backgroundStyle == "textured" ||
            backgroundStyle == "textured_grid" ||
            backgroundStyle == "textured_diagonal" ||
            backgroundStyle == "textured_dots") {
            return QStringLiteral("textured_mesh");
        }

        if (backgroundStyle == "white_textured" ||
            backgroundStyle == "white_textured_grid" ||
            backgroundStyle == "white_textured_diagonal" ||
            backgroundStyle == "white_textured_dots") {
            return QStringLiteral("white_textured_mesh");
        }

        return backgroundStyle;
    }

    bool usesLightLiveViewBackground(const QString& backgroundStyle) {
        return backgroundStyle == "white" || backgroundStyle.startsWith("white_");
    }

    QColor liveViewBaseBackground(const QString& backgroundStyle) {
        return usesLightLiveViewBackground(backgroundStyle) ? QColor("#F2F2F2") : QColor("#000000");
    }

    QBrush liveViewBackgroundBrush(const QString& backgroundStyle) {
        const QString normalizedStyle = normalizeLiveViewBackgroundStyle(backgroundStyle);
        if (normalizedStyle == "textured_mesh" || normalizedStyle == "white_textured_mesh") {
            QPixmap texture(28, 28);
            const bool isWhite = normalizedStyle == "white_textured_mesh";
            texture.fill(isWhite ? QColor("#F3F3F3") : QColor("#0E0E0E"));

            QPainter texturePainter(&texture);
            texturePainter.setPen(QPen(isWhite ? QColor("#D8D8D8") : QColor("#1F1F1F"), 1));
            for (int offset = 0; offset <= 28; offset += 7) {
                texturePainter.drawLine(offset, 0, offset, 28);
                texturePainter.drawLine(0, offset, 28, offset);
            }
            texturePainter.setPen(QPen(isWhite ? QColor("#E6E6E6") : QColor("#181818"), 1));
            texturePainter.drawLine(0, 0, 28, 28);
            texturePainter.drawLine(28, 0, 0, 28);
            texturePainter.end();

            return QBrush(texture);
        }

        return QBrush(liveViewBaseBackground(normalizedStyle));
    }

    QString normalizeIp(const QString& ip) {
        return ip.trimmed();
    }

    QString normalizeMac(const QString& mac) {
        QString normalized;
        normalized.reserve(mac.size());
        for (const QChar ch : mac) {
            if (ch.isLetterOrNumber()) {
                normalized.append(ch.toUpper());
            }
        }
        return normalized;
    }

    bool isValidIpv4String(const QString& text) {
        QHostAddress addr;
        return addr.setAddress(text.trimmed()) && addr.protocol() == QAbstractSocket::IPv4Protocol;
    }

    // Returns the netmask of the local interface whose subnet contains `ip`, or
    // a /16 fallback when no interface matches (used for the Force-IP flow so
    // the temporary address stays reachable from this host).
    QString subnetMaskForIp(const QString& ip) {
        QHostAddress addr;
        if (!addr.setAddress(ip.trimmed())) {
            return QStringLiteral("255.255.0.0");
        }
        const quint32 target = addr.toIPv4Address();
        const QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface& iface : ifaces) {
            if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack)) {
                continue;
            }
            for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
                if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) {
                    continue;
                }
                const QHostAddress netmask = entry.netmask();
                if (netmask.isNull()) {
                    continue;
                }
                if ((entry.ip().toIPv4Address() & netmask.toIPv4Address())
                        == (target & netmask.toIPv4Address())) {
                    return netmask.toString();
                }
            }
        }
        return QStringLiteral("255.255.0.0");
    }

    bool cameraConfigEqual(const CameraInfo& lhs, const CameraInfo& rhs) {
        return lhs.id == rhs.id
            && lhs.source == rhs.source
            && lhs.name == rhs.name
            && lhs.location == rhs.location
            && lhs.side == rhs.side
            && lhs.machinePosition == rhs.machinePosition
            && lhs.floor == rhs.floor
            && normalizeIp(lhs.ipAddress) == normalizeIp(rhs.ipAddress)
            && normalizeMac(lhs.macAddress) == normalizeMac(rhs.macAddress)
            && normalizeIp(lhs.subnetMask) == normalizeIp(rhs.subnetMask)
            && normalizeIp(lhs.defaultGateway) == normalizeIp(rhs.defaultGateway)
            && lhs.fps == rhs.fps
            && lhs.enableAcquisitionFps == rhs.enableAcquisitionFps
            && lhs.width == rhs.width
            && lhs.height == rhs.height
            && lhs.offsetX == rhs.offsetX
            && lhs.offsetY == rhs.offsetY
            && lhs.pixelFormat == rhs.pixelFormat
            && lhs.exposureTimeAbs == rhs.exposureTimeAbs
            && lhs.enableExposureTimeBase == rhs.enableExposureTimeBase
            && lhs.exposureTimeBaseAbs == rhs.exposureTimeBaseAbs
            && lhs.exposureTimeRaw == rhs.exposureTimeRaw
            && lhs.chunkModeActive == rhs.chunkModeActive
            && lhs.enabledChunks == rhs.enabledChunks;
    }

    bool cameraConfigListEqual(const std::vector<CameraInfo>& lhs, const std::vector<CameraInfo>& rhs) {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        for (size_t i = 0; i < lhs.size(); ++i) {
            if (!cameraConfigEqual(lhs[i], rhs[i])) {
                return false;
            }
        }

        return true;
    }

    QString joinCameraIds(const QList<int>& ids) {
        QStringList parts;
        for (int id : ids) {
            parts.append(QString::number(id));
        }
        return parts.join(", ");
    }
    QStringList buildOpcUaDiscoveryCandidates(const QString& configuredEndpoint, bool allowNetworkScan) {
        QStringList candidates;
        QSet<QString> seen;
        auto add = [&](const QString& c) {
            const QString t = c.trimmed();
            if (!t.isEmpty() && !seen.contains(t)) { seen.insert(t); candidates.append(t); }
        };
        add(configuredEndpoint);
        if (!allowNetworkScan) {
            return candidates;
        }
        add(QStringLiteral("opc.tcp://localhost:4840"));
        add(QStringLiteral("opc.tcp://127.0.0.1:4840"));
        const QString localHost = QHostInfo::localHostName().trimmed();
        if (!localHost.isEmpty())
            add(QStringLiteral("opc.tcp://%1:4840").arg(localHost));
        for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
            if (!(iface.flags() & QNetworkInterface::IsUp) ||
                !(iface.flags() & QNetworkInterface::IsRunning) ||
                (iface.flags() & QNetworkInterface::IsLoopBack)) continue;
            for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
                const QHostAddress addr = entry.ip();
                if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback())
                    add(QStringLiteral("opc.tcp://%1:4840").arg(addr.toString()));
            }
        }
        return candidates;
    }

    void stylePrimaryActionButton(QPushButton* button, const ThemeColors& tc) {
        button->setStyleSheet(QString(
            "QPushButton { "
            "  background-color: %1; "
            "  color: %2; "
            "  border: none; "
            "  border-radius: 8px; "
            "  padding: 8px 16px; "
            "  font-size: 13px; "
            "  font-weight: 600; "
            "} "
            "QPushButton:hover { "
            "  background-color: %3; "
            "} "
            "QPushButton:pressed { "
            "  background-color: %1; "
            "}"
        ).arg(tc.primary, tc.bg, tc.btnHover));
    }
}

bool ConfigDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == cameraScrollWidget_ && event->type() == QEvent::Resize) {
        relayoutCameraCards();
    }

    if (obj == (uiPreferencesScrollArea_ ? uiPreferencesScrollArea_->viewport() : nullptr)
            && event->type() == QEvent::Resize) {
        relayoutUiPreferencePanels();
    }

    return QWidget::eventFilter(obj, event);
}

void ConfigDialog::updateOpcUaDiscoveryStatus(const QString& message, bool detected) {
    if (!opcUaDiscoveryStatusLabel_) return;
    const QString color = detected ? CameraConfig::getThemeColors().primary : QStringLiteral("#A9B4C2");
    opcUaDiscoveryStatusLabel_->setText(message);
    opcUaDiscoveryStatusLabel_->setStyleSheet(QString("color: %1; font-size: 11px;").arg(color));
}

void ConfigDialog::refreshOpcUaEndpointDiscovery(bool overwriteExistingEndpoint, bool allowNetworkScan) {
    // Kick off an async probe and return immediately. The old implementation
    // waited synchronously (nested event loop, up to 750ms per candidate) from
    // showEvent, which froze the UI for every entry into System Configuration.
    if (opcUaProbeActive_) {
        return;
    }

    const QString configured = opcUaEndpointEdit_ ? opcUaEndpointEdit_->text().trimmed() : QString();
    if (!allowNetworkScan && configured.isEmpty()) {
        updateOpcUaDiscoveryStatus(QStringLiteral("Enter the OPC UA endpoint URL, or click Detect Server to scan manually."), false);
        opcUaDiscoveryAttempted_ = true;
        return;
    }

    opcUaProbeNotFoundMessage_ = configured.isEmpty()
        ? QStringLiteral("No OPC UA server found on this network. Enter the endpoint URL manually.")
        : QStringLiteral("No OPC UA server responded. Verify your endpoint URL manually.");
    updateOpcUaDiscoveryStatus(QStringLiteral("Scanning for OPC UA servers…"), false);
    if (opcUaDetectEndpointBtn_) opcUaDetectEndpointBtn_->setEnabled(false);

    startOpcUaProbe(buildOpcUaDiscoveryCandidates(configured, allowNetworkScan),
                    overwriteExistingEndpoint);
}

void ConfigDialog::startOpcUaProbe(const QStringList& candidates, bool overwriteExistingEndpoint) {
    if (opcUaProbeActive_) {
        return;
    }

    if (!opcUaProbeTimer_) {
        opcUaProbeTimer_ = new QTimer(this);
        opcUaProbeTimer_->setSingleShot(true);
        opcUaProbeTimer_->setInterval(750);
        connect(opcUaProbeTimer_, &QTimer::timeout,
                this, [this]() { handleOpcUaProbeTimeout(); });
    }

    if (!opcUaProbeProvider_) {
        opcUaProbeProvider_ = new QOpcUaProvider(this);
    }
    opcUaProbeBackend_ = QStringLiteral("open62541");
    opcUaProbeBackendAvailable_ =
        opcUaProbeProvider_->availableBackends().contains(opcUaProbeBackend_, Qt::CaseInsensitive);
    if (!opcUaProbeBackendAvailable_) {
        finishOpcUaProbe(false, QString(),
                         QStringLiteral("Qt OPC UA backend 'open62541' is not available."));
        return;
    }

    if (candidates.isEmpty()) {
        finishOpcUaProbe(false, QString(), opcUaProbeNotFoundMessage_);
        return;
    }

    opcUaProbeCandidates_ = candidates;
    opcUaProbeCandidateIndex_ = 0;
    opcUaProbeOverwrite_ = overwriteExistingEndpoint;
    opcUaProbeActive_ = true;
    probeNextOpcUaCandidate();
}

void ConfigDialog::probeNextOpcUaCandidate() {
    if (!opcUaProbeActive_ || !opcUaProbeProvider_ || !opcUaProbeBackendAvailable_) {
        return;
    }

    if (opcUaProbeCandidateIndex_ >= opcUaProbeCandidates_.size()) {
        finishOpcUaProbe(false, QString(), opcUaProbeNotFoundMessage_);
        return;
    }

    opcUaProbeCurrentCandidate_ = opcUaProbeCandidates_[opcUaProbeCandidateIndex_].trimmed();
    if (opcUaProbeCurrentCandidate_.isEmpty()) {
        ++opcUaProbeCandidateIndex_;
        QTimer::singleShot(0, this, [this]() { probeNextOpcUaCandidate(); });
        return;
    }

    opcUaProbeClient_ = opcUaProbeProvider_->createClient(opcUaProbeBackend_);
    if (!opcUaProbeClient_) {
        ++opcUaProbeCandidateIndex_;
        QTimer::singleShot(0, this, [this]() { probeNextOpcUaCandidate(); });
        return;
    }
    opcUaProbeClient_->setParent(this);

    connect(opcUaProbeClient_, &QOpcUaClient::endpointsRequestFinished, this,
            [this](const QVector<QOpcUaEndpointDescription>& endpoints,
                   QOpcUa::UaStatusCode statusCode, const QUrl&) {
        if (!opcUaProbeActive_) {
            return;
        }

        bool detected = false;
        QString detectedEndpoint;
        if (statusCode == QOpcUa::UaStatusCode::Good && !endpoints.isEmpty()) {
            for (const QOpcUaEndpointDescription& endpoint : endpoints) {
                if (endpoint.endpointUrl().startsWith(QStringLiteral("opc.tcp://"), Qt::CaseInsensitive)) {
                    detectedEndpoint = endpoint.endpointUrl().trimmed();
                    detected = true;
                    break;
                }
            }
            if (!detected) {
                // Server answered, but advertised no opc.tcp endpoint: fall
                // back to the address we probed, like the original flow did.
                detected = true;
                detectedEndpoint = opcUaProbeCurrentCandidate_;
            }
        }
        handleOpcUaProbeOutcome(detected, detectedEndpoint);
    });

    opcUaProbeTimer_->start();
    if (!opcUaProbeClient_->requestEndpoints(opcUaProbeCurrentCandidate_)) {
        // Could not even start the request; move on to the next candidate.
        ++opcUaProbeCandidateIndex_;
        clearActiveOpcUaProbeClient();
        QTimer::singleShot(0, this, [this]() { probeNextOpcUaCandidate(); });
    }
}

void ConfigDialog::handleOpcUaProbeOutcome(bool detected, const QString& endpointUrl) {
    if (!opcUaProbeActive_) {
        return;
    }
    clearActiveOpcUaProbeClient();

    if (detected) {
        finishOpcUaProbe(true, endpointUrl,
                         QStringLiteral("Detected OPC UA server: %1").arg(endpointUrl));
        return;
    }

    ++opcUaProbeCandidateIndex_;
    QTimer::singleShot(0, this, [this]() { probeNextOpcUaCandidate(); });
}

void ConfigDialog::handleOpcUaProbeTimeout() {
    if (!opcUaProbeActive_ || !opcUaProbeClient_) {
        return;
    }
    ++opcUaProbeCandidateIndex_;
    clearActiveOpcUaProbeClient();
    QTimer::singleShot(0, this, [this]() { probeNextOpcUaCandidate(); });
}

void ConfigDialog::clearActiveOpcUaProbeClient() {
    if (opcUaProbeTimer_) {
        opcUaProbeTimer_->stop();
    }
    if (opcUaProbeClient_) {
        disconnect(opcUaProbeClient_, nullptr, this, nullptr);
        if (opcUaProbeClient_->state() != QOpcUaClient::Disconnected) {
            opcUaProbeClient_->disconnectFromEndpoint();
        }
        opcUaProbeClient_->deleteLater();
        opcUaProbeClient_ = nullptr;
    }
}

void ConfigDialog::finishOpcUaProbe(bool detected, const QString& endpointUrl,
                                    const QString& statusMessage) {
    opcUaProbeActive_ = false;
    clearActiveOpcUaProbeClient();
    updateOpcUaDiscoveryStatus(statusMessage, detected);

    if (detected && opcUaEndpointEdit_ &&
            (opcUaProbeOverwrite_ || opcUaEndpointEdit_->text().trimmed().isEmpty())) {
        opcUaEndpointEdit_->setText(endpointUrl);
    }
    if (opcUaDetectEndpointBtn_) {
        opcUaDetectEndpointBtn_->setEnabled(true);
    }
    opcUaDiscoveryAttempted_ = true;
}

void ConfigDialog::setOpcUaRuntimeSource(OpcUaClientService* service) {
    if (opcUaRuntimeSource_ == service) {
        return;
    }
    if (opcUaRuntimeSource_) {
        disconnect(opcUaRuntimeSource_, nullptr, this, nullptr);
    }
    opcUaRuntimeSource_ = service;
    if (service) {
        connect(service, &OpcUaClientService::runtimeStatusChanged,
                this, &ConfigDialog::updateOpcUaRuntimeStatus);
    }
}

void ConfigDialog::updateOpcUaRuntimeStatus(const OpcUaRuntimeStatus& status) {
    lastOpcUaRuntimeStatus_ = status;

    // Client state
    if (opcUaStatusClientLabel_) {
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
        opcUaStatusClientLabel_->setText(QStringLiteral("%1 %2").arg(dot, status.clientStateText));
        opcUaStatusClientLabel_->setStyleSheet(
            QStringLiteral("color: %1; font-size: 12px; font-weight: 600;").arg(color));
    }

    refreshOpcUaSpeedDisplay();

    if (!opcUaStatusTable_) {
        return;
    }

    const int rowCount = qMin(kOpcUaTriggerSlots, opcUaStatusTable_->rowCount());
    for (int i = 0; i < rowCount; ++i) {
        // Identity columns (Name / Type / On) always come from the dialog's live
        // row widgets so unsaved edits show correctly; runtime info (held,
        // value, state, last fired) is overlaid from the service snapshot.
        const OpcUaTriggerRowWidgets& row = opcUaTriggerRows_[static_cast<size_t>(i)];
        OpcUaTagRuntimeStatus tagStatus;
        tagStatus.tagIndex = i;
        tagStatus.name = row.nameEdit ? row.nameEdit->text() : QString();
        if (tagStatus.name.isEmpty()) {
            tagStatus.name = QStringLiteral("Trigger %1").arg(i + 1);
        }
        tagStatus.enabled = row.enabledCheck && row.enabledCheck->isChecked();
        tagStatus.simulated = row.simulatedCombo && row.simulatedCombo->currentData().toBool();
        for (const OpcUaTagRuntimeStatus& snapshotTag : status.tags) {
            if (snapshotTag.tagIndex == i) {
                tagStatus.held = snapshotTag.held;
                tagStatus.active = snapshotTag.active;
                tagStatus.valueText = snapshotTag.valueText;
                tagStatus.lastFiredMs = snapshotTag.lastFiredMs;
                break;
            }
        }

        const QString typeText = tagStatus.simulated
            ? QStringLiteral("Sim") : QStringLiteral("Live");
        QString stateText;
        QString stateColor;
        if (tagStatus.held || tagStatus.active) {
            stateText = QStringLiteral("FIRING");
            stateColor = QStringLiteral("#E0A800");
        } else if (!tagStatus.enabled) {
            stateText = QStringLiteral("Off");
            stateColor = QStringLiteral("#8B949E");
        } else {
            stateText = QStringLiteral("Idle");
            stateColor = QStringLiteral("#4CAF50");
        }

        QString lastFiredText = QStringLiteral("—");
        if (tagStatus.lastFiredMs > 0) {
            lastFiredText = QDateTime::fromMSecsSinceEpoch(tagStatus.lastFiredMs)
                .toString(QStringLiteral("HH:mm:ss.zzz"));
        }

        auto makeItem = [](const QString& text, const QString& color) {
            QTableWidgetItem* item = new QTableWidgetItem(text);
            item->setForeground(QBrush(QColor(color)));
            item->setFlags(Qt::ItemIsEnabled);
            return item;
        };
        opcUaStatusTable_->setItem(i, 0, makeItem(tagStatus.name, QStringLiteral("#E3E3E3")));
        opcUaStatusTable_->setItem(i, 1, makeItem(typeText,
            tagStatus.simulated ? QStringLiteral("#00E5FF") : QStringLiteral("#A9B4C2")));
        opcUaStatusTable_->setItem(i, 2, makeItem(tagStatus.valueText, QStringLiteral("#E3E3E3")));
        opcUaStatusTable_->setItem(i, 3, makeItem(stateText, stateColor));
        opcUaStatusTable_->setItem(i, 4, makeItem(lastFiredText, QStringLiteral("#8B949E")));
    }
}

void ConfigDialog::refreshOpcUaSpeedDisplay() {
    if (!opcUaStatusSpeedLabel_) {
        return;
    }

    // Live Status reflects the primary (first) speed anchor row.
    const OpcUaSpeedRowWidgets& row = opcUaSpeedRows_[0];
    const bool speedEnabled = row.enabledCheck && row.enabledCheck->isChecked();
    const bool speedSimulated = row.simulatedCombo && row.simulatedCombo->currentData().toBool();

    if (speedEnabled && speedSimulated) {
        // Live from the dialog's simulated-speed config (works even before the
        // settings are saved or the service is running). Matches the service's
        // (raw * scale) + offset formula.
        const double rawValue = row.simulatedValueSpin ? row.simulatedValueSpin->value() : 0.0;
        const double scale = row.scaleSpin ? row.scaleSpin->value() : 1.0;
        const double offset = row.offsetSpin ? row.offsetSpin->value() : 0.0;
        QString unit = row.unitEdit ? row.unitEdit->text().trimmed() : QString();
        if (unit.isEmpty()) {
            unit = QStringLiteral("m/min");
        }
        opcUaStatusSpeedLabel_->setText(
            QStringLiteral("%1 %2").arg(QString::number(rawValue * scale + offset, 'f', 2), unit));
        opcUaStatusSpeedLabel_->setStyleSheet(
            QStringLiteral("color: #4CAF50; font-size: 12px; font-weight: 600;"));
        return;
    }

    if (lastOpcUaRuntimeStatus_.speedValid) {
        const QString speedColor = lastOpcUaRuntimeStatus_.speedStale
            ? QStringLiteral("#E0A800") : QStringLiteral("#4CAF50");
        const QString staleSuffix = lastOpcUaRuntimeStatus_.speedStale
            ? QStringLiteral("  (STALE)") : QString();
        opcUaStatusSpeedLabel_->setText(lastOpcUaRuntimeStatus_.speedText + staleSuffix);
        opcUaStatusSpeedLabel_->setStyleSheet(
            QStringLiteral("color: %1; font-size: 12px; font-weight: 600;").arg(speedColor));
        return;
    }

    opcUaStatusSpeedLabel_->setText(QStringLiteral("—"));
    opcUaStatusSpeedLabel_->setStyleSheet(QStringLiteral("color: #8B949E; font-size: 12px;"));
}

void ConfigDialog::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (networkRefreshTimer_) {
        networkRefreshTimer_->start();
    }
    refreshOpcUaEndpointDiscovery(true, false);
}

void ConfigDialog::hideEvent(QHideEvent* event) {
    if (networkRefreshTimer_) {
        networkRefreshTimer_->stop();
    }
    // Release any still-held push-hold trigger buttons so the OPC UA service
    // never keeps firing a tag whose button release was missed (e.g. the dialog
    // was hidden mid-press).
    for (int i = 0; i < kOpcUaTriggerSlots; ++i) {
        emit opcUaManualTriggerRequested(i, false, OpcUaTriggerTagSettings{});
    }
    QWidget::hideEvent(event);
}

void ConfigDialog::onNetworkRefreshTimerTick() {
    if (!isVisible()) {
        return;
    }
    currentGigEDevices_ = CameraManager::enumerateGigEDevices();
    // refreshNetworkStatus() also refreshes the read-only Fixed IP registry.
    refreshNetworkStatus();
    // Keep the storage statistics fresh (new events appear while open).
    refreshStorageStats();
}

void ConfigDialog::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    relayoutUiPreferencePanels();
}

ConfigDialog::ConfigDialog(CameraManager* cameraManager, QWidget *parent)
    : QWidget(parent)
    , cameraManager_(cameraManager)
    , networkSummaryHeader_(nullptr)
    , isAdminMode_(false)
    , primaryColor_("#E3E3E3")
    , accentColor_("#00E5FF") {

    setWindowTitle("System Configuration");

    currentGigEDevices_ = CameraManager::enumerateGigEDevices();

    setupUI();

    connect(ipConfiguratorPanel_, &IpConfiguratorPanel::applyRequested,
            this, &ConfigDialog::onIpConfiguratorApplyRequested);
    connect(ipConfiguratorPanel_, &IpConfiguratorPanel::forceIpRequested,
            this, &ConfigDialog::onIpConfiguratorForceIpRequested);
    connect(ipConfiguratorPanel_, &IpConfiguratorPanel::statusMessage,
            this, [this](const QString& message) {
        if (connectionLogsBrowser_) connectionLogsBrowser_->append(message);
    });
    ipConfiguratorPanel_->refresh();

    loadSettings();
    liveViewGridTitlePresets_.updateStyles();
    liveViewDetailTitlePresets_.updateStyles();
    liveViewDetailSectionPresets_.updateStyles();
    analysisVideoTitlePresets_.updateStyles();
    analysisTimestampPresets_.updateStyles();
    analysisTabPresets_.updateStyles();
    analysisMetadataPresets_.updateStyles();
    setupUiModificationTracking();

    // Periodically re-scan GigE devices while the config tab is visible so the
    // Fixed IP registry's Detected IP column and the network summary stay live.
    // GigE enumeration can block the UI thread (broadcast scan, esp. offline),
    // so keep the cadence conservative.
    networkRefreshTimer_ = new QTimer(this);
    networkRefreshTimer_->setInterval(10000);
    connect(networkRefreshTimer_, &QTimer::timeout,
            this, &ConfigDialog::onNetworkRefreshTimerTick);
}

void ConfigDialog::showRecordingSettingsPage() {
    if (!sidebar_)
        return;
    for (int i = 0; i < sidebar_->count(); ++i) {
        if (sidebar_->item(i)->text() == QLatin1String("Recording & Triggers")) {
            sidebar_->setCurrentRow(i);
            return;
        }
    }
}

ConfigDialog::~ConfigDialog() = default;

void ConfigDialog::setCameraManager(CameraManager* manager) {
    cameraManager_ = manager;
}

void ConfigDialog::setupUI() {
    constexpr int kPageMargin = 16;
    constexpr int kSectionSpacing = 16;
    constexpr int kControlSpacing = 12;
    constexpr int kSidebarMinWidth = 220;
    constexpr int kSidebarContentPadding = 64;

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(kSectionSpacing);
    mainLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    ThemeColors tc = CameraConfig::getThemeColors();

    auto createPillStyle = [&tc](bool active) {
        if (active) {
            return QString(
                "QPushButton { background-color: %1; color: %2; border: 1px solid %1; border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: 700; } "
                "QPushButton:hover { background-color: %3; } "
                "QPushButton:pressed { background-color: %1; }"
            ).arg(tc.primary, tc.bg, tc.text);
        }
        return QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: 600; } "
            "QPushButton:hover { border-color: %4; } "
            "QPushButton:pressed { background-color: %4; }"
        ).arg(tc.btnBg, tc.text, tc.border, tc.primary);
    };

    auto createTypographyRow = [this, &tc, &createPillStyle](QWidget* parent, QComboBox* fontCombo, QSpinBox* sizeSpin,
            const QString& tooltip, int presetS, int presetM, int presetL, PresetButtonGroup& presets) {
        const QString formattedTooltip = settingsTooltip(tooltip);
        QWidget* row = new QWidget(parent);
        QVBoxLayout* rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);
        row->setToolTip(formattedTooltip);

        QHBoxLayout* controlsLayout = new QHBoxLayout();
        controlsLayout->setContentsMargins(0, 0, 0, 0);
        controlsLayout->setSpacing(8);
        controlsLayout->addWidget(fontCombo, 1);
        controlsLayout->addWidget(sizeSpin);
        rowLayout->addLayout(controlsLayout);

        QHBoxLayout* presetsLayout = new QHBoxLayout();
        presetsLayout->setContentsMargins(0, 0, 0, 0);
        presetsLayout->setSpacing(6);
        presetsLayout->addStretch();
        rowLayout->addLayout(presetsLayout);

        const QString inactiveStyle = createPillStyle(false);

        presets.targetSpin = sizeSpin;
        presets.presetS = presetS;
        presets.presetM = presetM;
        presets.presetL = presetL;
        presets.inactiveStyle = inactiveStyle;
        presets.activeStyle = createPillStyle(true);

        const auto addPill = [row, presetsLayout, sizeSpin, &presets](const QString& label, int preset) {
            QPushButton* btn = new QPushButton(label, row);
            btn->setFixedSize(28, 22);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setToolTip(settingsTooltip(QString("%1: %2 px").arg(label, QString::number(preset))));
            presetsLayout->addWidget(btn);
            QObject::connect(btn, &QPushButton::clicked, [sizeSpin, preset]() {
                sizeSpin->setValue(preset);
            });
            return btn;
        };

        presets.btnS = addPill("S", presetS);
        presets.btnM = addPill("M", presetM);
        presets.btnL = addPill("L", presetL);

        QObject::connect(sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), [&presets]() {
            presets.updateStyles();
        });

        return row;
    };
    
    // Create list widget for sidebar navigation
    QListWidget* sidebar = sidebar_ = new QListWidget(this);
    sidebar->setIconSize(QSize(20, 20));
    sidebar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebar->setTextElideMode(Qt::ElideNone);
    sidebar->setFixedWidth(kSidebarMinWidth);
    sidebar->setStyleSheet(QString(
        "QListWidget { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 8px; "
        "  outline: 0; "
        "} "
        "QListWidget::item { "
        "  padding: 10px 12px; "
        "  color: %3; "
        "  font-size: 14px; "
        "  font-weight: 600; "
        "  border-bottom: 1px solid %2; "
        "} "
        "QListWidget::item:selected { "
        "  background-color: %4; "
        "  border-left: 4px solid %5; "
        "} "
        "QListWidget::item:hover:!selected { "
        "  background-color: %4; "
        "}"
    ).arg(tc.btnBg, tc.border, tc.text, tc.bg, tc.primary));

    QStackedWidget* stackedWidget = new QStackedWidget(this);
    stackedWidget->setStyleSheet("QStackedWidget { background: transparent; }");

    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(kSectionSpacing);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(sidebar);
    contentLayout->addWidget(stackedWidget, 1);


    // Camera Configuration Tab
    QWidget* camSetupGroup = new QWidget(this);
    QVBoxLayout* camSetupLayout = new QVBoxLayout(camSetupGroup);
    camSetupLayout->setSpacing(kSectionSpacing);
    camSetupLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    // Premium Network Summary Header
    networkSummaryHeader_ = new NetworkSummaryHeader(this);
    connect(networkSummaryHeader_, &NetworkSummaryHeader::refreshRequested,
            this, &ConfigDialog::onRefreshLogsClicked);
    // Removed clear/toggle signals as they belong to diagnostics tab now
    connect(networkSummaryHeader_, &NetworkSummaryHeader::addCameraRequested,
            this, &ConfigDialog::onAddCameraConfigClicked);
    camSetupLayout->addWidget(networkSummaryHeader_);

    // Scroll area for camera cards
    cameraScrollArea_ = new QScrollArea(this);
    cameraScrollArea_->setWidgetResizable(true);
    cameraScrollArea_->setFrameShape(QFrame::NoFrame);
    cameraScrollArea_->setStyleSheet(QString(
        "QScrollArea { border: none; background: transparent; } "
        "QScrollBar:vertical { "
        "  background-color: %1; "
        "  width: 12px; "
        "  border-radius: 6px; "
        "} "
        "QScrollBar::handle:vertical { "
        "  background-color: %2; "
        "  border-radius: 6px; "
        "  min-height: 30px; "
        "} "
        "QScrollBar::handle:vertical:hover { "
        "  background-color: %3; "
        "} "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
        "  height: 0px; "
        "}"
    ).arg(tc.bg, tc.btnBg, tc.primary));

    cameraScrollWidget_ = new QWidget();
    cameraListLayout_ = new QGridLayout(cameraScrollWidget_);
    cameraListLayout_->setSpacing(kSectionSpacing);
    cameraListLayout_->setContentsMargins(0, 0, 8, 0);
    cameraListLayout_->setAlignment(Qt::AlignTop);
    cameraScrollWidget_->installEventFilter(this);

    cameraScrollArea_->setWidget(cameraScrollWidget_);

    QTabWidget* cameraSubTabs = new QTabWidget(camSetupGroup);
    cameraSubTabs->setDocumentMode(true);
    cameraSubTabs->setStyleSheet(QString(
        "QTabWidget::pane { border: 1px solid %1; border-radius: 10px; top: -1px; background-color: rgba(255, 255, 255, 0.01); padding: 2px; } "
        "QTabBar::tab { background-color: %2; color: %3; border: 1px solid %1; border-bottom: none; padding: 6px 14px; min-width: 110px; border-top-left-radius: 8px; border-top-right-radius: 8px; font-weight: 600; font-size: 12px; } "
        "QTabBar::tab:selected { color: %4; background-color: rgba(255, 255, 255, 0.04); margin-bottom: -1px; } "
        "QTabBar::tab:!selected { margin-top: 3px; color: %3; } "
        "QTabBar::tab:hover { color: %4; }"
    ).arg(tc.border, tc.btnBg, tc.text, tc.primary));

    // Tab 1: Camera Cards (existing scroll area + save button)
    QWidget* cameraCardsPage = new QWidget(cameraSubTabs);
    QVBoxLayout* cameraCardsPageLayout = new QVBoxLayout(cameraCardsPage);
    cameraCardsPageLayout->setContentsMargins(0, 8, 0, 0);
    cameraCardsPageLayout->setSpacing(kSectionSpacing);
    cameraCardsPageLayout->addWidget(cameraScrollArea_, 1);

    QHBoxLayout* cameraActionsLayout = new QHBoxLayout();
    cameraActionsLayout->addStretch();

    cameraSaveBtn_ = new QPushButton("Save Camera Configuration", cameraCardsPage);
    cameraSaveBtn_->setIcon(IconManager::instance().save(16));
    cameraSaveBtn_->setDefault(true);
    stylePrimaryActionButton(cameraSaveBtn_, tc);
    connect(cameraSaveBtn_, &QPushButton::clicked, this, &ConfigDialog::saveCameraConfiguration);
    cameraActionsLayout->addWidget(cameraSaveBtn_);
    cameraCardsPageLayout->addLayout(cameraActionsLayout);
    cameraSubTabs->addTab(cameraCardsPage, "Camera Cards");

    // Tab 2: Fixed IP List — read-only registry of fixed IPs per camera ID.
    // Camera IDs are counted/added/removed on the Camera Cards tab; this panel
    // is a display-only mirror refreshed via refreshFixedIpList().
    fixedIpListPanel_ = new FixedIpListPanel(cameraSubTabs);
    cameraSubTabs->addTab(fixedIpListPanel_, "Fixed IP List");

    // Tab 3: Machine Groups — read-only registry of the 4 fixed camera groups.
    // Assigning a camera to a group happens on its Camera Card; this panel is
    // a display-only mirror refreshed via refreshMachineGroups().
    machineGroupsPanel_ = new MachineGroupsPanel(cameraSubTabs);
    cameraSubTabs->addTab(machineGroupsPanel_, "Machine Groups");

    // Tab 4: IP Configurator
    ipConfiguratorPanel_ = new IpConfiguratorPanel(cameraSubTabs);
    cameraSubTabs->addTab(ipConfiguratorPanel_, "IP Configurator");

    camSetupLayout->addWidget(cameraSubTabs, 1);

    QListWidgetItem* camSetupItem = new QListWidgetItem(IconManager::instance().settings(20), "Camera Configuration");
    sidebar->addItem(camSetupItem);
    stackedWidget->addWidget(camSetupGroup);

    // Machine Layout Tab — visual map of the machine: every camera on a
    // distance line (mm) at its Camera Card position, plus all marked & aligned
    // defects projected onto the same scale from the event database.
    QWidget* machineLayoutGroup = new QWidget(this);
    QVBoxLayout* machineLayoutPageLayout = new QVBoxLayout(machineLayoutGroup);
    machineLayoutPageLayout->setSpacing(kSectionSpacing);
    machineLayoutPageLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    machineLayoutPanel_ = new MachineLayoutPanel(machineLayoutGroup);
    machineLayoutPageLayout->addWidget(machineLayoutPanel_, 1);
    QListWidgetItem* machineLayoutItem = new QListWidgetItem(IconManager::instance().settings(20), "Machine Layout");
    sidebar->addItem(machineLayoutItem);
    stackedWidget->addWidget(machineLayoutGroup);

    // Recording & Triggers Tab
    QWidget* bufferGroup = new QWidget(this);
    QVBoxLayout* bufferLayout = new QVBoxLayout(bufferGroup);
    bufferLayout->setSpacing(kSectionSpacing);
    bufferLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    const QString sectionStyle = QString(
        "QGroupBox { font-weight: 600; color: %1; border: 1px solid %2; "
        "border-radius: 8px; margin-top: 8px; padding-top: 8px; font-size: 12px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
    ).arg(tc.primary, tc.border);

    auto createSectionForm = [&](const QString& title) {
        QGroupBox* group = new QGroupBox(title, bufferGroup);
        group->setStyleSheet(sectionStyle);
        QFormLayout* form = new QFormLayout(group);
        form->setSpacing(kControlSpacing);
        form->setContentsMargins(14, 18, 14, 14);
        form->setHorizontalSpacing(kSectionSpacing);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bufferLayout->addWidget(group);
        return form;
    };

    QFormLayout* recordingForm = createSectionForm("Recording");

    // Global FPS
    globalFpsSpin_ = new QSpinBox(bufferGroup);
    globalFpsSpin_->setRange(1, 200);
    globalFpsSpin_->setSuffix(" fps");
    globalFpsSpin_->setStyleSheet(QString(
        "QSpinBox { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 6px; "
        "  padding: 6px 10px; "
        "  color: %3; "
        "  min-width: 100px; "
        "} "
        "QSpinBox:hover { border-color: %4; } "
        "QSpinBox:focus { border-color: %4; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    recordingForm->addRow("Fallback FPS:", globalFpsSpin_);

    // Pre-Trigger
    preTriggerSpin_ = new QSpinBox(bufferGroup);
    preTriggerSpin_->setRange(1, 60);
    preTriggerSpin_->setSuffix(" sec");
    preTriggerSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    recordingForm->addRow("Pre-Trigger Buffer:", preTriggerSpin_);

    // Post-Trigger
    postTriggerSpin_ = new QSpinBox(bufferGroup);
    postTriggerSpin_->setRange(1, 60);
    postTriggerSpin_->setSuffix(" sec");
    postTriggerSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    recordingForm->addRow("Post-Trigger Recording:", postTriggerSpin_);

    // Frame-count estimate: (pre + post) seconds × fallback FPS. Above 600
    // frames the whole-event dashboard chart samples every Mth frame (600
    // cap); the DETAIL strip still shows every frame around the playhead.
    recordingInfoLabel_ = new QLabel(bufferGroup);
    recordingInfoLabel_->setWordWrap(true);
    recordingInfoLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; }").arg(tc.text));
    recordingForm->addRow(recordingInfoLabel_);
    connect(globalFpsSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ConfigDialog::updateRecordingInfoLabel);
    connect(preTriggerSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ConfigDialog::updateRecordingInfoLabel);
    connect(postTriggerSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ConfigDialog::updateRecordingInfoLabel);

    // Camera Mode: Real hardware vs. Emulated cameras (no hardware needed).
    // Defaults to Real. Switching re-runs the camera lifecycle (Pylon is
    // re-initialized with the new PYLON_CAMEMU device set), so it applies on
    // save or on the next Server-button start - no app restart required.
    QFormLayout* cameraModeForm = createSectionForm("Camera Mode");

    cameraSourceCombo_ = new QComboBox(bufferGroup);
    cameraSourceCombo_->addItem("Real Cameras (hardware)", static_cast<int>(CameraConfig::CameraSource::RealCamera));
    cameraSourceCombo_->addItem("Emulated Cameras (no hardware)", static_cast<int>(CameraConfig::CameraSource::Emulation));
    cameraSourceCombo_->setStyleSheet(QString(
        "QComboBox { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 6px; "
        "  padding: 6px 10px; "
        "  color: %3; "
        "  min-width: 180px; "
        "} "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; width: 22px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    cameraModeForm->addRow("Camera Source:", cameraSourceCombo_);

    QLabel* cameraModeNote = new QLabel(
        "Emulated cameras need no hardware and are handy for testing without a "
        "machine. This switch only activates the \"Emulated\" Source setting on "
        "individual camera cards - cards left on Real still use hardware. The "
        "switch applies when the camera system is (re)started, e.g. saving this "
        "page or toggling the Server button in the Analysis View.", bufferGroup);
    cameraModeNote->setWordWrap(true);
    cameraModeNote->setStyleSheet(QString("color: %1; padding-top: 4px;").arg(tc.text));
    cameraModeForm->addRow("", cameraModeNote);

    QFormLayout* retentionForm = createSectionForm("Record Storage");

    eventRetentionSpin_ = new QSpinBox(bufferGroup);
    eventRetentionSpin_->setRange(1, 10000);
    eventRetentionSpin_->setSuffix(" records");
    eventRetentionSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    retentionForm->addRow("Keep Recent Records:", eventRetentionSpin_);

    // Event storage folder (moved here from UI Preferences; saved with recording settings)
    QWidget* eventStorageRowWidget = new QWidget(bufferGroup);
    QHBoxLayout* eventStorageRowLayout = new QHBoxLayout(eventStorageRowWidget);
    eventStorageRowLayout->setContentsMargins(0, 0, 0, 0);
    eventStorageRowLayout->setSpacing(8);

    eventStoragePathEdit_ = new QLineEdit(eventStorageRowWidget);
    eventStoragePathEdit_->setReadOnly(true);
    eventStoragePathEdit_->setStyleSheet(QString(
        "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 6px 10px; font-size: 11px; }"
    ).arg(tc.btnBg, tc.text, tc.border));
    eventStorageRowLayout->addWidget(eventStoragePathEdit_, 1);

    browseEventStorageBtn_ = new QPushButton("Browse", eventStorageRowWidget);
    browseEventStorageBtn_->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 6px 12px; font-size: 11px; font-weight: 500; } "
        "QPushButton:hover { border-color: %4; background-color: rgba(255, 255, 255, 0.04); }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.primary));
    browseEventStorageBtn_->setToolTip("Choose a different folder for event storage.");
    connect(browseEventStorageBtn_, &QPushButton::clicked, this, [this]() {
        const QString selectedDir = QFileDialog::getExistingDirectory(
            this,
            "Select Event Storage Folder",
            eventStoragePathEdit_ ? eventStoragePathEdit_->text() : CameraConfig::getEventStoragePath());
        if (!selectedDir.isEmpty() && eventStoragePathEdit_) {
            eventStoragePathEdit_->setText(QDir::cleanPath(selectedDir));
        }
    });
    eventStorageRowLayout->addWidget(browseEventStorageBtn_);

    resetEventStorageBtn_ = new QPushButton("Default", eventStorageRowWidget);
    resetEventStorageBtn_->setStyleSheet(browseEventStorageBtn_->styleSheet());
    resetEventStorageBtn_->setToolTip("Restore the default event storage path.");
    connect(resetEventStorageBtn_, &QPushButton::clicked, this, [this]() {
        if (eventStoragePathEdit_) {
            eventStoragePathEdit_->setText(CameraConfig::getDefaultEventStoragePath());
        }
    });
    eventStorageRowLayout->addWidget(resetEventStorageBtn_);

    QLabel* storageFolderLabel = new QLabel("Folder:", bufferGroup);
    storageFolderLabel->setToolTip("Directory where event recordings and metadata are saved.");
    retentionForm->addRow(storageFolderLabel, eventStorageRowWidget);

    QLabel* eventStorageNote = new QLabel("Used by new recordings and historical event loading.", bufferGroup);
    eventStorageNote->setWordWrap(true);
    eventStorageNote->setStyleSheet(QString("color: %1; font-size: 11px; font-style: italic;").arg(tc.text));
    retentionForm->addRow("", eventStorageNote);

    // Live storage statistics for the configured event folder.
    storageStatsLabel_ = new QLabel(bufferGroup);
    storageStatsLabel_->setWordWrap(true);
    storageStatsLabel_->setStyleSheet(QString(
        "color: %1; font-size: 11px; padding-top: 4px;"
    ).arg(tc.text));
    retentionForm->addRow("Storage:", storageStatsLabel_);

    // Configurable low-disk warning threshold (percent of volume free).
    lowDiskThresholdSpin_ = new QSpinBox(bufferGroup);
    lowDiskThresholdSpin_->setRange(1, 99);
    lowDiskThresholdSpin_->setSuffix(" %");
    lowDiskThresholdSpin_->setValue(CameraConfig::getLowDiskWarningPct());
    lowDiskThresholdSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    lowDiskThresholdSpin_->setToolTip(
        "When free space drops below this percentage of the storage volume, "
        "the Storage row turns amber (red below half of this value).");
    retentionForm->addRow("Low Disk Warning:", lowDiskThresholdSpin_);

    QFormLayout* triggerForm = createSectionForm("Triggering");

    QLabel* defectNote = new QLabel("Defect trigger is controlled from the Live screen for immediate operation.", bufferGroup);
    defectNote->setWordWrap(true);
    defectNote->setStyleSheet(QString("color: %1; padding-top: 4px;").arg(tc.text));
    triggerForm->addRow("Defect Trigger:", defectNote);

    // Unsaved-changes tracking for the recording settings.
    connect(globalFpsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkRecordingSettingsModified);
    connect(preTriggerSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkRecordingSettingsModified);
    connect(postTriggerSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkRecordingSettingsModified);
    connect(eventRetentionSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkRecordingSettingsModified);
    connect(lowDiskThresholdSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkRecordingSettingsModified);
    connect(lowDiskThresholdSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::refreshStorageStats);
    connect(cameraSourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkRecordingSettingsModified);
    // Browse/Default buttons only call setText() on the path edit, so the
    // textChanged connection above covers them too.
    connect(eventStoragePathEdit_, &QLineEdit::textChanged, this, &ConfigDialog::checkRecordingSettingsModified);
    connect(eventStoragePathEdit_, &QLineEdit::textChanged, this, &ConfigDialog::refreshStorageStats);

    bufferLayout->addStretch();

    QHBoxLayout* recordingActionsLayout = new QHBoxLayout();
    recordingActionsLayout->addStretch();

    // Unsaved-changes indicator for the Recording & Triggers page.
    recordingUnsavedIndicator_ = new QLabel(bufferGroup);
    recordingUnsavedIndicator_->setStyleSheet(QString(
        "color: #FFB020; font-size: 11px; font-weight: 600;"
    ));
    recordingUnsavedIndicator_->setText("");
    recordingUnsavedIndicator_->setToolTip("You have unsaved changes in this section.");
    recordingActionsLayout->addWidget(recordingUnsavedIndicator_, 0, Qt::AlignVCenter);
    recordingActionsLayout->addSpacing(8);

    recordingSaveBtn_ = new QPushButton("Save Recording Settings", bufferGroup);
    recordingSaveBtn_->setIcon(IconManager::instance().save(16));
    stylePrimaryActionButton(recordingSaveBtn_, tc);
    connect(recordingSaveBtn_, &QPushButton::clicked, this, &ConfigDialog::saveRecordingSettings);
    recordingActionsLayout->addWidget(recordingSaveBtn_);
    bufferLayout->addLayout(recordingActionsLayout);

    QListWidgetItem* globalGroupItem = new QListWidgetItem(IconManager::instance().warning(20), "Recording & Triggers");
    sidebar->addItem(globalGroupItem);
    stackedWidget->addWidget(bufferGroup);

    // OPC UA Tab. Wrapped in a scroll area so the trigger grid (now sized for
    // every sheet-break sensor, kOpcUaTriggerSlots = 12) and the live status
    // table stay reachable on shorter screens.
    QScrollArea* opcUaScrollArea = new QScrollArea(this);
    opcUaScrollArea->setWidgetResizable(true);
    opcUaScrollArea->setFrameShape(QFrame::NoFrame);
    opcUaScrollArea->setStyleSheet(QString(
        "QScrollArea { border: none; background: transparent; } "
        "QScrollBar:vertical { "
        "  background-color: %1; "
        "  width: 12px; "
        "  border-radius: 6px; "
        "} "
        "QScrollBar::handle:vertical { "
        "  background-color: %2; "
        "  border-radius: 6px; "
        "  min-height: 30px; "
        "} "
        "QScrollBar::handle:vertical:hover { "
        "  background-color: %3; "
        "} "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
        "  height: 0px; "
        "}"
    ).arg(tc.bg, tc.btnBg, tc.primary));
    QWidget* opcUaGroup = new QWidget(opcUaScrollArea);
    QVBoxLayout* opcUaLayout = new QVBoxLayout(opcUaGroup);
    opcUaLayout->setSpacing(kSectionSpacing);
    opcUaLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    const QString opcUaLineEditStyle = QString(
        "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 6px 10px; } "
        "QLineEdit:hover { border-color: %4; } "
        "QLineEdit:focus { border-color: %4; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.primary);
    const QString opcUaComboStyle = QString(
        "QComboBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 6px 10px; min-width: 120px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; width: 20px; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.primary);
    // Compact combo variant for the trigger grid so the extra Sim column stays tight.
    const QString opcUaGridComboStyle = QString(
        "QComboBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 8px; min-width: 64px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; width: 16px; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.primary);
    // Compact push-hold trigger button style for the grid.
    const QString opcUaTriggerBtnStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 14px; font-weight: 600; } "
        "QPushButton:hover { border-color: %4; } "
        "QPushButton:pressed { background-color: %4; color: %2; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.primary);

    auto createOpcUaForm = [&](QGroupBox* group) {
        QFormLayout* form = new QFormLayout(group);
        form->setSpacing(kControlSpacing);
        form->setContentsMargins(14, 18, 14, 14);
        form->setHorizontalSpacing(kSectionSpacing);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return form;
    };

    QLabel* opcUaHeaderLabel = new QLabel("OPC UA Client", opcUaGroup);
    opcUaHeaderLabel->setStyleSheet(QString("color: %1; font-size: 17px; font-weight: 700;").arg(tc.primary));
    opcUaLayout->addWidget(opcUaHeaderLabel);

    QLabel* opcUaDescriptionLabel = new QLabel(
        "Connect to external boolean trigger tags and a machine speed tag for event synchronization.",
        opcUaGroup);
    opcUaDescriptionLabel->setWordWrap(true);
    opcUaDescriptionLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(tc.text));
    opcUaLayout->addWidget(opcUaDescriptionLabel);

    QTabWidget* opcUaTabs = new QTabWidget(opcUaGroup);
    opcUaTabs->setDocumentMode(true);
    opcUaTabs->setStyleSheet(QString(
        "QTabWidget::pane { border: 1px solid %1; border-radius: 10px; top: -1px; background-color: rgba(255, 255, 255, 0.01); padding: 2px; } "
        "QTabBar::tab { background-color: %2; color: %3; border: 1px solid %1; border-bottom: none; padding: 6px 14px; min-width: 110px; border-top-left-radius: 8px; border-top-right-radius: 8px; font-weight: 600; font-size: 12px; } "
        "QTabBar::tab:selected { color: %4; background-color: rgba(255, 255, 255, 0.04); margin-bottom: -1px; } "
        "QTabBar::tab:!selected { margin-top: 3px; color: %5; } "
        "QTabBar::tab:hover { color: %4; }"
    ).arg(tc.border, tc.btnBg, tc.text, tc.primary, tc.text));
    opcUaLayout->addWidget(opcUaTabs, 1);

    QWidget* opcUaConnectionTab = new QWidget(opcUaTabs);
    QVBoxLayout* opcUaConnectionTabLayout = new QVBoxLayout(opcUaConnectionTab);
    opcUaConnectionTabLayout->setContentsMargins(10, 10, 10, 10);
    opcUaConnectionTabLayout->setSpacing(10);

    QGroupBox* opcUaConnectionGroup = new QGroupBox("Connection", opcUaConnectionTab);
    opcUaConnectionGroup->setStyleSheet(sectionStyle);
    QFormLayout* opcUaConnectionForm = createOpcUaForm(opcUaConnectionGroup);

    opcUaEnabledCheck_ = new QCheckBox("Enable OPC UA client", opcUaConnectionGroup);
    opcUaEnabledCheck_->setStyleSheet(QString("color: %1;").arg(tc.text));
    opcUaConnectionForm->addRow("Client:", opcUaEnabledCheck_);

    opcUaEndpointEdit_ = new QLineEdit(opcUaConnectionGroup);
    opcUaEndpointEdit_->setPlaceholderText("opc.tcp://127.0.0.1:4840");
    opcUaEndpointEdit_->setStyleSheet(opcUaLineEditStyle);
    opcUaConnectionForm->addRow("Endpoint URL:", opcUaEndpointEdit_);
    QWidget* opcUaEndpointActions = new QWidget(opcUaConnectionGroup);
    QHBoxLayout* opcUaEndpointActionsLayout = new QHBoxLayout(opcUaEndpointActions);
    opcUaEndpointActionsLayout->setContentsMargins(0, 0, 0, 0);
    opcUaEndpointActionsLayout->setSpacing(8);

    opcUaDiscoveryStatusLabel_ = new QLabel("Checking for discoverable OPC UA servers...", opcUaEndpointActions);
    opcUaDiscoveryStatusLabel_->setWordWrap(true);
    opcUaDiscoveryStatusLabel_->setStyleSheet(QString("color: %1; font-size: 11px;").arg(tc.text));
    opcUaEndpointActionsLayout->addWidget(opcUaDiscoveryStatusLabel_, 1);

    opcUaDetectEndpointBtn_ = new QPushButton("Detect Server", opcUaEndpointActions);
    stylePrimaryActionButton(opcUaDetectEndpointBtn_, tc);
    opcUaDetectEndpointBtn_->setIcon(IconManager::instance().refresh(16));
    connect(opcUaDetectEndpointBtn_, &QPushButton::clicked, this, [this]() {
        refreshOpcUaEndpointDiscovery(false, true);
    });
    opcUaEndpointActionsLayout->addWidget(opcUaDetectEndpointBtn_, 0, Qt::AlignTop);
    opcUaConnectionForm->addRow(QString(), opcUaEndpointActions);


    opcUaPublishIntervalSpin_ = new QSpinBox(opcUaConnectionGroup);
    opcUaPublishIntervalSpin_->setRange(50, 10000);
    opcUaPublishIntervalSpin_->setSuffix(" ms");
    opcUaPublishIntervalSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    opcUaConnectionForm->addRow("Publish Interval:", opcUaPublishIntervalSpin_);

    opcUaReconnectIntervalSpin_ = new QSpinBox(opcUaConnectionGroup);
    opcUaReconnectIntervalSpin_->setRange(250, 60000);
    opcUaReconnectIntervalSpin_->setSuffix(" ms");
    opcUaReconnectIntervalSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    opcUaConnectionForm->addRow("Reconnect Delay:", opcUaReconnectIntervalSpin_);
    opcUaConnectionTabLayout->addWidget(opcUaConnectionGroup);

    QGroupBox* opcUaAuthGroup = new QGroupBox("Authentication", opcUaConnectionTab);
    opcUaAuthGroup->setStyleSheet(sectionStyle);
    QFormLayout* opcUaAuthForm = createOpcUaForm(opcUaAuthGroup);

    opcUaAuthModeCombo_ = new QComboBox(opcUaAuthGroup);
    opcUaAuthModeCombo_->setStyleSheet(opcUaComboStyle);
    opcUaAuthModeCombo_->addItem("Anonymous", false);
    opcUaAuthModeCombo_->addItem("Username / Password", true);
    opcUaAuthForm->addRow("Mode:", opcUaAuthModeCombo_);

    opcUaUsernameEdit_ = new QLineEdit(opcUaAuthGroup);
    opcUaUsernameEdit_->setStyleSheet(opcUaLineEditStyle);
    opcUaAuthForm->addRow("Username:", opcUaUsernameEdit_);

    opcUaPasswordEdit_ = new QLineEdit(opcUaAuthGroup);
    opcUaPasswordEdit_->setEchoMode(QLineEdit::Password);
    opcUaPasswordEdit_->setStyleSheet(opcUaLineEditStyle);
    opcUaAuthForm->addRow("Password:", opcUaPasswordEdit_);

    connect(opcUaAuthModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        const bool useCredentials = opcUaAuthModeCombo_ && opcUaAuthModeCombo_->currentData().toBool();
        if (opcUaUsernameEdit_) {
            opcUaUsernameEdit_->setEnabled(isAdminMode_ && useCredentials);
        }
        if (opcUaPasswordEdit_) {
            opcUaPasswordEdit_->setEnabled(isAdminMode_ && useCredentials);
        }
    });
    opcUaConnectionTabLayout->addWidget(opcUaAuthGroup);
    opcUaConnectionTabLayout->addStretch(1);
    opcUaTabs->addTab(opcUaConnectionTab, "Connection");

    QWidget* opcUaTriggerTab = new QWidget(opcUaTabs);
    QVBoxLayout* opcUaTriggerTabLayout = new QVBoxLayout(opcUaTriggerTab);
    opcUaTriggerTabLayout->setContentsMargins(10, 10, 10, 10);
    opcUaTriggerTabLayout->setSpacing(10);

    QGroupBox* opcUaTriggerGroup = new QGroupBox("Trigger Tags", opcUaTriggerTab);
    opcUaTriggerGroup->setStyleSheet(sectionStyle);
    QVBoxLayout* opcUaTriggerLayout = new QVBoxLayout(opcUaTriggerGroup);
    opcUaTriggerLayout->setContentsMargins(14, 18, 14, 14);
    opcUaTriggerLayout->setSpacing(8);

    QLabel* opcUaTriggerNote = new QLabel(
        "Each enabled tag fires a recording while it is held active (push-hold), or for Live tags when the server value reads True. Set the sensor Position (mm) to align every camera's window to when the defect passes it (uses the machine speed).",
        opcUaTriggerGroup);
    opcUaTriggerNote->setWordWrap(true);
    opcUaTriggerNote->setStyleSheet(QString("color: %1;").arg(tc.text));
    opcUaTriggerLayout->addWidget(opcUaTriggerNote);

    QGridLayout* opcUaTriggerGrid = new QGridLayout();
    opcUaTriggerGrid->setHorizontalSpacing(8);
    opcUaTriggerGrid->setVerticalSpacing(6);
    opcUaTriggerGrid->addWidget(new QLabel("On", opcUaTriggerGroup), 0, 0);
    opcUaTriggerGrid->addWidget(new QLabel("Name", opcUaTriggerGroup), 0, 1);
    opcUaTriggerGrid->addWidget(new QLabel("NodeId", opcUaTriggerGroup), 0, 2);
    QLabel* opcUaSimHeader = new QLabel("Sim", opcUaTriggerGroup);
    opcUaSimHeader->setToolTip("Simulated: the trigger fires only from the push-hold button (no OPC UA server subscription).");
    opcUaTriggerGrid->addWidget(opcUaSimHeader, 0, 3);
    QLabel* opcUaGroupHeader = new QLabel("Group", opcUaTriggerGroup);
    opcUaGroupHeader->setToolTip("Camera group this trigger records: All, or one of the machine sections (Press-Part, Pre-Dryer, After-Dryer, Calender-Reel). Only that group's cameras are recorded.");
    opcUaTriggerGrid->addWidget(opcUaGroupHeader, 0, 4);
    QLabel* opcUaPositionHeader = new QLabel("Position", opcUaTriggerGroup);
    opcUaPositionHeader->setToolTip("Machine position (mm) of the trigger sensor. When set (> 0), the recorder spatially aligns every camera: each camera's saved window is centered on when the defect passes it, using the machine speed. 0 = record the same wall-clock window for all cameras.");
    opcUaTriggerGrid->addWidget(opcUaPositionHeader, 0, 5);
    QLabel* opcUaHoldHeader = new QLabel("Push-Hold", opcUaTriggerGroup);
    opcUaHoldHeader->setToolTip("Press and hold to fire this trigger repeatedly (every Repeat ms). Release to stop.");
    opcUaTriggerGrid->addWidget(opcUaHoldHeader, 0, 6);
    QLabel* opcUaRepeatHeader = new QLabel("Repeat", opcUaTriggerGroup);
    opcUaRepeatHeader->setToolTip("Push-hold repeat interval: while held, a recording fires every N ms (0 = as fast as possible).");
    opcUaTriggerGrid->addWidget(opcUaRepeatHeader, 0, 7);

    for (int i = 0; i < kOpcUaTriggerSlots; ++i) {
        OpcUaTriggerRowWidgets& row = opcUaTriggerRows_[static_cast<size_t>(i)];
        row.enabledCheck = new QCheckBox(opcUaTriggerGroup);
        row.enabledCheck->setStyleSheet(QString("color: %1;").arg(tc.text));
        opcUaTriggerGrid->addWidget(row.enabledCheck, i + 1, 0, Qt::AlignCenter);

        row.nameEdit = new QLineEdit(opcUaTriggerGroup);
        row.nameEdit->setPlaceholderText(QString("Trigger %1").arg(i + 1));
        row.nameEdit->setStyleSheet(opcUaLineEditStyle);
        opcUaTriggerGrid->addWidget(row.nameEdit, i + 1, 1);

        row.nodeIdEdit = new QLineEdit(opcUaTriggerGroup);
        row.nodeIdEdit->setPlaceholderText("ns=2;s=Line.Trigger");
        row.nodeIdEdit->setStyleSheet(opcUaLineEditStyle);
        opcUaTriggerGrid->addWidget(row.nodeIdEdit, i + 1, 2);

        row.simulatedCombo = new QComboBox(opcUaTriggerGroup);
        row.simulatedCombo->setStyleSheet(opcUaGridComboStyle);
        row.simulatedCombo->addItem("Live", false);
        row.simulatedCombo->addItem("Simulated", true);
        row.simulatedCombo->setToolTip("Simulated: fires only from the push-hold button (no OPC UA server subscription). Live: fires when the tag reads True, plus the button as a manual override.");
        opcUaTriggerGrid->addWidget(row.simulatedCombo, i + 1, 3);

        row.groupCombo = new QComboBox(opcUaTriggerGroup);
        row.groupCombo->setStyleSheet(opcUaGridComboStyle);
        row.groupCombo->addItem("All", CameraGroup::kUnassigned);
        row.groupCombo->addItem(CameraGroup::name(CameraGroup::kWire), CameraGroup::kWire);
        row.groupCombo->addItem(CameraGroup::name(CameraGroup::kPressPart), CameraGroup::kPressPart);
        row.groupCombo->addItem(CameraGroup::name(CameraGroup::kPreDryer), CameraGroup::kPreDryer);
        row.groupCombo->addItem(CameraGroup::name(CameraGroup::kAfterDryer), CameraGroup::kAfterDryer);
        row.groupCombo->addItem(CameraGroup::name(CameraGroup::kCalenderReel), CameraGroup::kCalenderReel);
        row.groupCombo->setToolTip("Camera group this trigger records. 'All' records every active camera; a specific group records only cameras assigned to it (assign cameras on their Camera Card, see Machine Groups).");
        opcUaTriggerGrid->addWidget(row.groupCombo, i + 1, 4);

        row.positionMmSpin = new QSpinBox(opcUaTriggerGroup);
        row.positionMmSpin->setRange(0, 500000);
        row.positionMmSpin->setSuffix(" mm");
        row.positionMmSpin->setStyleSheet(globalFpsSpin_->styleSheet());
        row.positionMmSpin->setToolTip("Machine position (mm) of the trigger sensor. When set (> 0), every camera's recording window is centered on when the defect passes it (uses the machine speed). 0 = no spatial alignment.");
        opcUaTriggerGrid->addWidget(row.positionMmSpin, i + 1, 5);

        row.manualTriggerBtn = new QPushButton("Hold", opcUaTriggerGroup);
        row.manualTriggerBtn->setStyleSheet(opcUaTriggerBtnStyle);
        row.manualTriggerBtn->setCursor(Qt::PointingHandCursor);
        row.manualTriggerBtn->setToolTip("Push-hold: press and hold to fire this trigger repeatedly (every Repeat ms). Release to stop.");
        connect(row.manualTriggerBtn, &QPushButton::pressed, this, [this, i]() {
            // Snapshot the row's live config so the manual trigger works even
            // before the OPC UA settings are saved.
            const OpcUaTriggerRowWidgets& r = opcUaTriggerRows_[static_cast<size_t>(i)];
            OpcUaTriggerTagSettings tag;
            tag.name = r.nameEdit ? r.nameEdit->text().trimmed() : QString();
            tag.nodeId = r.nodeIdEdit ? r.nodeIdEdit->text().trimmed() : QString();
            tag.enabled = r.enabledCheck && r.enabledCheck->isChecked();
            tag.simulated = r.simulatedCombo && r.simulatedCombo->currentData().toBool();
            tag.group = r.groupCombo ? r.groupCombo->currentData().toInt() : CameraGroup::kUnassigned;
            tag.positionMm = r.positionMmSpin ? r.positionMmSpin->value() : 0;
            tag.minimumIntervalMs = r.minimumIntervalSpin ? r.minimumIntervalSpin->value() : 0;
            if (tag.name.isEmpty()) {
                tag.name = QStringLiteral("Trigger %1").arg(i + 1);
            }
            emit opcUaManualTriggerRequested(i, true, tag);
        });
        connect(row.manualTriggerBtn, &QPushButton::released, this, [this, i]() {
            emit opcUaManualTriggerRequested(i, false, OpcUaTriggerTagSettings{});
        });
        opcUaTriggerGrid->addWidget(row.manualTriggerBtn, i + 1, 6, Qt::AlignCenter);

        row.minimumIntervalSpin = new QSpinBox(opcUaTriggerGroup);
        row.minimumIntervalSpin->setRange(0, 60000);
        row.minimumIntervalSpin->setSuffix(" ms");
        row.minimumIntervalSpin->setStyleSheet(globalFpsSpin_->styleSheet());
        row.minimumIntervalSpin->setToolTip("Push-hold repeat interval: while held, fires every N ms (0 = as fast as possible).");
        opcUaTriggerGrid->addWidget(row.minimumIntervalSpin, i + 1, 7);
    }
    opcUaTriggerLayout->addLayout(opcUaTriggerGrid);
    opcUaTriggerTabLayout->addWidget(opcUaTriggerGroup);
    opcUaTriggerTabLayout->addStretch(1);
    opcUaTabs->addTab(opcUaTriggerTab, "Triggers");

    QWidget* opcUaSpeedTab = new QWidget(opcUaTabs);
    QVBoxLayout* opcUaSpeedTabLayout = new QVBoxLayout(opcUaSpeedTab);
    opcUaSpeedTabLayout->setContentsMargins(10, 10, 10, 10);
    opcUaSpeedTabLayout->setSpacing(10);

    QGroupBox* opcUaSpeedGroup = new QGroupBox("Machine Speed Tags", opcUaSpeedTab);
    opcUaSpeedGroup->setStyleSheet(sectionStyle);
    QVBoxLayout* opcUaSpeedGroupLayout = new QVBoxLayout(opcUaSpeedGroup);
    opcUaSpeedGroupLayout->setContentsMargins(14, 18, 14, 14);
    opcUaSpeedGroupLayout->setSpacing(8);

    QLabel* opcUaSpeedNote = new QLabel(
        "Each row subscribes to one drive's speed tag (m/min) at its machine position (mm). "
        "The app interpolates the paper's local speed between anchors, so the draw between "
        "drive groups is reflected when defects are projected onto the machine layout. "
        "A single anchor behaves like the legacy one-speed setup.",
        opcUaSpeedGroup);
    opcUaSpeedNote->setWordWrap(true);
    opcUaSpeedNote->setStyleSheet(QString("color: %1;").arg(tc.text));
    opcUaSpeedGroupLayout->addWidget(opcUaSpeedNote);

    const int sc = 0;
    QGridLayout* opcUaSpeedGrid = new QGridLayout();
    opcUaSpeedGrid->setHorizontalSpacing(8);
    opcUaSpeedGrid->setVerticalSpacing(6);
    opcUaSpeedGrid->addWidget(new QLabel("On", opcUaSpeedGroup), 0, sc);
    opcUaSpeedGrid->addWidget(new QLabel("Name", opcUaSpeedGroup), 0, sc + 1);
    opcUaSpeedGrid->addWidget(new QLabel("NodeId", opcUaSpeedGroup), 0, sc + 2);
    QLabel* opcUaSpeedSimHeader = new QLabel("Sim", opcUaSpeedGroup);
    opcUaSpeedSimHeader->setToolTip("Simulated: reports a fixed value without subscribing to the OPC UA server.");
    opcUaSpeedGrid->addWidget(opcUaSpeedSimHeader, 0, sc + 3);
    QLabel* opcUaSpeedSimValueHeader = new QLabel("Sim Value", opcUaSpeedGroup);
    opcUaSpeedSimValueHeader->setToolTip("Fixed raw value reported while simulated (before Scale/Offset are applied).");
    opcUaSpeedGrid->addWidget(opcUaSpeedSimValueHeader, 0, sc + 4);
    QLabel* opcUaSpeedPosHeader = new QLabel("Pos (mm)", opcUaSpeedGroup);
    opcUaSpeedPosHeader->setToolTip("Machine position (mm) of the drive this tag reports. The local speed at any camera is interpolated between the anchors.");
    opcUaSpeedGrid->addWidget(opcUaSpeedPosHeader, 0, sc + 5);
    opcUaSpeedGrid->addWidget(new QLabel("Scale", opcUaSpeedGroup), 0, sc + 6);
    opcUaSpeedGrid->addWidget(new QLabel("Offset", opcUaSpeedGroup), 0, sc + 7);
    opcUaSpeedGrid->addWidget(new QLabel("Unit", opcUaSpeedGroup), 0, sc + 8);
    QLabel* opcUaSpeedStaleHeader = new QLabel("Stale (ms)", opcUaSpeedGroup);
    opcUaSpeedStaleHeader->setToolTip("Speed samples older than this timeout are ignored for spatial alignment.");
    opcUaSpeedGrid->addWidget(opcUaSpeedStaleHeader, 0, sc + 9);

    for (int i = 0; i < kOpcUaSpeedSlots; ++i) {
        OpcUaSpeedRowWidgets& row = opcUaSpeedRows_[static_cast<size_t>(i)];
        row.enabledCheck = new QCheckBox(opcUaSpeedGroup);
        row.enabledCheck->setStyleSheet(QString("color: %1;").arg(tc.text));
        opcUaSpeedGrid->addWidget(row.enabledCheck, i + 1, sc, Qt::AlignCenter);

        row.nameEdit = new QLineEdit(opcUaSpeedGroup);
        row.nameEdit->setPlaceholderText(QString("Speed %1").arg(i + 1));
        row.nameEdit->setStyleSheet(opcUaLineEditStyle);
        opcUaSpeedGrid->addWidget(row.nameEdit, i + 1, sc + 1);

        row.nodeIdEdit = new QLineEdit(opcUaSpeedGroup);
        row.nodeIdEdit->setPlaceholderText("ns=2;s=Line.Speed");
        row.nodeIdEdit->setStyleSheet(opcUaLineEditStyle);
        opcUaSpeedGrid->addWidget(row.nodeIdEdit, i + 1, sc + 2);

        row.simulatedCombo = new QComboBox(opcUaSpeedGroup);
        row.simulatedCombo->setStyleSheet(opcUaGridComboStyle);
        row.simulatedCombo->addItem("Live", false);
        row.simulatedCombo->addItem("Sim", true);
        row.simulatedCombo->setToolTip("Simulated: reports a fixed value without subscribing to the OPC UA server.");
        opcUaSpeedGrid->addWidget(row.simulatedCombo, i + 1, sc + 3);

        row.simulatedValueSpin = new QDoubleSpinBox(opcUaSpeedGroup);
        row.simulatedValueSpin->setDecimals(4);
        row.simulatedValueSpin->setRange(-100000.0, 100000.0);
        row.simulatedValueSpin->setSingleStep(1.0);
        row.simulatedValueSpin->setStyleSheet(globalFpsSpin_->styleSheet());
        row.simulatedValueSpin->setEnabled(false);
        row.simulatedValueSpin->setToolTip("Fixed raw value reported while simulated (before Scale/Offset are applied).");
        opcUaSpeedGrid->addWidget(row.simulatedValueSpin, i + 1, sc + 4);

        row.positionMmSpin = new QSpinBox(opcUaSpeedGroup);
        row.positionMmSpin->setRange(0, 500000);
        row.positionMmSpin->setSuffix(" mm");
        row.positionMmSpin->setStyleSheet(globalFpsSpin_->styleSheet());
        row.positionMmSpin->setToolTip("Machine position (mm) of the drive this tag reports. 0 = position unknown (treated as a global speed).");
        opcUaSpeedGrid->addWidget(row.positionMmSpin, i + 1, sc + 5);

        row.scaleSpin = new QDoubleSpinBox(opcUaSpeedGroup);
        row.scaleSpin->setDecimals(4);
        row.scaleSpin->setRange(-100000.0, 100000.0);
        row.scaleSpin->setSingleStep(0.1);
        row.scaleSpin->setStyleSheet(globalFpsSpin_->styleSheet());
        opcUaSpeedGrid->addWidget(row.scaleSpin, i + 1, sc + 6);

        row.offsetSpin = new QDoubleSpinBox(opcUaSpeedGroup);
        row.offsetSpin->setDecimals(4);
        row.offsetSpin->setRange(-100000.0, 100000.0);
        row.offsetSpin->setSingleStep(0.1);
        row.offsetSpin->setStyleSheet(globalFpsSpin_->styleSheet());
        opcUaSpeedGrid->addWidget(row.offsetSpin, i + 1, sc + 7);

        row.unitEdit = new QLineEdit(opcUaSpeedGroup);
        row.unitEdit->setPlaceholderText("m/min");
        row.unitEdit->setStyleSheet(opcUaLineEditStyle);
        opcUaSpeedGrid->addWidget(row.unitEdit, i + 1, sc + 8);

        row.staleTimeoutSpin = new QSpinBox(opcUaSpeedGroup);
        row.staleTimeoutSpin->setRange(100, 60000);
        row.staleTimeoutSpin->setSuffix(" ms");
        row.staleTimeoutSpin->setStyleSheet(globalFpsSpin_->styleSheet());
        opcUaSpeedGrid->addWidget(row.staleTimeoutSpin, i + 1, sc + 9);

        connect(row.simulatedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i]() {
            OpcUaSpeedRowWidgets& r = opcUaSpeedRows_[static_cast<size_t>(i)];
            if (r.simulatedValueSpin) {
                r.simulatedValueSpin->setEnabled(r.simulatedCombo
                    && r.simulatedCombo->currentData().toBool());
            }
        });
        // Keep the Live Status speed row in sync with the dialog's own simulated
        // speed config (works even before saving / without the service running).
        connect(row.enabledCheck, &QCheckBox::toggled, this,
                [this]() { refreshOpcUaSpeedDisplay(); });
        connect(row.simulatedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this]() { refreshOpcUaSpeedDisplay(); });
        connect(row.simulatedValueSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this]() { refreshOpcUaSpeedDisplay(); });
        connect(row.scaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this]() { refreshOpcUaSpeedDisplay(); });
        connect(row.offsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this]() { refreshOpcUaSpeedDisplay(); });
        connect(row.unitEdit, &QLineEdit::textChanged, this,
                [this]() { refreshOpcUaSpeedDisplay(); });
    }
    opcUaSpeedGroupLayout->addLayout(opcUaSpeedGrid);

    // Paper direction relative to the mm axis (which way the web travels past
    // the cameras). Global for every speed anchor.
    QWidget* opcUaSpeedDirRow = new QWidget(opcUaSpeedGroup);
    QFormLayout* opcUaSpeedDirForm = new QFormLayout(opcUaSpeedDirRow);
    opcUaSpeedDirForm->setContentsMargins(0, 8, 0, 0);
    opcUaSpeedDirForm->setHorizontalSpacing(kSectionSpacing);
    opcUaSpeedDirForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    opcUaPositionDirectionCombo_ = new QComboBox(opcUaSpeedDirRow);
    opcUaPositionDirectionCombo_->setStyleSheet(opcUaComboStyle);
    opcUaPositionDirectionCombo_->addItem("Increase position with time", 1);
    opcUaPositionDirectionCombo_->addItem("Decrease position with time", -1);
    opcUaSpeedDirForm->addRow("Position Direction:", opcUaPositionDirectionCombo_);
    opcUaSpeedGroupLayout->addWidget(opcUaSpeedDirRow);

    opcUaSpeedTabLayout->addWidget(opcUaSpeedGroup);
    opcUaSpeedTabLayout->addStretch(1);
    opcUaTabs->addTab(opcUaSpeedTab, "Speed");

    // Live Status: client state, speed, and per-tag trigger state, updated from
    // OpcUaClientService::runtimeStatusChanged while the dialog is open.
    QGroupBox* opcUaStatusGroup = new QGroupBox("Live Status", opcUaGroup);
    opcUaStatusGroup->setStyleSheet(sectionStyle);
    QVBoxLayout* opcUaStatusLayout = new QVBoxLayout(opcUaStatusGroup);
    opcUaStatusLayout->setContentsMargins(14, 18, 14, 14);
    opcUaStatusLayout->setSpacing(8);

    QFormLayout* opcUaStatusForm = new QFormLayout();
    opcUaStatusForm->setSpacing(6);
    opcUaStatusForm->setHorizontalSpacing(16);
    opcUaStatusForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    QLabel* opcUaStatusClientCaption = new QLabel("Client:", opcUaStatusGroup);
    opcUaStatusClientCaption->setStyleSheet(QString("color: %1; font-size: 12px;").arg(tc.text));
    opcUaStatusClientLabel_ = new QLabel("○ Idle (OPC UA disabled)", opcUaStatusGroup);
    opcUaStatusClientLabel_->setStyleSheet("color: #8B949E; font-size: 12px; font-weight: 600;");
    opcUaStatusForm->addRow(opcUaStatusClientCaption, opcUaStatusClientLabel_);

    QLabel* opcUaStatusSpeedCaption = new QLabel("Speed:", opcUaStatusGroup);
    opcUaStatusSpeedCaption->setStyleSheet(QString("color: %1; font-size: 12px;").arg(tc.text));
    opcUaStatusSpeedLabel_ = new QLabel("—", opcUaStatusGroup);
    opcUaStatusSpeedLabel_->setStyleSheet("color: #8B949E; font-size: 12px;");
    opcUaStatusForm->addRow(opcUaStatusSpeedCaption, opcUaStatusSpeedLabel_);

    opcUaStatusLayout->addLayout(opcUaStatusForm);

    opcUaStatusTable_ = new QTableWidget(kOpcUaTriggerSlots, 5, opcUaStatusGroup);
    opcUaStatusTable_->setHorizontalHeaderLabels(QStringList()
        << "Name" << "Type" << "Value" << "State" << "Last Fired");
    opcUaStatusTable_->verticalHeader()->setVisible(false);
    opcUaStatusTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    opcUaStatusTable_->setSelectionMode(QAbstractItemView::NoSelection);
    opcUaStatusTable_->setFocusPolicy(Qt::NoFocus);
    opcUaStatusTable_->setShowGrid(false);
    opcUaStatusTable_->setAlternatingRowColors(false);
    opcUaStatusTable_->setStyleSheet(QString(
        "QTableWidget { background: transparent; border: none; color: %1; font-size: 12px; } "
        "QTableWidget::item { padding: 4px 8px; border: none; } "
        "QHeaderView::section { background: transparent; color: %2; border: none; border-bottom: 1px solid %3; padding: 4px 8px; font-weight: 600; font-size: 11px; }"
    ).arg(tc.text, tc.primary, tc.border));
    QHeaderView* opcUaStatusHeader = opcUaStatusTable_->horizontalHeader();
    opcUaStatusHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    opcUaStatusHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    opcUaStatusHeader->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    opcUaStatusHeader->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    opcUaStatusHeader->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    opcUaStatusLayout->addWidget(opcUaStatusTable_);

    opcUaLayout->addWidget(opcUaStatusGroup);

    // Populate the status table with the configured rows right away (the service
    // refreshes it live once wired up).
    updateOpcUaRuntimeStatus(OpcUaRuntimeStatus{});

    QHBoxLayout* opcUaActionsLayout = new QHBoxLayout();
    opcUaActionsLayout->addStretch();
    opcUaSaveBtn_ = new QPushButton("Save OPC UA Settings", opcUaGroup);
    opcUaSaveBtn_->setIcon(IconManager::instance().save(16));
    stylePrimaryActionButton(opcUaSaveBtn_, tc);
    connect(opcUaSaveBtn_, &QPushButton::clicked, this, &ConfigDialog::saveOpcUaSettings);
    opcUaActionsLayout->addWidget(opcUaSaveBtn_);
    opcUaLayout->addLayout(opcUaActionsLayout);

    opcUaScrollArea->setWidget(opcUaGroup);

    QListWidgetItem* opcUaItem = new QListWidgetItem(IconManager::instance().settings(20), "OPC UA");
    sidebar->addItem(opcUaItem);
    stackedWidget->addWidget(opcUaScrollArea);


    // UI Preferences Tab
    QWidget* uiGroup = new QWidget(this);
    QVBoxLayout* uiPageLayout = new QVBoxLayout(uiGroup);
    uiPageLayout->setContentsMargins(16, 8, 16, 6);
    uiPageLayout->setSpacing(5);

    uiPreferencesScrollArea_ = nullptr;

    // Header section
    QLabel* uiHeaderLabel = new QLabel("UI Preferences", uiGroup);
    uiHeaderLabel->setStyleSheet(QString(
        "color: %1; font-size: 17px; font-weight: 700;"
    ).arg(tc.primary));
    uiPageLayout->addWidget(uiHeaderLabel);

    QLabel* uiDescriptionLabel = new QLabel(
        "Customize theme and screen typography.", uiGroup);
    uiDescriptionLabel->setWordWrap(true);
    uiDescriptionLabel->setStyleSheet(QString(
        "color: %1; font-size: 11px; padding-bottom: 2px;"
    ).arg(tc.text));
    uiPageLayout->addWidget(uiDescriptionLabel);

    // Separator line
    QFrame* uiSeparator = new QFrame(uiGroup);
    uiSeparator->setFrameShape(QFrame::HLine);
    uiSeparator->setFrameShadow(QFrame::Plain);
    uiSeparator->setFixedHeight(1);
    uiSeparator->setStyleSheet(QString(
        "background-color: %1; border: none;"
    ).arg(tc.border));
    uiPageLayout->addWidget(uiSeparator);

    QWidget* uiPanel = new QWidget(uiGroup);
    QGridLayout* uiPanelLayout = new QGridLayout(uiPanel);
    uiPanelLayout->setContentsMargins(0, 0, 0, 0);
    uiPanelLayout->setHorizontalSpacing(8);
    uiPanelLayout->setVerticalSpacing(3);
    uiPanelLayout->setColumnStretch(0, 1);
    uiPanelLayout->setColumnStretch(1, 1);
    uiPanelLayout->setRowStretch(0, 0);
    uiPanelLayout->setRowStretch(1, 1);

    const QString uiSectionStyle = QString(
        "QGroupBox { font-weight: 600; color: %1; border: 1px solid %2; "
        "background-color: rgba(255, 255, 255, 0.02); border-radius: 10px; margin-top: 12px; padding: 12px 10px 7px 10px; font-size: 12px; } "
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 6px; top: 2px; }"
    ).arg(tc.primary, tc.border);

    int uiSectionIndex = 0;
    auto createUiSectionForm = [&](const QString& title) {
        QGroupBox* group = new QGroupBox(title, uiPanel);
        group->setStyleSheet(uiSectionStyle);
        QFormLayout* form = new QFormLayout(group);
        form->setSpacing(6);
        form->setHorizontalSpacing(10);
        form->setContentsMargins(10, 14, 10, 8);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        // With the Data Storage section moved out, each remaining section spans
        // the full width for a balanced layout.
        uiPanelLayout->addWidget(group, uiSectionIndex, 0, 1, 2);
        ++uiSectionIndex;
        return form;
    };

    QFormLayout* themeForm = createUiSectionForm("Appearance");

    QTabWidget* uiDetailTabs = new QTabWidget(uiPanel);
    uiDetailTabs->setDocumentMode(true);
    uiDetailTabs->setStyleSheet(QString(
        "QTabWidget::pane { border: 1px solid %1; border-radius: 10px; top: -1px; background-color: rgba(255, 255, 255, 0.01); padding: 2px; } "
        "QTabBar::tab { background-color: %2; color: %3; border: 1px solid %1; border-bottom: none; padding: 6px 14px; min-width: 110px; border-top-left-radius: 8px; border-top-right-radius: 8px; font-weight: 600; font-size: 12px; } "
        "QTabBar::tab:selected { color: %4; background-color: rgba(255, 255, 255, 0.04); margin-bottom: -1px; } "
        "QTabBar::tab:!selected { margin-top: 3px; color: %5; } "
        "QTabBar::tab:hover { color: %4; }"
    ).arg(tc.border, tc.btnBg, tc.text, tc.primary, tc.text));
    uiPanelLayout->addWidget(uiDetailTabs, 1, 0, 1, 2);

    QWidget* liveViewTab = new QWidget(uiDetailTabs);
    QVBoxLayout* liveViewTabLayout = new QVBoxLayout(liveViewTab);
    liveViewTabLayout->setContentsMargins(10, 10, 10, 10);
    liveViewTabLayout->setSpacing(10);

    QWidget* liveViewGroup = new QWidget(liveViewTab);
    QVBoxLayout* liveViewGroupLayout = new QVBoxLayout(liveViewGroup);
    liveViewGroupLayout->setContentsMargins(0, 0, 0, 0);
    liveViewGroupLayout->setSpacing(10);
    liveViewTabLayout->addWidget(liveViewGroup, 0);
    liveViewTabLayout->addStretch(1);

    QLabel* liveViewSectionTitle = new QLabel("Live View", liveViewGroup);
    liveViewSectionTitle->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: 700; padding-bottom: 2px;").arg(tc.primary));
    liveViewGroupLayout->addWidget(liveViewSectionTitle);

    QWidget* liveViewContent = new QWidget(liveViewGroup);
    liveViewContentLayout_ = new QHBoxLayout(liveViewContent);
    liveViewContentLayout_->setContentsMargins(0, 0, 0, 0);
    liveViewContentLayout_->setSpacing(16);
    liveViewContentLayout_->setAlignment(Qt::AlignTop);
    liveViewGroupLayout->addWidget(liveViewContent);

    QWidget* liveViewSettingsPanel = new QWidget(liveViewContent);
    QVBoxLayout* liveViewSettingsLayout = new QVBoxLayout(liveViewSettingsPanel);
    liveViewSettingsLayout->setContentsMargins(0, 0, 0, 0);
    liveViewSettingsLayout->setSpacing(12);
    liveViewSettingsPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    liveViewContentLayout_->addWidget(liveViewSettingsPanel, 5, Qt::AlignTop);

    const QString settingsCardStyle = QString(
        "QGroupBox { font-weight: 600; color: %1; border: 1px solid %2; border-radius: 10px; margin-top: 8px; "
        "background-color: rgba(255, 255, 255, 0.02); padding: 10px; font-size: 12px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
    ).arg(tc.primary, tc.border);

    QGroupBox* liveSettingsCard = new QGroupBox("Live View Appearance", liveViewSettingsPanel);
    liveSettingsCard->setStyleSheet(settingsCardStyle);
    QVBoxLayout* liveSettingsCardLayout = new QVBoxLayout(liveSettingsCard);
    liveSettingsCardLayout->setContentsMargins(12, 16, 12, 12);
    liveSettingsCardLayout->setSpacing(10);
    liveViewSettingsLayout->addWidget(liveSettingsCard);

    const int kRowSpacing = 10;

    QFormLayout* liveViewForm = new QFormLayout();
    liveViewForm->setSpacing(kRowSpacing);
    liveViewForm->setHorizontalSpacing(16);
    liveViewForm->setContentsMargins(0, 0, 0, 0);
    liveViewForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    liveViewForm->setFormAlignment(Qt::AlignTop);

    auto createLiveViewRowLabel = [&](const QString& title, const QString& detail) {
        const QString formattedTooltip = settingsTooltip(detail);
        QWidget* labelWidget = new QWidget(liveViewGroup);
        QVBoxLayout* labelLayout = new QVBoxLayout(labelWidget);
        labelLayout->setContentsMargins(0, 1, 0, 0);
        labelLayout->setSpacing(0);

        QLabel* titleLabel = new QLabel(title, labelWidget);
        titleLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: 600;").arg(tc.text));
        titleLabel->setToolTip(formattedTooltip);
        labelLayout->addWidget(titleLabel);
        labelWidget->setToolTip(formattedTooltip);

        return labelWidget;
    };

    // Theme selection dropdown
    themeGridWidget_ = nullptr;
    themeGridLayout_ = nullptr;
    themeButtonGroup_ = nullptr;

    QComboBox* themeCombo = new QComboBox(uiGroup);
    themeCombo_ = themeCombo;
    themeCombo->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 8px 12px; color: %3; min-width: 220px; font-size: 12px; font-weight: 600; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 8px; } "
        "QComboBox QAbstractItemView { background-color: %1; border: 1px solid %2; border-radius: 6px; color: %3; selection-background-color: %5; padding: 4px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary, tc.bg));
    QLabel* themeLabel = new QLabel("Color Theme:", uiGroup);
    themeLabel->setToolTip("Select a color theme for the application.");

    const struct { const char* name; int index; } themeEntries[] = {
        {"Industrial Dark - Cyan", 0},
        {"Classic Dark - Blue", 1},
        {"High Contrast - Orange", 2},
        {"Warning State - Yellow", 3},
        {"Precision - Green", 4},
        {"Visionary - Purple", 5},
        {"Alert - Deep Red", 6},
        {"Contrast Mono - B&W", 7}
    };

    for (int i = 0; i < 8; ++i) {
        const auto& entry = themeEntries[i];
        const ThemeColors entryColors = CameraConfig::getThemeColors(entry.index);

        // Create a color swatch icon
        QPixmap swatch(20, 20);
        swatch.fill(Qt::transparent);
        QPainter painter(&swatch);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(entryColors.bg));
        painter.setPen(QPen(QColor(entryColors.border), 1));
        painter.drawRoundedRect(1, 1, 18, 18, 4, 4);
        // Draw accent bar
        painter.setBrush(QColor(entryColors.primary));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(4, 8, 12, 4, 2, 2);
        painter.end();

        themeCombo->addItem(QIcon(swatch), entry.name, entry.index);
    }

    selectedThemeIndex_ = CameraConfig::getThemePreset();
    themeCombo->setCurrentIndex(selectedThemeIndex_);

    // Store null in themeCards_ since we no longer use card buttons
    for (int i = 0; i < 8; ++i) {
        themeCards_[i] = nullptr;
    }

    connect(themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, themeCombo](int index) {
        this->selectedThemeIndex_ = themeCombo->currentData().toInt();
        // Update combo text color to match selected theme
        const ThemeColors selectedColors = CameraConfig::getThemeColors(this->selectedThemeIndex_);
        themeCombo->setStyleSheet(QString(
            "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 8px 12px; color: %3; min-width: 220px; font-size: 12px; font-weight: 600; } "
            "QComboBox:hover { border-color: %4; } "
            "QComboBox:focus { border-color: %4; } "
            "QComboBox::drop-down { border: none; padding-right: 8px; } "
            "QComboBox QAbstractItemView { background-color: %1; border: 1px solid %2; border-radius: 6px; color: %5; selection-background-color: %6; padding: 4px; }"
        ).arg(selectedColors.btnBg, selectedColors.border, selectedColors.primary, selectedColors.primary, selectedColors.text, selectedColors.bg));
        emit this->themeSelectionChanged();
    });

    // Apply initial theme color to combo
    {
        const ThemeColors initColors = CameraConfig::getThemeColors(selectedThemeIndex_);
        themeCombo->setStyleSheet(QString(
            "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 8px 12px; color: %3; min-width: 220px; font-size: 12px; font-weight: 600; } "
            "QComboBox:hover { border-color: %4; } "
            "QComboBox:focus { border-color: %4; } "
            "QComboBox::drop-down { border: none; padding-right: 8px; } "
            "QComboBox QAbstractItemView { background-color: %1; border: 1px solid %2; border-radius: 6px; color: %5; selection-background-color: %6; padding: 4px; }"
        ).arg(initColors.btnBg, initColors.border, initColors.primary, initColors.primary, initColors.text, initColors.bg));
    }

    themeForm->addRow(themeLabel, themeCombo);

    QLabel* liveViewDescription = new QLabel("Tune card surface, title, and section typography.", uiGroup);
    liveViewDescription->setWordWrap(true);
    liveViewDescription->setStyleSheet(QString("color: %1; font-size: 11px; padding-bottom: 2px;").arg(tc.text));
    liveSettingsCardLayout->addWidget(liveViewDescription);

    liveViewBackgroundStyleCombo_ = new QComboBox(uiGroup);
    liveViewBackgroundStyleCombo_->addItem("Black", "black");
    liveViewBackgroundStyleCombo_->addItem("White", "white");
    liveViewBackgroundStyleCombo_->addItem("Textured Mesh", "textured_mesh");
    liveViewBackgroundStyleCombo_->addItem("White Textured Mesh", "white_textured_mesh");
    liveViewBackgroundStyleCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    liveViewBackgroundStyleCombo_->setFixedWidth(160);
    liveViewForm->addRow(
        createLiveViewRowLabel("Card Surface", "Background for the card body."),
        liveViewBackgroundStyleCombo_);

    liveViewGridTitleFontCombo_ = new QComboBox(uiGroup);
    populateCuratedFontCombo(liveViewGridTitleFontCombo_);
    liveViewGridTitleFontCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));

    liveViewGridTitleSizeSpin_ = new QSpinBox(uiGroup);
    liveViewGridTitleSizeSpin_->setRange(10, 40);
    liveViewGridTitleSizeSpin_->setSuffix(" px");
    liveViewGridTitleSizeSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    liveViewGridTitleSizeSpin_->setFixedWidth(100);
    liveViewGridTitleSizeSpin_->setFixedWidth(72);

    liveViewForm->addRow(
        createLiveViewRowLabel("Grid Title", "Camera title shown on each grid tile."),
        createTypographyRow(liveViewGroup, liveViewGridTitleFontCombo_, liveViewGridTitleSizeSpin_, "Camera title shown on each grid tile.", 10, 14, 18, liveViewGridTitlePresets_));

    liveViewDetailTitleFontCombo_ = new QComboBox(uiGroup);
    populateCuratedFontCombo(liveViewDetailTitleFontCombo_);
    liveViewDetailTitleFontCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));

    liveViewDetailTitleSizeSpin_ = new QSpinBox(uiGroup);
    liveViewDetailTitleSizeSpin_->setRange(10, 40);
    liveViewDetailTitleSizeSpin_->setSuffix(" px");
    liveViewDetailTitleSizeSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    
    liveViewDetailTitleSizeSpin_->setFixedWidth(72);

    liveViewForm->addRow(
        createLiveViewRowLabel("Detail Title", "Primary camera title in the detail card."),
        createTypographyRow(liveViewGroup, liveViewDetailTitleFontCombo_, liveViewDetailTitleSizeSpin_, "Primary camera title in the detail card.", 10, 14, 18, liveViewDetailTitlePresets_));

    liveViewDetailSectionFontCombo_ = new QComboBox(uiGroup);
    populateCuratedFontCombo(liveViewDetailSectionFontCombo_);
    liveViewDetailSectionFontCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));

    liveViewDetailSectionSizeSpin_ = new QSpinBox(uiGroup);
    liveViewDetailSectionSizeSpin_->setRange(9, 32);
    liveViewDetailSectionSizeSpin_->setSuffix(" px");
    liveViewDetailSectionSizeSpin_->setStyleSheet(globalFpsSpin_->styleSheet());

    liveViewDetailSectionSizeSpin_->setFixedWidth(72);

    liveViewForm->addRow(
        createLiveViewRowLabel("Section Header", "Group titles inside the detail card."),
        createTypographyRow(liveViewGroup, liveViewDetailSectionFontCombo_, liveViewDetailSectionSizeSpin_, "Group titles inside the detail card.", 11, 13, 16, liveViewDetailSectionPresets_));

    liveSettingsCardLayout->addLayout(liveViewForm);
    liveSettingsCardLayout->addSpacing(2);

    QWidget* analysisViewTab = new QWidget(uiDetailTabs);
    QVBoxLayout* analysisViewTabLayout = new QVBoxLayout(analysisViewTab);
    analysisViewTabLayout->setContentsMargins(10, 10, 10, 10);
    analysisViewTabLayout->setSpacing(10);

    QWidget* analysisViewGroup = new QWidget(analysisViewTab);
    QVBoxLayout* analysisViewGroupLayout = new QVBoxLayout(analysisViewGroup);
    analysisViewGroupLayout->setContentsMargins(0, 0, 0, 0);
    analysisViewGroupLayout->setSpacing(10);
    analysisViewTabLayout->addWidget(analysisViewGroup, 0);
    analysisViewTabLayout->addStretch(1);

    QLabel* analysisViewSectionTitle = new QLabel("Analysis View", analysisViewGroup);
    analysisViewSectionTitle->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: 700; padding-bottom: 2px;").arg(tc.primary));
    analysisViewGroupLayout->addWidget(analysisViewSectionTitle);

    QWidget* analysisViewContent = new QWidget(analysisViewGroup);
    analysisViewContentLayout_ = new QHBoxLayout(analysisViewContent);
    analysisViewContentLayout_->setContentsMargins(0, 0, 0, 0);
    analysisViewContentLayout_->setSpacing(16);
    analysisViewContentLayout_->setAlignment(Qt::AlignTop);
    analysisViewGroupLayout->addWidget(analysisViewContent);

    QWidget* analysisSettingsPanel = new QWidget(analysisViewContent);
    QVBoxLayout* analysisSettingsLayout = new QVBoxLayout(analysisSettingsPanel);
    analysisSettingsLayout->setContentsMargins(0, 0, 0, 0);
    analysisSettingsLayout->setSpacing(12);
    analysisViewContentLayout_->addWidget(analysisSettingsPanel, 4, Qt::AlignTop);

    QGroupBox* analysisSettingsCard = new QGroupBox("Analysis View Appearance", analysisSettingsPanel);
    analysisSettingsCard->setStyleSheet(settingsCardStyle);
    QVBoxLayout* analysisSettingsCardLayout = new QVBoxLayout(analysisSettingsCard);
    analysisSettingsCardLayout->setContentsMargins(12, 16, 12, 12);
    analysisSettingsCardLayout->setSpacing(10);
    analysisSettingsLayout->addWidget(analysisSettingsCard);

    QLabel* analysisViewDescription = new QLabel("Tune video titles, timestamps, tabs, and playback surface.", uiGroup);
    analysisViewDescription->setWordWrap(true);
    analysisViewDescription->setStyleSheet(QString("color: %1; font-size: 11px; padding-bottom: 2px;").arg(tc.text));
    analysisSettingsCardLayout->addWidget(analysisViewDescription);

    QFormLayout* analysisViewForm = new QFormLayout();
    analysisViewForm->setSpacing(kRowSpacing);
    analysisViewForm->setHorizontalSpacing(12);
    analysisViewForm->setContentsMargins(0, 0, 0, 0);
    analysisViewForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    analysisViewForm->setFormAlignment(Qt::AlignTop);

    analysisVideoTitleFontCombo_ = new QComboBox(uiGroup);
    populateCuratedFontCombo(analysisVideoTitleFontCombo_);
    analysisVideoTitleFontCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    analysisVideoTitleSizeSpin_ = new QSpinBox(uiGroup);
    analysisVideoTitleSizeSpin_->setRange(8, 24);
    analysisVideoTitleSizeSpin_->setSuffix(" px");
    analysisVideoTitleSizeSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    analysisVideoTitleSizeSpin_->setFixedWidth(72);
    analysisViewForm->addRow(
        createLiveViewRowLabel("Video Title", "Title overlay on analysis video tiles."),
        createTypographyRow(analysisViewGroup, analysisVideoTitleFontCombo_, analysisVideoTitleSizeSpin_, "Title overlay on analysis video tiles.", 8, 10, 14, analysisVideoTitlePresets_));

    analysisTimestampFontCombo_ = new QComboBox(uiGroup);
    populateCuratedFontCombo(analysisTimestampFontCombo_);
    analysisTimestampFontCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    analysisTimestampSizeSpin_ = new QSpinBox(uiGroup);
    analysisTimestampSizeSpin_->setRange(7, 20);
    analysisTimestampSizeSpin_->setSuffix(" px");
    analysisTimestampSizeSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    analysisTimestampSizeSpin_->setFixedWidth(72);
    analysisViewForm->addRow(
        createLiveViewRowLabel("Timestamp", "Timecode shown on the analysis video footer."),
        createTypographyRow(analysisViewGroup, analysisTimestampFontCombo_, analysisTimestampSizeSpin_, "Timecode shown on the analysis video footer.", 7, 8, 12, analysisTimestampPresets_));

    analysisTabFontCombo_ = new QComboBox(uiGroup);
    populateCuratedFontCombo(analysisTabFontCombo_);
    analysisTabFontCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    analysisTabSizeSpin_ = new QSpinBox(uiGroup);
    analysisTabSizeSpin_->setRange(10, 24);
    analysisTabSizeSpin_->setSuffix(" px");
    analysisTabSizeSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    analysisTabSizeSpin_->setFixedWidth(72);
    analysisViewForm->addRow(
        createLiveViewRowLabel("Tab Label", "Tabs and playback label typography."),
        createTypographyRow(analysisViewGroup, analysisTabFontCombo_, analysisTabSizeSpin_, "Tabs and playback label typography.", 10, 12, 16, analysisTabPresets_));

    analysisPlaybackSurfaceCombo_ = new QComboBox(uiGroup);
    analysisPlaybackSurfaceCombo_->addItem("Dark", "dark");
    analysisPlaybackSurfaceCombo_->addItem("Light", "light");
    analysisPlaybackSurfaceCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    analysisPlaybackSurfaceCombo_->setFixedWidth(160);
    analysisViewForm->addRow(
        createLiveViewRowLabel("Playback Surface", "Background used by the analysis playback bar and video blank surface."),
        analysisPlaybackSurfaceCombo_);

    analysisDefaultMetadataCombo_ = new QComboBox(uiGroup);
    analysisDefaultMetadataCombo_->addItem("None", "none");
    analysisDefaultMetadataCombo_->addItem("Standard", "standard");
    analysisDefaultMetadataCombo_->addItem("Timestamp + Frame Counter", "full");
    analysisDefaultMetadataCombo_->addItem("Timestamp Only", "timestamp");
    analysisDefaultMetadataCombo_->addItem("Frame Counter Only", "framecounter");
    analysisDefaultMetadataCombo_->addItem("Real Time Only", "realtime");
    analysisDefaultMetadataCombo_->addItem("Relative Frame Only", "relative");
    analysisDefaultMetadataCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    analysisDefaultMetadataCombo_->setFixedWidth(200);
    analysisViewForm->addRow(
        createLiveViewRowLabel("Default Metadata", "Metadata overlay shown by default when an event is opened in Analysis View."),
        analysisDefaultMetadataCombo_);

    analysisMetadataFontCombo_ = new QComboBox(uiGroup);
    populateCuratedFontCombo(analysisMetadataFontCombo_);
    analysisMetadataFontCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    analysisMetadataSizeSpin_ = new QSpinBox(uiGroup);
    analysisMetadataSizeSpin_->setRange(7, 20);
    analysisMetadataSizeSpin_->setSuffix(" px");
    analysisMetadataSizeSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    analysisMetadataSizeSpin_->setFixedWidth(72);
    analysisViewForm->addRow(
        createLiveViewRowLabel("Metadata Font", "Typography for the metadata side of the analysis frame HUD."),
        createTypographyRow(analysisViewGroup, analysisMetadataFontCombo_, analysisMetadataSizeSpin_,
                            "Typography for the metadata side of the analysis frame HUD.", 7, 8, 12, analysisMetadataPresets_));

    analysisSettingsCardLayout->addLayout(analysisViewForm);

    QHBoxLayout* liveViewActionsLayout = new QHBoxLayout();
    liveViewActionsLayout->setContentsMargins(0, 6, 0, 0);
    liveViewActionsLayout->addStretch();

    const QString secondaryActionButtonStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 6px 14px; font-size: 11px; font-weight: 600; } "
        "QPushButton:hover { border-color: %4; background-color: rgba(255, 255, 255, 0.04); }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.primary);

    liveViewResetBtn_ = new QPushButton("Reset Live View", uiGroup);
    liveViewResetBtn_->setToolTip("Restore all Live View card settings to defaults.");
    liveViewResetBtn_->setStyleSheet(secondaryActionButtonStyle);
    connect(liveViewResetBtn_, &QPushButton::clicked, this, &ConfigDialog::resetLiveViewCardSettings);
    liveViewActionsLayout->addWidget(liveViewResetBtn_);
    liveSettingsCardLayout->addLayout(liveViewActionsLayout);

    QHBoxLayout* analysisActionsLayout = new QHBoxLayout();
    analysisActionsLayout->setContentsMargins(0, 6, 0, 0);
    analysisActionsLayout->addStretch();
    analysisResetBtn_ = new QPushButton("Reset Analysis View", uiGroup);
    analysisResetBtn_->setToolTip("Restore all Analysis View settings to defaults.");
    analysisResetBtn_->setStyleSheet(secondaryActionButtonStyle);
    connect(analysisResetBtn_, &QPushButton::clicked, this, &ConfigDialog::resetAnalysisViewSettings);
    analysisActionsLayout->addWidget(analysisResetBtn_);
    analysisSettingsCardLayout->addLayout(analysisActionsLayout);

    auto refreshTypographyPreviews = [this]() {
        if (!liveViewDetailPreviewWidget_ ||
            !liveViewCardPreviewFrame_ ||
            !liveViewCardPreviewTitleLabel_ ||
            !liveViewCardPreviewMetaLabel_ ||
            !liveViewCardPreviewStatusLabel_) {
            return;
        }

        const int selectedThemePreset = selectedThemeIndex_;
        const ThemeColors previewThemeColors = CameraConfig::getThemeColors(selectedThemePreset);
        const QString backgroundStyle = normalizeLiveViewBackgroundStyle(liveViewBackgroundStyleCombo_->currentData().toString());
        const QBrush surfaceBrush = liveViewBackgroundBrush(backgroundStyle);
        const QColor themeBorderColor(previewThemeColors.border);
        const QColor themeTextColor(previewThemeColors.text);
        const QColor themePrimaryColor(previewThemeColors.primary);
        const QColor themePanelColor(previewThemeColors.btnBg);
        const QColor borderColor = themeBorderColor;
        const QColor titleColor = themeTextColor;
        const QColor subtitleColor = themeTextColor.lighter(145);
        const QColor groupTextColor = themeTextColor;
        const QColor groupBorderColor = themeBorderColor;
        const QColor statusTextColor = QColor("#0B1116");
        const QColor statusBackgroundColor = themePrimaryColor;
        const QColor detailPanelBackground(themePanelColor.red(), themePanelColor.green(), themePanelColor.blue(), 224);

        QFont detailPreviewFont(currentCuratedFontFamily(liveViewDetailTitleFontCombo_));
        detailPreviewFont.setPixelSize(liveViewDetailTitleSizeSpin_->value());
        detailPreviewFont.setBold(true);
        liveViewDetailPreviewWidget_->setPreviewThemeColors(previewThemeColors);
        liveViewDetailPreviewWidget_->setPreviewBackgroundStyle(backgroundStyle);
        liveViewDetailPreviewWidget_->setOverlayFont(detailPreviewFont);
        liveViewDetailPreviewWidget_->setOverlayText("CAM-01: DRYER");
        liveViewDetailPreviewWidget_->setTemperatureStatus(-1.0, TempStatus::Unknown);
        liveViewDetailPreviewWidget_->update();

        QPalette previewPalette = liveViewCardPreviewFrame_->palette();
        previewPalette.setBrush(QPalette::Window, surfaceBrush);
        previewPalette.setColor(QPalette::WindowText, titleColor);
        liveViewCardPreviewFrame_->setAutoFillBackground(true);
        liveViewCardPreviewFrame_->setPalette(previewPalette);
        liveViewCardPreviewFrame_->setStyleSheet(QString(
            "QFrame#liveViewCardPreview { border: 1px solid %1; border-radius: 12px; }"
        ).arg(borderColor.name()));

        updateFontPreviewLabel(
            liveViewCardPreviewTitleLabel_,
            currentCuratedFontFamily(liveViewDetailTitleFontCombo_),
            liveViewDetailTitleSizeSpin_->value(),
            "CAM-01: DRYER"
        );
        liveViewCardPreviewTitleLabel_->setStyleSheet(QString("color: %1; background: transparent;").arg(titleColor.name()));

        liveViewCardPreviewMetaLabel_->setText("Dryer Section | Operator Side | Mono8");
        liveViewCardPreviewMetaLabel_->setStyleSheet(QString("color: %1; background: transparent; font-size: 10px;").arg(subtitleColor.name()));

        updateFontPreviewLabel(
            liveViewCardPreviewStatusLabel_,
            currentCuratedFontFamily(liveViewDetailTitleFontCombo_),
            std::max(11, liveViewDetailTitleSizeSpin_->value() - 3),
            "Connected"
        );
        liveViewCardPreviewStatusLabel_->setStyleSheet(QString(
            "color: %1; background-color: %2; border-radius: 10px; padding: 4px 10px;"
        ).arg(statusTextColor.name(), statusBackgroundColor.name(QColor::HexArgb)));

        QFont sectionPreviewFont(currentCuratedFontFamily(liveViewDetailSectionFontCombo_));
        sectionPreviewFont.setPixelSize(liveViewDetailSectionSizeSpin_->value());
        sectionPreviewFont.setBold(true);
        const QString groupStyle = QString(
            "QGroupBox { color: %1; border: 1px solid %2; border-radius: 8px; margin-top: 10px; padding-top: 10px; background-color: %4; } "
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; } "
            "QLabel { color: %3; background: transparent; }"
        ).arg(groupTextColor.name(), groupBorderColor.name(), subtitleColor.name(), detailPanelBackground.name(QColor::HexArgb));
        if (liveViewCardPreviewInfoGroup_) {
            liveViewCardPreviewInfoGroup_->setFont(sectionPreviewFont);
            liveViewCardPreviewInfoGroup_->setStyleSheet(groupStyle);
        }
        if (liveViewCardPreviewControlGroup_) {
            liveViewCardPreviewControlGroup_->setFont(sectionPreviewFont);
            liveViewCardPreviewControlGroup_->setStyleSheet(groupStyle);
        }

        liveViewCardPreviewFrame_->update();
    };

    connect(liveViewGridTitleFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshTypographyPreviews);
    connect(liveViewGridTitleSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, refreshTypographyPreviews);
    connect(liveViewDetailTitleFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshTypographyPreviews);
    connect(liveViewDetailTitleSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, refreshTypographyPreviews);
    connect(liveViewDetailSectionFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshTypographyPreviews);
    connect(liveViewDetailSectionSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, refreshTypographyPreviews);
    connect(liveViewBackgroundStyleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshTypographyPreviews);
    connect(this, &ConfigDialog::themeSelectionChanged, this, refreshTypographyPreviews);

    auto refreshAnalysisPreview = [this]() {
        if (!analysisPreviewVideoWidget_ || !analysisPreviewFrame_ || !analysisPreviewSectionLabel_ ||
            !analysisPreviewTabLabel_ || !analysisPreviewFrameLabel_) {
            return;
        }

        const int selectedThemePreset = selectedThemeIndex_;
        const ThemeColors previewThemeColors = CameraConfig::getThemeColors(selectedThemePreset);
        AnalysisViewStyle style = CameraConfig::getDefaultAnalysisViewStyle();
        style.videoTitleFontFamily = currentCuratedFontFamily(analysisVideoTitleFontCombo_);
        style.videoTitleFontSize = analysisVideoTitleSizeSpin_->value();
        style.timestampFontFamily = currentCuratedFontFamily(analysisTimestampFontCombo_);
        style.timestampFontSize = analysisTimestampSizeSpin_->value();
        style.tabFontFamily = currentCuratedFontFamily(analysisTabFontCombo_);
        style.tabFontSize = analysisTabSizeSpin_->value();
        style.playbackSurfaceStyle = analysisPlaybackSurfaceCombo_->currentData().toString();
        style.defaultMetadataMode = analysisDefaultMetadataCombo_->currentData().toString();
        style.defaultMetadataFontFamily = currentCuratedFontFamily(analysisMetadataFontCombo_);
        style.defaultMetadataFontSize = analysisMetadataSizeSpin_->value();

        const QColor borderColor(previewThemeColors.border);
        const QColor textColor(previewThemeColors.text);
        const QColor primaryColor(previewThemeColors.primary);
        const QColor panelColor(previewThemeColors.btnBg);
        const QColor playbackSurface = style.playbackSurfaceStyle == "light" ? QColor("#F2F2F2") : QColor("#000000");

        analysisPreviewVideoWidget_->setPreviewThemeColors(previewThemeColors);
        analysisPreviewVideoWidget_->setPreviewStyle(style);
        analysisPreviewVideoWidget_->setTimestamp("00:00:12.4");

        QPalette analysisPalette = analysisPreviewFrame_->palette();
        analysisPalette.setColor(QPalette::Window, panelColor);
        analysisPreviewFrame_->setAutoFillBackground(true);
        analysisPreviewFrame_->setPalette(analysisPalette);
        analysisPreviewFrame_->setStyleSheet(QString(
            "QFrame#analysisPreviewFrame { border: 1px solid %1; border-radius: 12px; }"
        ).arg(borderColor.name()));

        QFont sectionFont(style.tabFontFamily);
        sectionFont.setPixelSize(std::max(11, style.tabFontSize));
        sectionFont.setBold(true);
        analysisPreviewSectionLabel_->setFont(sectionFont);
        analysisPreviewSectionLabel_->setStyleSheet(QString("color: %1; background: transparent;").arg(textColor.name()));

        analysisPreviewTabLabel_->setFont(sectionFont);
        analysisPreviewTabLabel_->setStyleSheet(QString(
            "color: %1; background-color: %2; border: 1px solid %3; border-radius: 8px; padding: 6px 12px;"
        ).arg(textColor.name(), panelColor.name(), primaryColor.name()));

        QFont frameFont(style.timestampFontFamily);
        frameFont.setPixelSize(style.timestampFontSize);
        analysisPreviewFrameLabel_->setFont(frameFont);
        analysisPreviewFrameLabel_->setStyleSheet(QString(
            "color: %1; background-color: %2; border: 1px solid %3; border-radius: 6px; padding: 4px 8px;"
        ).arg(textColor.name(), playbackSurface.name(), borderColor.name()));
    };

    connect(analysisVideoTitleFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshAnalysisPreview);
    connect(analysisVideoTitleSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, refreshAnalysisPreview);
    connect(analysisTimestampFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshAnalysisPreview);
    connect(analysisTimestampSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, refreshAnalysisPreview);
    connect(analysisTabFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshAnalysisPreview);
    connect(analysisTabSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, refreshAnalysisPreview);
    connect(analysisPlaybackSurfaceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshAnalysisPreview);
    connect(analysisDefaultMetadataCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshAnalysisPreview);
    connect(analysisMetadataFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshAnalysisPreview);
    connect(analysisMetadataSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, refreshAnalysisPreview);
    connect(this, &ConfigDialog::themeSelectionChanged, this, refreshAnalysisPreview);

    QFrame* livePreviewContainer = new QFrame(liveViewGroup);
    livePreviewContainer_ = livePreviewContainer;
    livePreviewContainer->setFrameShape(QFrame::NoFrame);
    livePreviewContainer->setObjectName("livePreviewContainer");
    livePreviewContainer->setStyleSheet(QString(
        "QFrame#livePreviewContainer { background-color: %1; border: 1px solid %2; border-radius: 12px; }"
    ).arg(tc.btnBg, tc.border));
    livePreviewContainer->setMinimumWidth(360);
    livePreviewContainer->setMaximumWidth(390);
    livePreviewContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    QVBoxLayout* livePreviewPageLayout = new QVBoxLayout(livePreviewContainer);
    livePreviewPageLayout->setContentsMargins(10, 8, 10, 8);
    livePreviewPageLayout->setSpacing(8);

    QLabel* previewTitle = new QLabel("Detail Card Preview", livePreviewContainer);
    previewTitle->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: 700; background: transparent; border: none;").arg(tc.primary));
    livePreviewPageLayout->addWidget(previewTitle);

    QLabel* previewNote = new QLabel("The preview uses the current detail title and section styling with representative live card data.", livePreviewContainer);
    previewNote->setWordWrap(true);
    previewNote->setStyleSheet(QString("color: %1; font-size: 10px; background: transparent; border: none;").arg(tc.text));
    livePreviewPageLayout->addWidget(previewNote);

    liveViewCardPreviewFrame_ = new QFrame(livePreviewContainer);
    liveViewCardPreviewFrame_->setFrameShape(QFrame::NoFrame);
    liveViewCardPreviewFrame_->setObjectName("liveViewCardPreview");
    liveViewCardPreviewFrame_->setMinimumWidth(0);
    liveViewCardPreviewFrame_->setMaximumWidth(QWIDGETSIZE_MAX);
    liveViewCardPreviewFrame_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    QVBoxLayout* liveViewCardPreviewLayout = new QVBoxLayout(liveViewCardPreviewFrame_);
    liveViewCardPreviewLayout->setContentsMargins(12, 12, 12, 12);
    liveViewCardPreviewLayout->setSpacing(10);

    liveViewDetailPreviewWidget_ = new CameraWidget(liveViewCardPreviewFrame_);
    liveViewDetailPreviewWidget_->setCameraId(0);
    liveViewDetailPreviewWidget_->setFixedHeight(212);
    liveViewDetailPreviewWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    liveViewCardPreviewLayout->addWidget(liveViewDetailPreviewWidget_);

    QHBoxLayout* liveViewCardPreviewHeaderLayout = new QHBoxLayout();
    liveViewCardPreviewHeaderLayout->setContentsMargins(0, 0, 0, 0);
    liveViewCardPreviewHeaderLayout->setSpacing(8);

    QVBoxLayout* liveViewCardPreviewTitleLayout = new QVBoxLayout();
    liveViewCardPreviewTitleLayout->setContentsMargins(0, 0, 0, 0);
    liveViewCardPreviewTitleLayout->setSpacing(2);

    liveViewCardPreviewTitleLabel_ = new QLabel(liveViewCardPreviewFrame_);
    liveViewCardPreviewTitleLabel_->setWordWrap(true);
    liveViewCardPreviewTitleLabel_->setMinimumHeight(18);
    liveViewCardPreviewTitleLayout->addWidget(liveViewCardPreviewTitleLabel_);

    liveViewCardPreviewMetaLabel_ = new QLabel(liveViewCardPreviewFrame_);
    liveViewCardPreviewMetaLabel_->setWordWrap(true);
    liveViewCardPreviewMetaLabel_->setMinimumHeight(14);
    liveViewCardPreviewTitleLayout->addWidget(liveViewCardPreviewMetaLabel_);

    liveViewCardPreviewHeaderLayout->addLayout(liveViewCardPreviewTitleLayout, 1);

    liveViewCardPreviewStatusLabel_ = new QLabel(liveViewCardPreviewFrame_);
    liveViewCardPreviewStatusLabel_->setAlignment(Qt::AlignCenter);
    liveViewCardPreviewStatusLabel_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    liveViewCardPreviewStatusLabel_->setMinimumWidth(96);
    liveViewCardPreviewHeaderLayout->addWidget(liveViewCardPreviewStatusLabel_, 0, Qt::AlignTop);

    liveViewCardPreviewLayout->addLayout(liveViewCardPreviewHeaderLayout);

    liveViewCardPreviewInfoGroup_ = new QGroupBox("Section Header", liveViewCardPreviewFrame_);
    QVBoxLayout* infoGroupLayout = new QVBoxLayout(liveViewCardPreviewInfoGroup_);
    infoGroupLayout->setContentsMargins(8, 12, 8, 8);
    infoGroupLayout->setSpacing(2);
    infoGroupLayout->addWidget(new QLabel("Sample value text", liveViewCardPreviewInfoGroup_));
    infoGroupLayout->addWidget(new QLabel("Secondary line", liveViewCardPreviewInfoGroup_));
    liveViewCardPreviewLayout->addWidget(liveViewCardPreviewInfoGroup_);

    liveViewCardPreviewControlGroup_ = new QGroupBox("Camera Parameters", liveViewCardPreviewFrame_);
    QVBoxLayout* controlGroupLayout = new QVBoxLayout(liveViewCardPreviewControlGroup_);
    controlGroupLayout->setContentsMargins(8, 12, 8, 8);
    controlGroupLayout->setSpacing(2);
    controlGroupLayout->addWidget(new QLabel("Exposure: 40880 us", liveViewCardPreviewControlGroup_));
    controlGroupLayout->addWidget(new QLabel("Pixel Format: Mono8", liveViewCardPreviewControlGroup_));
    liveViewCardPreviewLayout->addWidget(liveViewCardPreviewControlGroup_);

    livePreviewPageLayout->addWidget(liveViewCardPreviewFrame_, 0, Qt::AlignTop);

    liveViewContentLayout_->addWidget(livePreviewContainer, 3, Qt::AlignTop);

    QFrame* analysisPreviewContainer = new QFrame(analysisViewGroup);
    analysisPreviewContainer_ = analysisPreviewContainer;
    analysisPreviewContainer->setFrameShape(QFrame::NoFrame);
    analysisPreviewContainer->setObjectName("analysisPreviewContainer");
    analysisPreviewContainer->setStyleSheet(QString(
        "QFrame#analysisPreviewContainer { background-color: %1; border: 1px solid %2; border-radius: 12px; }"
    ).arg(tc.btnBg, tc.border));
    analysisPreviewContainer->setMinimumWidth(360);
    analysisPreviewContainer->setMaximumWidth(390);
    analysisPreviewContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    QVBoxLayout* analysisPreviewPageLayout = new QVBoxLayout(analysisPreviewContainer);
    analysisPreviewPageLayout->setContentsMargins(10, 8, 10, 8);
    analysisPreviewPageLayout->setSpacing(8);

    QLabel* analysisPreviewTitle = new QLabel("Analysis Preview", analysisPreviewContainer);
    analysisPreviewTitle->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: 700; background: transparent; border: none;").arg(tc.primary));
    analysisPreviewPageLayout->addWidget(analysisPreviewTitle);

    QLabel* analysisPreviewNote = new QLabel("The preview uses the current title and timestamp styling with representative analysis card data.", analysisPreviewContainer);
    analysisPreviewNote->setWordWrap(true);
    analysisPreviewNote->setStyleSheet(QString("color: %1; font-size: 10px; background: transparent; border: none;").arg(tc.text));
    analysisPreviewPageLayout->addWidget(analysisPreviewNote);

    // Two-row layout: header row + video row
    analysisPreviewFrame_ = new QFrame(analysisPreviewContainer);
    analysisPreviewFrame_->setFrameShape(QFrame::NoFrame);
    analysisPreviewFrame_->setObjectName("analysisPreviewFrame");
    analysisPreviewFrame_->setMinimumWidth(0);
    analysisPreviewFrame_->setMaximumWidth(QWIDGETSIZE_MAX);
    analysisPreviewFrame_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    QVBoxLayout* analysisPreviewLayout = new QVBoxLayout(analysisPreviewFrame_);
    analysisPreviewLayout->setContentsMargins(12, 12, 12, 12);
    analysisPreviewLayout->setSpacing(10);

    QHBoxLayout* analysisPreviewHeaderLayout = new QHBoxLayout();
    analysisPreviewHeaderLayout->setContentsMargins(0, 0, 0, 0);
    analysisPreviewHeaderLayout->setSpacing(8);
    analysisPreviewSectionLabel_ = new QLabel("Paper Break Log", analysisPreviewFrame_);
    analysisPreviewHeaderLayout->addWidget(analysisPreviewSectionLabel_, 1);
    analysisPreviewTabLabel_ = new QLabel("All Camera", analysisPreviewFrame_);
    analysisPreviewTabLabel_->setAlignment(Qt::AlignCenter);
    analysisPreviewHeaderLayout->addWidget(analysisPreviewTabLabel_, 0);
    analysisPreviewLayout->addLayout(analysisPreviewHeaderLayout);

    analysisPreviewVideoWidget_ = new AnalysisVideoWidget(0, "CAM-01: DRYER", analysisPreviewFrame_);
    analysisPreviewVideoWidget_->setFixedHeight(152);
    analysisPreviewVideoWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    analysisPreviewLayout->addWidget(analysisPreviewVideoWidget_);

    analysisPreviewFrameLabel_ = new QLabel("Frame: 12.4", analysisPreviewFrame_);
    analysisPreviewLayout->addWidget(analysisPreviewFrameLabel_, 0, Qt::AlignLeft);

    analysisPreviewPageLayout->addWidget(analysisPreviewFrame_);
    analysisViewContentLayout_->addWidget(analysisPreviewContainer, 3, Qt::AlignTop);

    uiDetailTabs->addTab(liveViewTab, "Live View");
    uiDetailTabs->addTab(analysisViewTab, "Analysis View");

    refreshTypographyPreviews();
    refreshAnalysisPreview();

    uiPageLayout->addWidget(uiPanel, 1);

    QFrame* uiActionsSeparator = new QFrame(uiGroup);
    uiActionsSeparator->setFrameShape(QFrame::HLine);
    uiActionsSeparator->setFrameShadow(QFrame::Plain);
    uiActionsSeparator->setFixedHeight(1);
    uiActionsSeparator->setStyleSheet(QString(
        "background-color: %1; border: none;"
    ).arg(tc.border));
    uiPageLayout->addWidget(uiActionsSeparator);

    QVBoxLayout* uiActionsLayout = new QVBoxLayout();
    uiActionsLayout->setSpacing(6);

    uiUnsavedIndicator_ = new QLabel(uiGroup);
    uiUnsavedIndicator_->setStyleSheet(QString(
        "color: #FFB020; font-size: 11px; font-weight: 600;"
    ));
    uiUnsavedIndicator_->setText("");
    uiUnsavedIndicator_->setToolTip("You have unsaved changes in this section.");
    uiActionsLayout->addWidget(uiUnsavedIndicator_, 0, Qt::AlignRight);

    QHBoxLayout* uiActionButtonsLayout = new QHBoxLayout();
    uiActionButtonsLayout->setSpacing(14);
    uiActionButtonsLayout->addStretch();

    uiApplyBtn_ = new QPushButton("Apply Now", uiGroup);
    uiApplyBtn_->setToolTip("Apply changes without closing this section.");
    uiApplyBtn_->setStyleSheet(secondaryActionButtonStyle);
    uiApplyBtn_->setEnabled(false);
    connect(uiApplyBtn_, &QPushButton::clicked, this, &ConfigDialog::applyUiSettings);
    uiActionButtonsLayout->addWidget(uiApplyBtn_);

    uiSaveBtn_ = new QPushButton("Save", uiGroup);
    uiSaveBtn_->setToolTip("Save all UI preferences and apply changes.");
    uiSaveBtn_->setIcon(IconManager::instance().save(16));
    stylePrimaryActionButton(uiSaveBtn_, tc);
    connect(uiSaveBtn_, &QPushButton::clicked, this, &ConfigDialog::saveUiSettings);
    uiActionButtonsLayout->addWidget(uiSaveBtn_);
    uiActionsLayout->addLayout(uiActionButtonsLayout);
    uiPageLayout->addLayout(uiActionsLayout);

    QListWidgetItem* uiGroupItem = new QListWidgetItem(IconManager::instance().settings(20), "UI Preferences");
    sidebar->addItem(uiGroupItem);
    stackedWidget->addWidget(uiGroup);

    // Diagnostics Tab
    QWidget* diagnosticsGroup = new QWidget(this);
    QVBoxLayout* diagnosticsLayout = new QVBoxLayout(diagnosticsGroup);
    diagnosticsLayout->setSpacing(kSectionSpacing);
    diagnosticsLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    QGroupBox* diagLogsGroup = new QGroupBox("Connection Diagnostics", diagnosticsGroup);
    diagLogsGroup->setStyleSheet(QString(
        "QGroupBox { font-weight: 600; color: %1; border: 1px solid %2; "
        "border-radius: 8px; margin-top: 8px; padding-top: 8px; font-size: 12px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
    ).arg(tc.primary, tc.border));
    
    QVBoxLayout* diagLogsLayout = new QVBoxLayout(diagLogsGroup);
    diagLogsLayout->setSpacing(kControlSpacing);
    diagLogsLayout->setContentsMargins(14, 18, 14, 14);
    
    connectionLogsBrowser_ = new QTextEdit(diagLogsGroup);
    connectionLogsBrowser_->setReadOnly(true);
    connectionLogsBrowser_->setStyleSheet(QString(
        "QTextEdit { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 6px; "
        "  color: %3; "
        "  font-family: 'SF Mono', Monaco, Consolas, monospace; "
        "  font-size: 12px; "
        "  padding: 8px; "
        "}"
    ).arg(tc.bg, tc.border, tc.text));
    diagLogsLayout->addWidget(connectionLogsBrowser_);
    
    QHBoxLayout* diagBtnsLayout = new QHBoxLayout();
    diagBtnsLayout->setSpacing(kControlSpacing);
    
    QPushButton* diagRefreshBtn = new QPushButton("Refresh Network", diagLogsGroup);
    diagRefreshBtn->setIcon(IconManager::instance().refresh(16));
    diagRefreshBtn->setStyleSheet(QString(
        "QPushButton { "
        "  background-color: %1; "
        "  color: %2; "
        "  border: none; "
        "  border-radius: 6px; "
        "  padding: 6px 12px; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "} "
        "QPushButton:hover { background-color: %3; }"
    ).arg(tc.primary, tc.bg, tc.btnHover));
    connect(diagRefreshBtn, &QPushButton::clicked, this, &ConfigDialog::onRefreshLogsClicked);
    
    QPushButton* diagClearBtn = new QPushButton("Clear Logs", diagLogsGroup);
    diagClearBtn->setIcon(IconManager::instance().trash(16));
    diagClearBtn->setStyleSheet(QString(
        "QPushButton { "
        "  background-color: transparent; "
        "  color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 6px; "
        "  padding: 6px 12px; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "} "
        "QPushButton:hover { background-color: rgba(255, 90, 90, 0.1); border-color: #FF5A5A; color: #FF5A5A; }"
    ).arg(tc.text, tc.border));
    connect(diagClearBtn, &QPushButton::clicked, this, &ConfigDialog::onClearLogsClicked);
    
    diagBtnsLayout->addWidget(diagRefreshBtn);
    diagBtnsLayout->addWidget(diagClearBtn);
    diagBtnsLayout->addStretch();
    diagLogsLayout->addLayout(diagBtnsLayout);
    
    diagnosticsLayout->addWidget(diagLogsGroup);
    
    QListWidgetItem* diagnosticsItem = new QListWidgetItem(IconManager::instance().info(20), "Diagnostics");
    sidebar->addItem(diagnosticsItem);
    stackedWidget->addWidget(diagnosticsGroup);

    int widestSidebarLabel = 0;
    for (int i = 0; i < sidebar->count(); ++i) {
        const QListWidgetItem* item = sidebar->item(i);
        widestSidebarLabel = std::max(widestSidebarLabel, sidebar->fontMetrics().horizontalAdvance(item->text()));
    }
    sidebar->setFixedWidth(std::max(kSidebarMinWidth, widestSidebarLabel + kSidebarContentPadding));

    mainLayout->addLayout(contentLayout, 1);
    
    connect(sidebar, &QListWidget::currentRowChanged, stackedWidget, &QStackedWidget::setCurrentIndex);
    connect(sidebar, &QListWidget::currentRowChanged, this, [this]() {
        relayoutUiPreferencePanels();
    });
    sidebar->setCurrentRow(0);

    // Initialize logs
    onRefreshLogsClicked();
}

void ConfigDialog::loadSettings() {
    // Load camera configurations and create cards
    // (refreshNetworkStatus() at the end of loadSettings() refreshes the
    // read-only Fixed IP registry.)
    std::vector<CameraInfo> cameras = CameraConfig::getCameras();
    for (const auto& cam : cameras) {
        createCameraWidgetBlock(cam);
    }

    // Load global settings
    globalFpsSpin_->setValue(CameraConfig::getFps());
    preTriggerSpin_->setValue(CameraConfig::getPreTriggerSeconds());
    postTriggerSpin_->setValue(CameraConfig::getPostTriggerSeconds());
    updateRecordingInfoLabel();
    eventRetentionSpin_->setValue(CameraConfig::getEventRetentionCount());
    if (lowDiskThresholdSpin_) lowDiskThresholdSpin_->setValue(CameraConfig::getLowDiskWarningPct());
    if (cameraSourceCombo_) {
        const int sourceIndex = cameraSourceCombo_->findData(
            static_cast<int>(CameraConfig::getCameraSource()));
        if (sourceIndex != -1) {
            cameraSourceCombo_->setCurrentIndex(sourceIndex);
        }
    }
    const OpcUaSettings opcUaSettings = CameraConfig::getOpcUaSettings();
    const OpcUaSettings defaultOpcUaSettings = CameraConfig::getDefaultOpcUaSettings();
    if (opcUaEnabledCheck_) {
        opcUaEnabledCheck_->setChecked(opcUaSettings.enabled);
    }
    if (opcUaEndpointEdit_) {
        opcUaEndpointEdit_->setText(opcUaSettings.endpointUrl);
    }
    if (opcUaAuthModeCombo_) {
        const int authModeIndex = opcUaAuthModeCombo_->findData(opcUaSettings.useUsernamePassword);
        if (authModeIndex != -1) {
            opcUaAuthModeCombo_->setCurrentIndex(authModeIndex);
        }
    }
    if (opcUaUsernameEdit_) {
        opcUaUsernameEdit_->setText(opcUaSettings.username);
    }
    if (opcUaPasswordEdit_) {
        opcUaPasswordEdit_->setText(opcUaSettings.password);
    }
    if (opcUaPublishIntervalSpin_) {
        opcUaPublishIntervalSpin_->setValue(opcUaSettings.publishIntervalMs);
    }
    if (opcUaReconnectIntervalSpin_) {
        opcUaReconnectIntervalSpin_->setValue(opcUaSettings.reconnectIntervalMs);
    }

    for (int i = 0; i < kOpcUaTriggerSlots; ++i) {
        // Saved rows win; anything beyond the saved list falls back to the
        // defaults template (and beyond that to a blank row). The defaults hold
        // only the 9 planned detectors, so guard the fallback index.
        const OpcUaTriggerTagSettings tag = (i < static_cast<int>(opcUaSettings.triggerTags.size()))
            ? opcUaSettings.triggerTags[static_cast<size_t>(i)]
            : (i < static_cast<int>(defaultOpcUaSettings.triggerTags.size())
                ? defaultOpcUaSettings.triggerTags[static_cast<size_t>(i)]
                : OpcUaTriggerTagSettings{});
        OpcUaTriggerRowWidgets& row = opcUaTriggerRows_[static_cast<size_t>(i)];
        if (row.enabledCheck) {
            row.enabledCheck->setChecked(tag.enabled);
        }
        if (row.nameEdit) {
            row.nameEdit->setText(tag.name);
        }
        if (row.nodeIdEdit) {
            row.nodeIdEdit->setText(tag.nodeId);
        }
        if (row.minimumIntervalSpin) {
            row.minimumIntervalSpin->setValue(tag.minimumIntervalMs);
        }
        if (row.simulatedCombo) {
            const int simulatedIndex = row.simulatedCombo->findData(tag.simulated);
            if (simulatedIndex != -1) {
                row.simulatedCombo->setCurrentIndex(simulatedIndex);
            }
        }
        if (row.groupCombo) {
            const int groupIndex = row.groupCombo->findData(tag.group);
            if (groupIndex != -1) {
                row.groupCombo->setCurrentIndex(groupIndex);
            }
        }
        if (row.positionMmSpin) {
            row.positionMmSpin->setValue(tag.positionMm);
        }
    }

    for (int i = 0; i < kOpcUaSpeedSlots; ++i) {
        const OpcUaSpeedTagSettings tag = (i < static_cast<int>(opcUaSettings.speedTags.size()))
            ? opcUaSettings.speedTags[static_cast<size_t>(i)]
            : (i < static_cast<int>(defaultOpcUaSettings.speedTags.size())
                ? defaultOpcUaSettings.speedTags[static_cast<size_t>(i)]
                : OpcUaSpeedTagSettings{});
        OpcUaSpeedRowWidgets& row = opcUaSpeedRows_[static_cast<size_t>(i)];
        if (row.enabledCheck) {
            row.enabledCheck->setChecked(tag.enabled);
        }
        if (row.nameEdit) {
            row.nameEdit->setText(tag.name);
        }
        if (row.nodeIdEdit) {
            row.nodeIdEdit->setText(tag.nodeId);
        }
        if (row.scaleSpin) {
            row.scaleSpin->setValue(tag.scale);
        }
        if (row.offsetSpin) {
            row.offsetSpin->setValue(tag.offset);
        }
        if (row.unitEdit) {
            row.unitEdit->setText(tag.unit);
        }
        if (row.staleTimeoutSpin) {
            row.staleTimeoutSpin->setValue(tag.staleTimeoutMs);
        }
        if (row.positionMmSpin) {
            row.positionMmSpin->setValue(tag.positionMm);
        }
        if (row.simulatedCombo) {
            const int simulatedIndex = row.simulatedCombo->findData(tag.simulated);
            if (simulatedIndex != -1) {
                row.simulatedCombo->setCurrentIndex(simulatedIndex);
            }
        }
        if (row.simulatedValueSpin) {
            row.simulatedValueSpin->setValue(tag.simulatedValue);
            row.simulatedValueSpin->setEnabled(tag.simulated);
        }
    }
    if (opcUaPositionDirectionCombo_) {
        const int directionIndex = opcUaPositionDirectionCombo_->findData(opcUaSettings.positionDirectionSign >= 0 ? 1 : -1);
        if (directionIndex != -1) {
            opcUaPositionDirectionCombo_->setCurrentIndex(directionIndex);
        }
    }
    const bool useOpcUaCredentials = opcUaSettings.useUsernamePassword;
    opcUaDiscoveryAttempted_ = false;
    updateOpcUaDiscoveryStatus(
        opcUaSettings.endpointUrl.trimmed().isEmpty()
            ? QStringLiteral("Checking for discoverable OPC UA servers...")
            : QStringLiteral("Saved endpoint URL loaded. Automatic detection will verify discoverable servers when this page is shown."),
        false
    );
    if (opcUaUsernameEdit_) {
        opcUaUsernameEdit_->setEnabled(isAdminMode_ && useOpcUaCredentials);
    }
    if (opcUaPasswordEdit_) {
        opcUaPasswordEdit_->setEnabled(isAdminMode_ && useOpcUaCredentials);
    }

    eventStoragePathEdit_->setText(CameraConfig::getEventStoragePath());
    refreshStorageStats();
    selectedThemeIndex_ = CameraConfig::getThemePreset();
    const int savedThemeIdx = selectedThemeIndex_;
    if (themeCombo_) {
        themeCombo_->setCurrentIndex(savedThemeIdx);
    }
    for (int i = 0; i < 8; ++i) {
        if (themeCards_[i]) {
            const ThemeColors c = CameraConfig::getThemeColors(i);
            if (i == savedThemeIdx) {
                themeCards_[i]->setStyleSheet(QString(
                    "QPushButton { background-color: rgba(255, 255, 255, 0.06); border: 2px solid %1; border-radius: 10px; padding: 0px; }"
                ).arg(c.primary));
            } else {
                themeCards_[i]->setStyleSheet(QString(
                    "QPushButton { background-color: rgba(255, 255, 255, 0.02); border: 1px solid %1; border-radius: 10px; padding: 0px; }"
                    "QPushButton:hover { border: 1.5px solid %2; background-color: rgba(255, 255, 255, 0.05); }"
                ).arg(c.border, c.primary));
            }
        }
    }
    const LiveViewCardStyle liveViewStyle = CameraConfig::getLiveViewCardStyle();
    selectCuratedFont(liveViewGridTitleFontCombo_, liveViewStyle.gridTitleFontFamily);
    liveViewGridTitleSizeSpin_->setValue(liveViewStyle.gridTitleFontSize);
    selectCuratedFont(liveViewDetailTitleFontCombo_, liveViewStyle.detailTitleFontFamily);
    liveViewDetailTitleSizeSpin_->setValue(liveViewStyle.detailTitleFontSize);
    selectCuratedFont(liveViewDetailSectionFontCombo_, liveViewStyle.detailSectionFontFamily);
    liveViewDetailSectionSizeSpin_->setValue(liveViewStyle.detailSectionFontSize);
    const QString backgroundStyle = normalizeLiveViewBackgroundStyle(liveViewStyle.backgroundStyle);

    const int backgroundStyleIndex = liveViewBackgroundStyleCombo_->findData(backgroundStyle);
    if (backgroundStyleIndex != -1) {
        liveViewBackgroundStyleCombo_->setCurrentIndex(backgroundStyleIndex);
    }

    const AnalysisViewStyle analysisStyle = CameraConfig::getAnalysisViewStyle();
    selectCuratedFont(analysisVideoTitleFontCombo_, analysisStyle.videoTitleFontFamily);
    analysisVideoTitleSizeSpin_->setValue(analysisStyle.videoTitleFontSize);
    selectCuratedFont(analysisTimestampFontCombo_, analysisStyle.timestampFontFamily);
    analysisTimestampSizeSpin_->setValue(analysisStyle.timestampFontSize);
    selectCuratedFont(analysisTabFontCombo_, analysisStyle.tabFontFamily);
    analysisTabSizeSpin_->setValue(analysisStyle.tabFontSize);
    const int analysisSurfaceIndex = analysisPlaybackSurfaceCombo_->findData(analysisStyle.playbackSurfaceStyle);
    if (analysisSurfaceIndex != -1) {
        analysisPlaybackSurfaceCombo_->setCurrentIndex(analysisSurfaceIndex);
    }
    const int analysisMetadataIndex = analysisDefaultMetadataCombo_->findData(analysisStyle.defaultMetadataMode);
    if (analysisMetadataIndex != -1) {
        analysisDefaultMetadataCombo_->setCurrentIndex(analysisMetadataIndex);
    }
    selectCuratedFont(analysisMetadataFontCombo_, analysisStyle.defaultMetadataFontFamily);
    analysisMetadataSizeSpin_->setValue(analysisStyle.defaultMetadataFontSize);

    // Initial network status update
    refreshNetworkStatus();
}

void ConfigDialog::setupUiModificationTracking() {
    connect(this, &ConfigDialog::themeSelectionChanged, this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewGridTitleFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewGridTitleSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewDetailTitleFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewDetailTitleSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewDetailSectionFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewDetailSectionSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewBackgroundStyleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisVideoTitleFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisVideoTitleSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisTimestampFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisTimestampSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisTabFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisTabSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisPlaybackSurfaceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisDefaultMetadataCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisMetadataFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisMetadataSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewResetBtn_, &QPushButton::clicked, this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisResetBtn_, &QPushButton::clicked, this, &ConfigDialog::checkUiSettingsModified);

    originalValues_ = captureCurrentSettings();
    originalRecordingValues_ = captureRecordingSettings();
    clearRecordingSettingsModified();
}

ConfigDialog::UiSettingsSnapshot ConfigDialog::captureCurrentSettings() const {
    UiSettingsSnapshot snap;
    snap.themePreset = selectedThemeIndex_;
    snap.liveViewGridTitleFont = liveViewGridTitleFontCombo_ ? currentCuratedFontFamily(liveViewGridTitleFontCombo_) : QString();
    snap.liveViewGridTitleSize = liveViewGridTitleSizeSpin_ ? liveViewGridTitleSizeSpin_->value() : 0;
    snap.liveViewDetailTitleFont = liveViewDetailTitleFontCombo_ ? currentCuratedFontFamily(liveViewDetailTitleFontCombo_) : QString();
    snap.liveViewDetailTitleSize = liveViewDetailTitleSizeSpin_ ? liveViewDetailTitleSizeSpin_->value() : 0;
    snap.liveViewDetailSectionFont = liveViewDetailSectionFontCombo_ ? currentCuratedFontFamily(liveViewDetailSectionFontCombo_) : QString();
    snap.liveViewDetailSectionSize = liveViewDetailSectionSizeSpin_ ? liveViewDetailSectionSizeSpin_->value() : 0;
    snap.liveViewBackgroundStyle = liveViewBackgroundStyleCombo_ ? liveViewBackgroundStyleCombo_->currentData().toString() : QString();
    snap.analysisVideoTitleFont = analysisVideoTitleFontCombo_ ? currentCuratedFontFamily(analysisVideoTitleFontCombo_) : QString();
    snap.analysisVideoTitleSize = analysisVideoTitleSizeSpin_ ? analysisVideoTitleSizeSpin_->value() : 0;
    snap.analysisTimestampFont = analysisTimestampFontCombo_ ? currentCuratedFontFamily(analysisTimestampFontCombo_) : QString();
    snap.analysisTimestampSize = analysisTimestampSizeSpin_ ? analysisTimestampSizeSpin_->value() : 0;
    snap.analysisTabFont = analysisTabFontCombo_ ? currentCuratedFontFamily(analysisTabFontCombo_) : QString();
    snap.analysisTabSize = analysisTabSizeSpin_ ? analysisTabSizeSpin_->value() : 0;
    snap.analysisPlaybackSurface = analysisPlaybackSurfaceCombo_ ? analysisPlaybackSurfaceCombo_->currentData().toString() : QString();
    snap.analysisDefaultMetadataMode = analysisDefaultMetadataCombo_ ? analysisDefaultMetadataCombo_->currentData().toString() : QString();
    snap.analysisMetadataFont = analysisMetadataFontCombo_ ? currentCuratedFontFamily(analysisMetadataFontCombo_) : QString();
    snap.analysisMetadataSize = analysisMetadataSizeSpin_ ? analysisMetadataSizeSpin_->value() : 0;
    return snap;
}

void ConfigDialog::checkUiSettingsModified() {
    bool modified = (captureCurrentSettings() != originalValues_);
    if (uiUnsavedIndicator_) {
        uiUnsavedIndicator_->setText(modified ? "Unsaved changes - apply or save" : "");
    }
    if (uiApplyBtn_) {
        uiApplyBtn_->setEnabled(isAdminMode_ && modified);
    }
}

void ConfigDialog::createCameraWidgetBlock(const CameraInfo& cam) {
    CameraCard* card = new CameraCard(cam, cameraScrollWidget_);
    connectCameraCardSignals(card);

    cameraCards_.push_back(card);
    relayoutCameraCards();
}

void ConfigDialog::relayoutCameraCards() {
    if (!cameraListLayout_ || !cameraScrollWidget_) {
        return;
    }

    while (QLayoutItem* item = cameraListLayout_->takeAt(0)) {
        if (item->widget()) {
            item->widget()->setParent(cameraScrollWidget_);
        }
        delete item;
    }

    const int availableWidth = cameraScrollArea_ ? cameraScrollArea_->viewport()->width() : cameraScrollWidget_->width();

    int columnCount = 1;
    int columnMinWidth = 720;

    if (availableWidth >= 1400) {
        columnCount = 3;
        columnMinWidth = 420;
    } else if (availableWidth >= 980) {
        columnCount = 2;
        columnMinWidth = 0;
    }

    for (int i = 0; i < columnCount; ++i) {
        cameraListLayout_->setColumnStretch(i, 1);
        cameraListLayout_->setColumnMinimumWidth(i, columnMinWidth);
    }

    for (int index = columnCount; index < 4; ++index) {
        cameraListLayout_->setColumnStretch(index, 0);
        cameraListLayout_->setColumnMinimumWidth(index, 0);
    }

    for (int index = 0; index < static_cast<int>(cameraCards_.size()); ++index) {
        CameraCard* card = cameraCards_[index];
        const int row = index / columnCount;
        const int column = index % columnCount;
        cameraListLayout_->addWidget(card, row, column, Qt::AlignTop);
    }
}

void ConfigDialog::relayoutUiPreferencePanels() {
    if (!isVisible()) {
        return;
    }

    const int availableWidth = uiPreferencesScrollArea_ && uiPreferencesScrollArea_->viewport()
        ? uiPreferencesScrollArea_->viewport()->width()
        : width();
    if (availableWidth <= 0) {
        return;
    }
    const bool stackLivePreview = availableWidth < 1040;
    const bool stackAnalysisPreview = availableWidth < 1040;
    const int themeGridWidth = themeGridWidget_ ? themeGridWidget_->width() : availableWidth;

    if (liveViewContentLayout_) {
        liveViewContentLayout_->setDirection(stackLivePreview ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        liveViewContentLayout_->setSpacing(stackLivePreview ? 12 : 16);
    }

    if (analysisViewContentLayout_) {
        analysisViewContentLayout_->setDirection(stackAnalysisPreview ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        analysisViewContentLayout_->setSpacing(stackAnalysisPreview ? 12 : 16);
    }

    const int livePreviewMinWidth = stackLivePreview ? 0 : 360;
    const int livePreviewMaxWidth = stackLivePreview ? QWIDGETSIZE_MAX : 390;
    const int analysisPreviewMinWidth = stackAnalysisPreview ? 0 : 360;
    const int analysisPreviewMaxWidth = stackAnalysisPreview ? QWIDGETSIZE_MAX : 390;
    const QSizePolicy::Policy livePreviewHorizontalPolicy = stackLivePreview ? QSizePolicy::Expanding : QSizePolicy::Preferred;
    const QSizePolicy::Policy analysisPreviewHorizontalPolicy = stackAnalysisPreview ? QSizePolicy::Expanding : QSizePolicy::Preferred;

    if (livePreviewContainer_) {
        livePreviewContainer_->setMinimumWidth(livePreviewMinWidth);
        livePreviewContainer_->setMaximumWidth(livePreviewMaxWidth);
        livePreviewContainer_->setSizePolicy(livePreviewHorizontalPolicy, QSizePolicy::Maximum);
    }

    if (liveViewCardPreviewFrame_) {
        liveViewCardPreviewFrame_->setMinimumWidth(livePreviewMinWidth);
        liveViewCardPreviewFrame_->setMaximumWidth(livePreviewMaxWidth);
    }

    if (analysisPreviewContainer_) {
        analysisPreviewContainer_->setMinimumWidth(analysisPreviewMinWidth);
        analysisPreviewContainer_->setMaximumWidth(analysisPreviewMaxWidth);
        analysisPreviewContainer_->setSizePolicy(analysisPreviewHorizontalPolicy, QSizePolicy::Maximum);
    }

    if (themeGridLayout_) {
        while (QLayoutItem* item = themeGridLayout_->takeAt(0)) {
            delete item;
        }

        int columnCount = 4;
        if (themeGridWidth < 300) {
            columnCount = 2;
        } else if (themeGridWidth < 450) {
            columnCount = 3;
        }

        for (int i = 0; i < columnCount; ++i) {
            themeGridLayout_->setColumnStretch(i, 1);
            themeGridLayout_->setColumnMinimumWidth(i, 0);
        }

        for (int i = columnCount; i < 4; ++i) {
            themeGridLayout_->setColumnStretch(i, 0);
            themeGridLayout_->setColumnMinimumWidth(i, 0);
        }

        for (int index = 0; index < 8; ++index) {
            QPushButton* card = qobject_cast<QPushButton*>(themeCards_[index]);
            if (!card) {
                continue;
            }

            const int row = index / columnCount;
            const int column = index % columnCount;
            themeGridLayout_->addWidget(card, row, column);
        }
    }

    updateGeometry();
}

void ConfigDialog::updateRecordingInfoLabel() {
    if (!recordingInfoLabel_) return;
    const int frames = (preTriggerSpin_->value() + postTriggerSpin_->value())
                       * globalFpsSpin_->value();
    if (frames <= EventSignalScanner::kMaxScannedFrames) {
        recordingInfoLabel_->setText(QStringLiteral(
            "≈ %1 frames per event — every frame is charted.").arg(frames));
    } else {
        const int stride = (frames + EventSignalScanner::kMaxScannedFrames - 1)
                           / EventSignalScanner::kMaxScannedFrames;
        recordingInfoLabel_->setText(QStringLiteral(
            "≈ %1 frames per event — above %2 the whole-event chart samples "
            "every %3rd frame; the DETAIL strip still shows every frame "
            "around the playhead.").arg(frames)
                .arg(EventSignalScanner::kMaxScannedFrames).arg(stride));
    }
}

void ConfigDialog::connectCameraCardSignals(CameraCard* card) {
    connect(card, &CameraCard::editToggled, this, &ConfigDialog::onCameraCardEditToggled);
    connect(card, &CameraCard::removeClicked, this, &ConfigDialog::onCameraCardRemoveClicked);
    connect(card, &CameraCard::sourceChanged, this, &ConfigDialog::onCameraCardSourceChanged);
    connect(card, &CameraCard::macChanged, this, &ConfigDialog::onCameraCardMacChanged);
    connect(card, &CameraCard::deviceSettingsClicked, this, &ConfigDialog::onCameraCardDeviceSettingsClicked);
}

CameraCard* ConfigDialog::findCameraCard(int cameraId) const {
    for (auto* card : cameraCards_) {
        if (card->cameraId() == cameraId) {
            return card;
        }
    }
    return nullptr;
}

CameraCard* ConfigDialog::findCameraCard(QObject* sender) const {
    for (auto* card : cameraCards_) {
        if (card == sender || card->findChild<QObject*>(sender->objectName()) == sender) {
            return card;
        }
    }
    return qobject_cast<CameraCard*>(sender);
}

void ConfigDialog::onCameraCardEditToggled(bool checked) {
    if (CameraCard* card = findCameraCard(sender())) {
        card->setEditable(checked && isAdminMode_);
    }
}

void ConfigDialog::onCameraCardRemoveClicked() {
    CameraCard* card = findCameraCard(sender());
    if (!card) return;

    // Show premium delete confirmation dialog
    DeleteConfirmationDialog dialog(
        QString("Camera %1: %2").arg(card->cameraId()).arg(card->name()),
        this
    );

    if (dialog.exec() == QDialog::Accepted) {
        // Remove from vector
        cameraCards_.erase(std::remove(cameraCards_.begin(), cameraCards_.end(), card),
                          cameraCards_.end());

        // Remove from UI
        cameraListLayout_->removeWidget(card);
        delete card;

        relayoutCameraCards();

        // refreshNetworkStatus() also refreshes the fixed-IP registry.
        refreshNetworkStatus();
    }
}

void ConfigDialog::refreshFixedIpList() {
    if (!fixedIpListPanel_) {
        return;
    }

    const bool emulationActive = CameraConfig::isEmulationActive();
    std::vector<CameraInfo> cameras;
    std::vector<QString> detectedIps;
    cameras.reserve(cameraCards_.size());
    detectedIps.reserve(cameraCards_.size());
    for (auto* card : cameraCards_) {
        cameras.push_back(card->cameraInfo());
        QString detected = card->detectedIp();
        if (card->sourceType() == 0) {
            detected = QStringLiteral("Emulated");
        } else if (emulationActive && card->sourceType() == 1 &&
                   (detected.isEmpty() || detected == QStringLiteral("Offline"))) {
            // Emulation is on but this Real card has no matching device - mirror
            // the camera card status instead of a bare "Offline".
            detected = QStringLiteral("Offline - no hardware");
        }
        detectedIps.push_back(detected);
    }
    fixedIpListPanel_->setCameras(cameras, detectedIps);

    // Keep the Machine Groups registry and the Machine Layout visual in sync
    // too (they mirror each camera's assigned group / position from its card).
    refreshMachineGroups();
    refreshMachineLayout();
}

void ConfigDialog::refreshMachineGroups() {
    if (!machineGroupsPanel_) {
        return;
    }

    std::vector<CameraInfo> cameras;
    cameras.reserve(cameraCards_.size());
    for (auto* card : cameraCards_) {
        cameras.push_back(card->cameraInfo());
    }
    machineGroupsPanel_->setCameras(cameras);
}

void ConfigDialog::refreshMachineLayout() {
    if (!machineLayoutPanel_) {
        return;
    }

    std::vector<CameraInfo> cameras;
    cameras.reserve(cameraCards_.size());
    for (auto* card : cameraCards_) {
        cameras.push_back(card->cameraInfo());
    }
    machineLayoutPanel_->setCameras(cameras);
}

void ConfigDialog::onCameraCardSourceChanged(int) {
    refreshNetworkStatus();
}

void ConfigDialog::onCameraCardMacChanged(const QString&) {
    QSet<QString> reservedMacs;
    for (auto* card : cameraCards_) {
        const QString configuredMac = normalizeMac(card->macAddress());
        if (!configuredMac.isEmpty() && configuredMac != "NONE/AUTO") {
            reservedMacs.insert(configuredMac);
        }
    }

    for (auto* card : cameraCards_) {
        card->updateMacCombo(currentGigEDevices_, card->macAddress(), reservedMacs);
    }

    // refreshNetworkStatus() also refreshes the fixed-IP registry.
    refreshNetworkStatus();
}

void ConfigDialog::onAddCameraConfigClicked() {
    // The Fixed IP List is the authority for the camera card count; both entry
    // points respect the same hard limit (16).
    if (static_cast<int>(cameraCards_.size()) >= 16) {
        QMessageBox::information(this, "Camera Limit",
            "Maximum of 16 cameras reached. Delete a camera first.");
        return;
    }

    CameraInfo cam;

    int maxId = 0;
    for (auto* card : cameraCards_) {
        if (card->cameraId() > maxId) maxId = card->cameraId();
    }
    cam.id = maxId + 1;
    cam.source = 0;
    cam.name = QString("DRYER %1").arg(cam.id);
    cam.location = QString("CYLINDER %1").arg(10 + cam.id);
    cam.side = "DRIVE SIDE";
    cam.machinePosition = 16000 + (cam.id * 500);
    cam.ipAddress = QString("172.20.2.%1").arg(cam.id);
    cam.macAddress = "";
    cam.subnetMask = "255.255.255.0";
    cam.defaultGateway = "0.0.0.0";
    cam.fps = 50;
    cam.enableAcquisitionFps = false;
    cam.width = 780;
    cam.height = 580;
    cam.offsetX = 0;
    cam.offsetY = 0;
    cam.pixelFormat = "Mono8";
    cam.exposureTimeAbs = 5000.0;
    cam.enableExposureTimeBase = false;
    cam.exposureTimeBaseAbs = 20.0;
    cam.exposureTimeRaw = 2044;
    cam.chunkModeActive = false;
    cam.enabledChunks = QStringList() << "Timestamp" << "Framecounter";
    cam.temperature = 0.0;

    createCameraWidgetBlock(cam);

    // Scroll to the new card
    cameraScrollArea_->ensureWidgetVisible(cameraCards_.back());
    refreshFixedIpList();
}

void ConfigDialog::onCameraCardDeviceSettingsClicked() {
    CameraCard* card = findCameraCard(sender());
    if (!card) return;

    const auto it = std::find(cameraCards_.begin(), cameraCards_.end(), card);
    const int cameraIndex = it == cameraCards_.end()
        ? 0
        : static_cast<int>(std::distance(cameraCards_.begin(), it));

    CameraInfo dialogInfo = CameraConfig::getCameraInfo(cameraIndex);
    const CameraInfo cardInfo = card->cameraInfo();
    dialogInfo.id = cardInfo.id;
    dialogInfo.source = cardInfo.source;
    dialogInfo.name = cardInfo.name;
    dialogInfo.location = cardInfo.location;
    dialogInfo.side = cardInfo.side;
    dialogInfo.machinePosition = cardInfo.machinePosition;
    dialogInfo.ipAddress = cardInfo.ipAddress;
    dialogInfo.macAddress = cardInfo.macAddress;
    dialogInfo.subnetMask = cardInfo.subnetMask;
    dialogInfo.defaultGateway = cardInfo.defaultGateway;

    CameraDeviceSettingsDialog dialog(cameraIndex, dialogInfo, cameraManager_, isAdminMode_, this);
    connect(&dialog, &CameraDeviceSettingsDialog::settingsApplied, this,
            [this, card, cameraIndex](const CameraInfo& info) {
                card->setCameraInfo(info);
                emit cameraDeviceSettingsChanged(cameraIndex, info);
            });
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    card->setCameraInfo(dialog.updatedInfo());
    emit cameraDeviceSettingsChanged(cameraIndex, dialog.updatedInfo());
}

void ConfigDialog::onRemoveCameraConfigClicked() {
    // Handled by CameraCard signals
}

bool ConfigDialog::validateAndPrepareEventStorage(QString* normalizedPath, QString* errorMessage) const {
    const QString eventStoragePath = QDir::cleanPath(eventStoragePathEdit_->text().trimmed());
    if (eventStoragePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "Please select a valid event storage folder.";
        }
        return false;
    }

    QDir storageDir(eventStoragePath);
    if (!storageDir.exists() && !QDir().mkpath(eventStoragePath)) {
        if (errorMessage) {
            *errorMessage = QString("Unable to create event storage folder:\n%1").arg(eventStoragePath);
        }
        return false;
    }

    QFileInfo storageInfo(eventStoragePath);
    if (!storageInfo.isDir() || !storageInfo.isWritable()) {
        if (errorMessage) {
            *errorMessage = QString("Event storage folder is not writable:\n%1").arg(eventStoragePath);
        }
        return false;
    }

    if (normalizedPath) {
        *normalizedPath = eventStoragePath;
    }
    return true;
}

void ConfigDialog::emitConfigUpdated(bool requiresCameraRestart) {
    QMetaObject::invokeMethod(this, [this, requiresCameraRestart]() {
        qInfo() << "[ConfigDialog] Emitting configUpdated" << requiresCameraRestart;
        emit configUpdated(requiresCameraRestart);
    }, Qt::QueuedConnection);
}

void ConfigDialog::saveCameraConfiguration() {
    qInfo() << "[ConfigDialog] Camera save requested. cardCount=" << cameraCards_.size();

    QStringList validationErrors;
    if (!validateConfiguration(&validationErrors)) {
        qWarning() << "[ConfigDialog] Validation failed:" << validationErrors;
        QMessageBox::warning(this, "Invalid Camera Configuration", validationErrors.join("\n"));
        return;
    }

    const std::vector<CameraInfo> previousCameras = CameraConfig::getCameras();
    std::vector<CameraInfo> newCameras;
    for (auto* card : cameraCards_) {
        CameraInfo cam = card->cameraInfo();
        cam.id = card->cameraId();
        cam.source = card->sourceType();
        cam.name = card->name();
        cam.location = card->location();
        cam.side = card->side();
        cam.machinePosition = card->position();
        cam.ipAddress = card->ipAddress();
        cam.macAddress = normalizeMac(card->macAddress());
        if (cam.macAddress.isEmpty()) cam.macAddress = "";
        cam.subnetMask = card->subnetMask();
        cam.defaultGateway = card->gateway();
        cam.temperature = 0.0;
        newCameras.push_back(cam);
    }

    CameraConfig::saveCameras(newCameras);

    const bool requiresCameraRestart = !cameraConfigListEqual(previousCameras, newCameras);
    qInfo() << "[ConfigDialog] Camera save complete. requiresCameraRestart=" << requiresCameraRestart;
    QMessageBox::information(
        this,
        "Camera Configuration Saved",
        requiresCameraRestart
            ? "Camera configuration saved. Acquisition will be restarted to apply the changes."
            : "Camera configuration saved.");
    emitConfigUpdated(requiresCameraRestart);

    // Keep the read-only Fixed IP registry in sync with the saved cards.
    refreshFixedIpList();
}

void ConfigDialog::saveRecordingSettings() {
    qInfo() << "[ConfigDialog] Recording save requested";

    QString eventStoragePath;
    QString eventStorageError;
    if (!validateAndPrepareEventStorage(&eventStoragePath, &eventStorageError)) {
        QMessageBox::warning(this, "Invalid Event Storage", eventStorageError);
        return;
    }

    CameraConfig::setFps(globalFpsSpin_->value());
    CameraConfig::setPreTriggerSeconds(preTriggerSpin_->value());
    CameraConfig::setPostTriggerSeconds(postTriggerSpin_->value());
    CameraConfig::setEventRetentionCount(eventRetentionSpin_->value());
    if (lowDiskThresholdSpin_) CameraConfig::setLowDiskWarningPct(lowDiskThresholdSpin_->value());
    CameraConfig::setEventStoragePath(eventStoragePath);
    bool cameraModeChanged = false;
    if (cameraSourceCombo_) {
        const auto newSource =
            static_cast<CameraConfig::CameraSource>(cameraSourceCombo_->currentData().toInt());
        cameraModeChanged = CameraConfig::getCameraSource() != newSource;
        CameraConfig::setCameraSource(newSource);
    }

    // The fallback fps is an app-start setting: the running cameras keep the
    // rate they were initialized with. Tell the user when a restart is needed.
    QString fallbackHint;
    if (!cameraModeChanged && globalFpsSpin_->value() != CameraManager::appStartFallbackFps()) {
        fallbackHint = QStringLiteral(
            "\n\nFallback FPS takes effect after the application is restarted "
            "(the running cameras keep their current rate).");
    }

    QMessageBox::information(this, "Recording Settings Saved",
        cameraModeChanged
            ? QStringLiteral("Recording settings saved.\n\nCamera mode change applied - acquisition is restarting with the new camera mode.")
            : QStringLiteral("Recording settings saved.%1").arg(fallbackHint));
    originalRecordingValues_ = captureRecordingSettings();
    clearRecordingSettingsModified();
    refreshStorageStats();
    // Camera Mode may have switched: re-evaluate the camera cards' online
    // status and the Fixed IP registry immediately (both depend on whether
    // emulation is active) instead of waiting for the next network poll.
    refreshNetworkStatus();
    // A Camera Mode switch changes which Pylon devices exist, so it must run
    // through the camera lifecycle (stop + re-initialize) to take effect.
    emitConfigUpdated(cameraModeChanged);
}
void ConfigDialog::saveOpcUaSettings() {
    qInfo() << "[ConfigDialog] OPC UA save requested";

    OpcUaSettings settings = CameraConfig::getDefaultOpcUaSettings();
    settings.enabled = opcUaEnabledCheck_ && opcUaEnabledCheck_->isChecked();
    settings.endpointUrl = opcUaEndpointEdit_ ? opcUaEndpointEdit_->text().trimmed() : QString();
    settings.useUsernamePassword = opcUaAuthModeCombo_ && opcUaAuthModeCombo_->currentData().toBool();
    settings.username = opcUaUsernameEdit_ ? opcUaUsernameEdit_->text().trimmed() : QString();
    settings.password = opcUaPasswordEdit_ ? opcUaPasswordEdit_->text() : QString();
    settings.publishIntervalMs = opcUaPublishIntervalSpin_ ? opcUaPublishIntervalSpin_->value() : settings.publishIntervalMs;
    settings.reconnectIntervalMs = opcUaReconnectIntervalSpin_ ? opcUaReconnectIntervalSpin_->value() : settings.reconnectIntervalMs;
    settings.positionDirectionSign = (opcUaPositionDirectionCombo_ && opcUaPositionDirectionCombo_->currentData().toInt() < 0) ? -1 : 1;

    QStringList validationErrors;
    bool hasEnabledTrigger = false;

    settings.triggerTags.clear();
    settings.triggerTags.reserve(kOpcUaTriggerSlots);
    for (int i = 0; i < kOpcUaTriggerSlots; ++i) {
        const OpcUaTriggerRowWidgets& row = opcUaTriggerRows_[static_cast<size_t>(i)];
        OpcUaTriggerTagSettings tag;
        tag.enabled = row.enabledCheck && row.enabledCheck->isChecked();
        tag.name = row.nameEdit ? row.nameEdit->text().trimmed() : QString();
        tag.nodeId = row.nodeIdEdit ? row.nodeIdEdit->text().trimmed() : QString();
        tag.minimumIntervalMs = row.minimumIntervalSpin ? row.minimumIntervalSpin->value() : 0;
        tag.simulated = row.simulatedCombo ? row.simulatedCombo->currentData().toBool() : false;
        tag.group = row.groupCombo ? row.groupCombo->currentData().toInt() : CameraGroup::kUnassigned;
        tag.positionMm = row.positionMmSpin ? row.positionMmSpin->value() : 0;

        if (tag.name.isEmpty()) {
            tag.name = QString("Trigger %1").arg(i + 1);
        }
        if (tag.enabled) {
            hasEnabledTrigger = true;
            if (!tag.simulated && tag.nodeId.isEmpty()) {
                validationErrors.append(QString("%1 is enabled but has no NodeId.").arg(tag.name));
            }
        }
        settings.triggerTags.push_back(tag);
    }

    settings.speedTags.clear();
    settings.speedTags.reserve(kOpcUaSpeedSlots);
    for (int i = 0; i < kOpcUaSpeedSlots; ++i) {
        const OpcUaSpeedRowWidgets& row = opcUaSpeedRows_[static_cast<size_t>(i)];
        OpcUaSpeedTagSettings tag;
        tag.enabled = row.enabledCheck && row.enabledCheck->isChecked();
        tag.name = row.nameEdit ? row.nameEdit->text().trimmed() : QString();
        tag.nodeId = row.nodeIdEdit ? row.nodeIdEdit->text().trimmed() : QString();
        tag.scale = row.scaleSpin ? row.scaleSpin->value() : 1.0;
        tag.offset = row.offsetSpin ? row.offsetSpin->value() : 0.0;
        tag.unit = row.unitEdit ? row.unitEdit->text().trimmed() : QStringLiteral("m/min");
        tag.staleTimeoutMs = row.staleTimeoutSpin ? row.staleTimeoutSpin->value() : 2000;
        tag.simulated = row.simulatedCombo && row.simulatedCombo->currentData().toBool();
        tag.simulatedValue = row.simulatedValueSpin ? row.simulatedValueSpin->value() : 0.0;
        tag.positionMm = row.positionMmSpin ? row.positionMmSpin->value() : 0;
        if (tag.name.isEmpty()) {
            tag.name = QString("Speed %1").arg(i + 1);
        }
        if (tag.unit.isEmpty()) {
            tag.unit = QStringLiteral("m/min");
        }
        settings.speedTags.push_back(tag);
    }

    bool hasRealTag = false;
    for (const auto& tag : settings.triggerTags) {
        if (tag.enabled && !tag.simulated && !tag.nodeId.isEmpty()) {
            hasRealTag = true;
            break;
        }
    }
    if (!hasRealTag) {
        for (const auto& tag : settings.speedTags) {
            if (tag.enabled && !tag.simulated && !tag.nodeId.isEmpty()) {
                hasRealTag = true;
                break;
            }
        }
    }

    if (settings.enabled) {
        if (hasRealTag && settings.endpointUrl.isEmpty()) {
            validationErrors.append("Endpoint URL is required when OPC UA is enabled and live tags are used.");
        }
        if (hasRealTag && settings.useUsernamePassword && settings.username.isEmpty()) {
            validationErrors.append("Username is required when username/password authentication is selected.");
        }
        bool anySpeedEnabled = false;
        for (const auto& tag : settings.speedTags) {
            if (tag.enabled) {
                anySpeedEnabled = true;
                break;
            }
        }
        if (!hasEnabledTrigger && !anySpeedEnabled) {
            validationErrors.append("Enable at least one trigger tag or a machine speed tag before turning on the OPC UA client.");
        }
    }

    for (const auto& tag : settings.speedTags) {
        if (tag.enabled && !tag.simulated && tag.nodeId.isEmpty()) {
            validationErrors.append(QString("%1 is enabled but has no NodeId.").arg(tag.name));
        }
    }

    if (!validationErrors.isEmpty()) {
        QMessageBox::warning(this, "Invalid OPC UA Settings", validationErrors.join("\n"));
        return;
    }

    CameraConfig::setOpcUaSettings(settings);
    QMessageBox::information(this, "OPC UA Settings Saved", "OPC UA settings saved.");
    emitConfigUpdated(false);
}


void ConfigDialog::saveUiSettings() {
    qInfo() << "[ConfigDialog] UI save requested";

    CameraConfig::setThemePreset(selectedThemeIndex_);
    CameraConfig::setLiveViewCardStyle({
        currentCuratedFontFamily(liveViewGridTitleFontCombo_),
        liveViewGridTitleSizeSpin_->value(),
        currentCuratedFontFamily(liveViewDetailTitleFontCombo_),
        liveViewDetailTitleSizeSpin_->value(),
        currentCuratedFontFamily(liveViewDetailSectionFontCombo_),
        liveViewDetailSectionSizeSpin_->value(),
        liveViewBackgroundStyleCombo_->currentData().toString()
    });
    CameraConfig::setAnalysisViewStyle({
        currentCuratedFontFamily(analysisVideoTitleFontCombo_),
        analysisVideoTitleSizeSpin_->value(),
        currentCuratedFontFamily(analysisTimestampFontCombo_),
        analysisTimestampSizeSpin_->value(),
        currentCuratedFontFamily(analysisTabFontCombo_),
        analysisTabSizeSpin_->value(),
        analysisPlaybackSurfaceCombo_->currentData().toString(),
        analysisDefaultMetadataCombo_ ? analysisDefaultMetadataCombo_->currentData().toString() : QStringLiteral("realtime"),
        currentCuratedFontFamily(analysisMetadataFontCombo_),
        analysisMetadataSizeSpin_->value()
    });

    QMessageBox::information(this, "UI Preferences Saved", "UI preferences saved.");
    originalValues_ = captureCurrentSettings();
    clearUiSettingsModified();
    emitConfigUpdated(false);
}

void ConfigDialog::applyUiSettings() {
    qInfo() << "[ConfigDialog] UI apply requested";

    CameraConfig::setThemePreset(selectedThemeIndex_);
    CameraConfig::setLiveViewCardStyle({
        currentCuratedFontFamily(liveViewGridTitleFontCombo_),
        liveViewGridTitleSizeSpin_->value(),
        currentCuratedFontFamily(liveViewDetailTitleFontCombo_),
        liveViewDetailTitleSizeSpin_->value(),
        currentCuratedFontFamily(liveViewDetailSectionFontCombo_),
        liveViewDetailSectionSizeSpin_->value(),
        liveViewBackgroundStyleCombo_->currentData().toString()
    });
    CameraConfig::setAnalysisViewStyle({
        currentCuratedFontFamily(analysisVideoTitleFontCombo_),
        analysisVideoTitleSizeSpin_->value(),
        currentCuratedFontFamily(analysisTimestampFontCombo_),
        analysisTimestampSizeSpin_->value(),
        currentCuratedFontFamily(analysisTabFontCombo_),
        analysisTabSizeSpin_->value(),
        analysisPlaybackSurfaceCombo_->currentData().toString(),
        analysisDefaultMetadataCombo_ ? analysisDefaultMetadataCombo_->currentData().toString() : QStringLiteral("realtime"),
        currentCuratedFontFamily(analysisMetadataFontCombo_),
        analysisMetadataSizeSpin_->value()
    });

    originalValues_ = captureCurrentSettings();
    clearUiSettingsModified();
    emitConfigUpdated(false);
}

void ConfigDialog::clearUiSettingsModified() {
    if (uiUnsavedIndicator_) {
        uiUnsavedIndicator_->setText("");
    }
    if (uiApplyBtn_) {
        uiApplyBtn_->setEnabled(false);
    }
}

ConfigDialog::RecordingSettingsSnapshot ConfigDialog::captureRecordingSettings() const {
    RecordingSettingsSnapshot snap;
    snap.fps = globalFpsSpin_ ? globalFpsSpin_->value() : 0;
    snap.preTrigger = preTriggerSpin_ ? preTriggerSpin_->value() : 0;
    snap.postTrigger = postTriggerSpin_ ? postTriggerSpin_->value() : 0;
    snap.retention = eventRetentionSpin_ ? eventRetentionSpin_->value() : 0;
    snap.lowDiskWarningPct = lowDiskThresholdSpin_ ? lowDiskThresholdSpin_->value() : 0;
    snap.cameraSource = cameraSourceCombo_ ? cameraSourceCombo_->currentData().toInt() : 0;
    snap.eventStoragePath = eventStoragePathEdit_ ? eventStoragePathEdit_->text() : QString();
    return snap;
}

void ConfigDialog::checkRecordingSettingsModified() {
    if (!recordingUnsavedIndicator_) {
        return;
    }
    const bool modified = (captureRecordingSettings() != originalRecordingValues_);
    recordingUnsavedIndicator_->setText(modified ? "Unsaved changes - save to apply" : "");
}

void ConfigDialog::clearRecordingSettingsModified() {
    if (recordingUnsavedIndicator_) {
        recordingUnsavedIndicator_->setText("");
    }
}

void ConfigDialog::refreshStorageStats() {
    if (!storageStatsLabel_) {
        return;
    }

    const QString path = eventStoragePathEdit_
        ? eventStoragePathEdit_->text().trimmed()
        : CameraConfig::getEventStoragePath();

    // Count event data files and their total size.
    qint64 totalBytes = 0;
    int fileCount = 0;
    QDir dir(path);
    if (dir.exists()) {
        const QStringList filters = {
            QStringLiteral("event_*.json"),
            QStringLiteral("event_*.bin"),
        };
        const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files, QDir::Name);
        for (const QFileInfo& info : entries) {
            totalBytes += info.size();
            ++fileCount;
        }
    }

    // Disk capacity and free space for the storage volume.
    QStorageInfo storage(path);
    QString capMb;
    QString freeMb;
    QString freePctStr = QStringLiteral("--");
    double freePct = 100.0;
    if (storage.isValid()) {
        capMb = QString::number(storage.bytesTotal() / (1024.0 * 1024.0), 'f', 1);
        freeMb = QString::number(storage.bytesAvailable() / (1024.0 * 1024.0), 'f', 1);
        if (storage.bytesTotal() > 0) {
            freePct = (100.0 * storage.bytesAvailable()) / storage.bytesTotal();
        }
        freePctStr = QString::number(freePct, 'f', 1);
    } else {
        capMb = QStringLiteral("unavailable");
        freeMb = QStringLiteral("unavailable");
    }

    // Low-disk warning: amber below the configured threshold, red below half of it.
    const double warningPct = CameraConfig::getLowDiskWarningPct();
    const double criticalPct = qMax(1.0, warningPct / 2.0);
    const ThemeColors themeColors = CameraConfig::getThemeColors();
    QString warningMarker;
    QString warningColor;
    if (storage.isValid() && freePct < warningPct) {
        warningMarker = QStringLiteral("\u26A0 ");
        warningColor = freePct < criticalPct ? QStringLiteral("#FF5A5A")
                                             : QStringLiteral("#FFB020");
    }
    storageStatsLabel_->setStyleSheet(QString(
        "color: %1; font-size: 11px; padding-top: 4px;")
        .arg(warningColor.isEmpty() ? themeColors.text : warningColor));

    const QString sizeMb = QString::number(totalBytes / (1024.0 * 1024.0), 'f', 1);
    const QString fileCountStr = QString::number(fileCount);

    storageStatsLabel_->setText(QStringLiteral(
        "%1Data: %2 MB  \u00B7  Files: %3  \u00B7  Disk: %4 MB  \u00B7  Free: %5 MB (%6%)")
        .arg(warningMarker, sizeMb, fileCountStr, capMb, freeMb, freePctStr));
    storageStatsLabel_->setToolTip(QStringLiteral(
        "Total recorded data: %1 MB across %2 files\n"
        "Disk capacity: %3 MB, free: %4 MB (%5% free)\n"
        "%6")
        .arg(sizeMb, fileCountStr, capMb, freeMb, freePctStr,
             storage.isValid()
                 ? (warningColor.isEmpty()
                        ? QStringLiteral("Storage is healthy.")
                        : QStringLiteral("Low disk space: free space is below the warning "
                                         "threshold. Consider freeing space or increasing capacity."))
                 : QStringLiteral("Storage info unavailable for this path.")));
}


void ConfigDialog::resetLiveViewCardSettings() {
    const LiveViewCardStyle defaults = CameraConfig::getDefaultLiveViewCardStyle();
    selectCuratedFont(liveViewGridTitleFontCombo_, defaults.gridTitleFontFamily);
    liveViewGridTitleSizeSpin_->setValue(defaults.gridTitleFontSize);
    selectCuratedFont(liveViewDetailTitleFontCombo_, defaults.detailTitleFontFamily);
    liveViewDetailTitleSizeSpin_->setValue(defaults.detailTitleFontSize);
    selectCuratedFont(liveViewDetailSectionFontCombo_, defaults.detailSectionFontFamily);
    liveViewDetailSectionSizeSpin_->setValue(defaults.detailSectionFontSize);

    const int backgroundStyleIndex = liveViewBackgroundStyleCombo_->findData(normalizeLiveViewBackgroundStyle(defaults.backgroundStyle));
    if (backgroundStyleIndex != -1) {
        liveViewBackgroundStyleCombo_->setCurrentIndex(backgroundStyleIndex);
    }
}

void ConfigDialog::resetAnalysisViewSettings() {
    const AnalysisViewStyle defaults = CameraConfig::getDefaultAnalysisViewStyle();
    selectCuratedFont(analysisVideoTitleFontCombo_, defaults.videoTitleFontFamily);
    analysisVideoTitleSizeSpin_->setValue(defaults.videoTitleFontSize);
    selectCuratedFont(analysisTimestampFontCombo_, defaults.timestampFontFamily);
    analysisTimestampSizeSpin_->setValue(defaults.timestampFontSize);
    selectCuratedFont(analysisTabFontCombo_, defaults.tabFontFamily);
    analysisTabSizeSpin_->setValue(defaults.tabFontSize);

    const int playbackSurfaceIndex = analysisPlaybackSurfaceCombo_->findData(defaults.playbackSurfaceStyle);
    if (playbackSurfaceIndex != -1) {
        analysisPlaybackSurfaceCombo_->setCurrentIndex(playbackSurfaceIndex);
    }

    const int metadataIndex = analysisDefaultMetadataCombo_->findData(defaults.defaultMetadataMode);
    if (metadataIndex != -1) {
        analysisDefaultMetadataCombo_->setCurrentIndex(metadataIndex);
    }
    selectCuratedFont(analysisMetadataFontCombo_, defaults.defaultMetadataFontFamily);
    analysisMetadataSizeSpin_->setValue(defaults.defaultMetadataFontSize);
}

void ConfigDialog::setAdminMode(bool isAdmin) {
    isAdminMode_ = isAdmin;

    // Global settings
    if (networkSummaryHeader_) {
        // Update add button visibility
    }
    globalFpsSpin_->setEnabled(isAdmin);
    preTriggerSpin_->setEnabled(isAdmin);
    postTriggerSpin_->setEnabled(isAdmin);
    eventRetentionSpin_->setEnabled(isAdmin);
    if (lowDiskThresholdSpin_) lowDiskThresholdSpin_->setEnabled(isAdmin);
    if (cameraSourceCombo_) cameraSourceCombo_->setEnabled(isAdmin);
    if (recordingSaveBtn_) recordingSaveBtn_->setEnabled(isAdmin);
    if (recordingUnsavedIndicator_) recordingUnsavedIndicator_->setVisible(isAdmin);
    if (opcUaEnabledCheck_) opcUaEnabledCheck_->setEnabled(isAdmin);
    if (opcUaEndpointEdit_) opcUaEndpointEdit_->setEnabled(isAdmin);
    if (opcUaAuthModeCombo_) opcUaAuthModeCombo_->setEnabled(isAdmin);
    if (opcUaUsernameEdit_) opcUaUsernameEdit_->setEnabled(isAdmin && opcUaAuthModeCombo_ && opcUaAuthModeCombo_->currentData().toBool());
    if (opcUaPasswordEdit_) opcUaPasswordEdit_->setEnabled(isAdmin && opcUaAuthModeCombo_ && opcUaAuthModeCombo_->currentData().toBool());
    if (opcUaPublishIntervalSpin_) opcUaPublishIntervalSpin_->setEnabled(isAdmin);
    if (opcUaReconnectIntervalSpin_) opcUaReconnectIntervalSpin_->setEnabled(isAdmin);
    for (auto& row : opcUaTriggerRows_) {
        if (row.enabledCheck) row.enabledCheck->setEnabled(isAdmin);
        if (row.nameEdit) row.nameEdit->setEnabled(isAdmin);
        if (row.nodeIdEdit) row.nodeIdEdit->setEnabled(isAdmin);
        if (row.simulatedCombo) row.simulatedCombo->setEnabled(isAdmin);
        if (row.groupCombo) row.groupCombo->setEnabled(isAdmin);
        if (row.positionMmSpin) row.positionMmSpin->setEnabled(isAdmin);
        if (row.manualTriggerBtn) row.manualTriggerBtn->setEnabled(isAdmin);
        if (row.minimumIntervalSpin) row.minimumIntervalSpin->setEnabled(isAdmin);
    }
    for (auto& row : opcUaSpeedRows_) {
        if (row.enabledCheck) row.enabledCheck->setEnabled(isAdmin);
        if (row.nameEdit) row.nameEdit->setEnabled(isAdmin);
        if (row.nodeIdEdit) row.nodeIdEdit->setEnabled(isAdmin);
        if (row.simulatedCombo) row.simulatedCombo->setEnabled(isAdmin);
        if (row.simulatedValueSpin) row.simulatedValueSpin->setEnabled(isAdmin
            && row.simulatedCombo && row.simulatedCombo->currentData().toBool());
        if (row.positionMmSpin) row.positionMmSpin->setEnabled(isAdmin);
        if (row.scaleSpin) row.scaleSpin->setEnabled(isAdmin);
        if (row.offsetSpin) row.offsetSpin->setEnabled(isAdmin);
        if (row.unitEdit) row.unitEdit->setEnabled(isAdmin);
        if (row.staleTimeoutSpin) row.staleTimeoutSpin->setEnabled(isAdmin);
    }
    if (opcUaPositionDirectionCombo_) opcUaPositionDirectionCombo_->setEnabled(isAdmin);
    if (opcUaSaveBtn_) opcUaSaveBtn_->setEnabled(isAdmin);
    eventStoragePathEdit_->setEnabled(isAdmin);
    browseEventStorageBtn_->setEnabled(isAdmin);
    resetEventStorageBtn_->setEnabled(isAdmin);
    for (int i = 0; i < 8; ++i) {
        if (themeCards_[i]) themeCards_[i]->setEnabled(isAdmin);
    }
    if (themeCombo_) themeCombo_->setEnabled(isAdmin);
    if (liveViewBackgroundStyleCombo_) liveViewBackgroundStyleCombo_->setEnabled(isAdmin);
    if (liveViewGridTitleFontCombo_) liveViewGridTitleFontCombo_->setEnabled(isAdmin);
    if (liveViewGridTitleSizeSpin_) liveViewGridTitleSizeSpin_->setEnabled(isAdmin);
    if (liveViewDetailTitleFontCombo_) liveViewDetailTitleFontCombo_->setEnabled(isAdmin);
    if (liveViewDetailTitleSizeSpin_) liveViewDetailTitleSizeSpin_->setEnabled(isAdmin);
    if (liveViewDetailSectionFontCombo_) liveViewDetailSectionFontCombo_->setEnabled(isAdmin);
    if (liveViewDetailSectionSizeSpin_) liveViewDetailSectionSizeSpin_->setEnabled(isAdmin);
    if (analysisVideoTitleFontCombo_) analysisVideoTitleFontCombo_->setEnabled(isAdmin);
    if (analysisVideoTitleSizeSpin_) analysisVideoTitleSizeSpin_->setEnabled(isAdmin);
    if (analysisTimestampFontCombo_) analysisTimestampFontCombo_->setEnabled(isAdmin);
    if (analysisTimestampSizeSpin_) analysisTimestampSizeSpin_->setEnabled(isAdmin);
    if (analysisTabFontCombo_) analysisTabFontCombo_->setEnabled(isAdmin);
    if (analysisTabSizeSpin_) analysisTabSizeSpin_->setEnabled(isAdmin);
    if (analysisPlaybackSurfaceCombo_) analysisPlaybackSurfaceCombo_->setEnabled(isAdmin);
    if (analysisDefaultMetadataCombo_) analysisDefaultMetadataCombo_->setEnabled(isAdmin);
    if (analysisMetadataFontCombo_) analysisMetadataFontCombo_->setEnabled(isAdmin);
    if (analysisMetadataSizeSpin_) analysisMetadataSizeSpin_->setEnabled(isAdmin);
    if (liveViewResetBtn_) liveViewResetBtn_->setEnabled(isAdmin);
    if (analysisResetBtn_) analysisResetBtn_->setEnabled(isAdmin);
    if (uiSaveBtn_) uiSaveBtn_->setEnabled(isAdmin);
    if (uiApplyBtn_) uiApplyBtn_->setEnabled(isAdmin && (captureCurrentSettings() != originalValues_));
    if (uiUnsavedIndicator_) uiUnsavedIndicator_->setVisible(isAdmin);

    // Per-camera: only the edit checkbox is admin-gated
    for (auto* card : cameraCards_) {
        // Card handles its own edit state
    }
    if (ipConfiguratorPanel_) ipConfiguratorPanel_->setAdminMode(isAdmin);
}

void ConfigDialog::onRefreshLogsClicked() {
    networkSummaryHeader_->setRefreshing(true);

    currentGigEDevices_ = CameraManager::enumerateGigEDevices(/*forceRefresh=*/true);
    QString refreshTs = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    connectionLogsBrowser_->append(QString("--- Refresh at %1 ---").arg(refreshTs));

    static QMap<QString, QString> cameraConnectionTimes;

    for (const auto& dev : currentGigEDevices_) {
        QString mac = QString::fromStdString(dev.macAddress);

        if (!cameraConnectionTimes.contains(mac)) {
            cameraConnectionTimes.insert(mac, QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
        }

        QString devTs = cameraConnectionTimes.value(mac);
        connectionLogsBrowser_->append(
            QString("[%1] %2 | MAC: %3 | IP: %4 | Subnet: %5 | Gateway: %6")
            .arg(devTs)
            .arg(QString::fromStdString(dev.friendlyName))
            .arg(QString::fromStdString(dev.macAddress))
            .arg(QString::fromStdString(dev.ipAddress))
            .arg(QString::fromStdString(dev.subnetMask))
            .arg(QString::fromStdString(dev.defaultGateway))
        );
    }

    if (currentGigEDevices_.empty()) {
        cameraConnectionTimes.clear();
        QString emptyTs = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        connectionLogsBrowser_->append(QString("[%1] No online Real cameras detected.").arg(emptyTs));
    }

    QSet<QString> reservedMacs;
    for (auto* card : cameraCards_) {
        const QString configuredMac = normalizeMac(card->macAddress());
        if (!configuredMac.isEmpty() && configuredMac != "NONE/AUTO") {
            reservedMacs.insert(configuredMac);
        }
    }

    for (auto* card : cameraCards_) {
        card->updateMacCombo(currentGigEDevices_, card->macAddress(), reservedMacs);
    }

    refreshNetworkStatus();
    networkSummaryHeader_->setRefreshing(false);
}

void ConfigDialog::onClearLogsClicked() {
    connectionLogsBrowser_->clear();
}

void ConfigDialog::onToggleLogsClicked() {
    // Left empty or we can remove the slot. Currently not used as logs are always visible in Diagnostics tab.
}

void ConfigDialog::onIpConfiguratorApplyRequested(const QString& mac, const QString& mode,
                                                  const QString& ip, const QString& mask,
                                                  const QString& gateway) {
    const QString normalizedMac = normalizeMac(mac);
    if (normalizedMac.isEmpty()) {
        ipConfiguratorPanel_->setApplyResult(false, "Invalid MAC address.");
        return;
    }

    // Stop acquisition while the camera network stack is reconfigured.
    const bool wasRunning = cameraManager_ && cameraManager_->isAcquiring();
    if (cameraManager_) {
        cameraManager_->stopAcquisition();
    }

    const IpConfigResult result = CameraManager::configureIpConfiguration(
        normalizedMac.toStdString(), mode.toStdString(),
        ip.toStdString(), mask.toStdString(), gateway.toStdString());

    if (wasRunning && cameraManager_) {
        // stopAcquisition() destroyed the runtime camera objects, so
        // startAcquisition() alone cannot re-attach them. Mirror MainWindow's
        // restart pattern (initialize() + startAcquisition()) to bring the
        // remaining cameras back; the camera that just changed IP is still
        // rebooting and is reconnected via the recovery thread below.
        cameraManager_->initialize();
        cameraManager_->startAcquisition();
    }

    if (result != IpConfigResult::Success) {
        const QString message = (result == IpConfigResult::DeviceNotFound)
            ? "Camera " + mac + " is not currently visible in GigE discovery.\n"
              "Check that it is powered on and connected, then click Refresh and try again."
            : "Failed to apply " + mode + " configuration to camera " + mac + ".\n"
              "Check the connection and that the camera supports this mode.";
        ipConfiguratorPanel_->setApplyResult(false, message);
        return;
    }

    // Match the card to the physical camera (by normalized MAC) so the
    // recovery thread below can reconnect it. Do NOT touch the card's
    // "Configured IP": each card/ID keeps its own fixed IP assignment and is
    // never rewritten by the IP Configurator (the live "Detected IP" row is
    // updated by refreshNetworkStatus() instead).
    CameraCard* matchedCard = nullptr;
    for (CameraCard* card : cameraCards_) {
        if (normalizeMac(card->macAddress()) == normalizedMac) {
            matchedCard = card;
            break;
        }
    }

    // The camera that just changed IP restarts its network stack and stays
    // unreachable for a few seconds, so initialize() above could not attach it.
    // Have the recovery thread reconnect it as soon as it reappears.
    if (wasRunning && cameraManager_ && matchedCard) {
        const std::vector<CameraInfo> cameras = CameraConfig::getCameras();
        for (size_t i = 0; i < cameras.size(); ++i) {
            if (cameras[i].id == matchedCard->cameraId()) {
                cameraManager_->requestCameraReconnect(static_cast<int>(i));
                break;
            }
        }
    }

    currentGigEDevices_ = CameraManager::enumerateGigEDevices(/*forceRefresh=*/true);
    refreshNetworkStatus();
    if (connectionLogsBrowser_) {
        connectionLogsBrowser_->append(QString("[%1] IP config applied: MAC=%2 mode=%3 IP=%4")
            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"), mac, mode, ip));
    }

    QString message = QString("Successfully applied %1 configuration to %2.").arg(mode, mac);
    message += " The camera restarts its network interface automatically and reappears"
               " within a few seconds \u2014 no cable changes needed.";
    if (mode == QStringLiteral("Static")) {
        message += QString(" Expected at %1.").arg(ip);
        if (gateway == QStringLiteral("0.0.0.0")) {
            message += " Note: this camera model keeps its previous gateway in live status"
                       " when 0.0.0.0 is applied (firmware limitation); 0.0.0.0 is stored"
                       " as the persistent setting.";
        }
    }
    ipConfiguratorPanel_->setApplyResult(true, message);

    // The camera disappears from discovery while it restarts its network
    // stack; refresh again shortly so camera cards show the new detected IP.
    QTimer::singleShot(8000, this, [this]() {
        currentGigEDevices_ = CameraManager::enumerateGigEDevices(/*forceRefresh=*/true);
        refreshNetworkStatus();
    });
}

void ConfigDialog::onIpConfiguratorForceIpRequested(const QString& mac, const QString& tempIp,
                                                    const QString& mask, const QString& gateway) {
    const QString normalizedMac = normalizeMac(mac);
    if (normalizedMac.isEmpty() || !isValidIpv4String(tempIp)) {
        ipConfiguratorPanel_->setApplyResult(false, "Invalid MAC address or temporary IP.");
        return;
    }

    // The temporary IP must live on a host subnet so the camera stays
    // reachable; derive a matching netmask when the panel did not provide one.
    QString usedMask = mask.trimmed();
    if (!isValidIpv4String(usedMask)) {
        usedMask = subnetMaskForIp(tempIp);
    }
    const QString usedGateway = isValidIpv4String(gateway.trimmed())
        ? gateway.trimmed() : QStringLiteral("0.0.0.0");

    const bool wasRunning = cameraManager_ && cameraManager_->isAcquiring();
    if (cameraManager_) {
        cameraManager_->stopAcquisition();
    }

    const IpConfigResult result = CameraManager::configureIpConfiguration(
        normalizedMac.toStdString(), "Static",
        tempIp.toStdString(), usedMask.toStdString(), usedGateway.toStdString());

    if (wasRunning && cameraManager_) {
        cameraManager_->initialize();
        cameraManager_->startAcquisition();
    }

    if (result != IpConfigResult::Success) {
        const QString message = (result == IpConfigResult::DeviceNotFound)
            ? "Camera " + mac + " is not currently visible in GigE discovery.\n"
              "Check that it is powered on and connected, then click Refresh and try again."
            : "Failed to assign temporary IP " + tempIp + " to camera " + mac + ".\n"
              "Check the connection and that the camera supports this operation.";
        ipConfiguratorPanel_->setApplyResult(false, message);
        return;
    }

    currentGigEDevices_ = CameraManager::enumerateGigEDevices(/*forceRefresh=*/true);
    refreshNetworkStatus();
    if (connectionLogsBrowser_) {
        connectionLogsBrowser_->append(QString("[%1] Temporary IP assigned: MAC=%2 IP=%3 mask=%4")
            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"), mac, tempIp, usedMask));
    }

    QString message = QString("Temporary IP %1 assigned to %2. The camera restarts its network "
                              "interface and reappears at this address within a few seconds. ")
        .arg(tempIp, mac);
    message += "Then select the camera again and click Apply to set the permanent IP configuration.\n\n"
               "Note: on this camera the temporary address is stored persistently; the final Apply "
               "overwrites it with the permanent configuration.";
    ipConfiguratorPanel_->setApplyResult(true, message);

    QTimer::singleShot(8000, this, [this]() {
        currentGigEDevices_ = CameraManager::enumerateGigEDevices(/*forceRefresh=*/true);
        refreshNetworkStatus();
    });
}

bool ConfigDialog::validateConfiguration(QStringList* errors) const {
    QMap<QString, QList<int>> ipUsage;
    QMap<QString, QList<int>> macUsage;

    for (auto* card : cameraCards_) {
        int source = card->sourceType();
        if (source == 2) continue;

        QString configuredIp = normalizeIp(card->ipAddress());
        if (!configuredIp.isEmpty()) {
            ipUsage[configuredIp].append(card->cameraId());
        }

        if (source == 1) {
            QString configuredMac = normalizeMac(card->macAddress());
            if (configuredMac.isEmpty() || configuredMac == "NONE/AUTO") {
                if (errors) {
                    errors->append(QString("Camera ID %1 is set to Real but has no MAC assigned.").arg(card->cameraId()));
                }
            } else {
                macUsage[configuredMac].append(card->cameraId());
            }
        }
    }

    for (auto it = ipUsage.cbegin(); it != ipUsage.cend(); ++it) {
        if (it.value().size() > 1 && errors) {
            errors->append(QString("Configured IP %1 is assigned to multiple camera IDs (%2).")
                          .arg(it.key(), joinCameraIds(it.value())));
        }
    }

    for (auto it = macUsage.cbegin(); it != macUsage.cend(); ++it) {
        if (it.value().size() > 1 && errors) {
            errors->append(QString("MAC %1 is assigned to multiple camera IDs (%2).")
                          .arg(it.key(), joinCameraIds(it.value())));
        }
    }

    return !errors || errors->isEmpty();
}

void ConfigDialog::refreshNetworkStatus() {
    // Emulation active when the env var is set (docker/CI) or the persisted
    // Camera Mode selector is Emulated. When active, Real cards cannot attach
    // (no real hardware is present) and Emulated cards are the ones that run.
    const bool emulationActive = CameraConfig::isEmulationActive();

    QMap<QString, QList<QString>> liveIpToMacs;
    QMap<QString, GigEDeviceInfo> macToDevice;

    for (const auto& dev : currentGigEDevices_) {
        QString ip = normalizeIp(QString::fromStdString(dev.ipAddress));
        QString mac = normalizeMac(QString::fromStdString(dev.macAddress));
        if (!mac.isEmpty()) {
            macToDevice.insert(mac, dev);
        }
        if (!ip.isEmpty()) {
            liveIpToMacs[ip].append(mac);
        }
    }

    QSet<QString> duplicateConfiguredIps;
    QSet<QString> duplicateConfiguredMacs;
    QMap<QString, int> configuredIpCounts;
    QMap<QString, int> configuredMacCounts;

    for (auto* card : cameraCards_) {
        if (card->sourceType() == 2) continue;

        QString configuredIp = normalizeIp(card->ipAddress());
        if (!configuredIp.isEmpty()) {
            configuredIpCounts[configuredIp] += 1;
        }

        if (card->sourceType() == 1) {
            QString configuredMac = normalizeMac(card->macAddress());
            if (!configuredMac.isEmpty() && configuredMac != "NONE/AUTO") {
                configuredMacCounts[configuredMac] += 1;
            }
        }
    }

    for (auto it = configuredIpCounts.cbegin(); it != configuredIpCounts.cend(); ++it) {
        if (it.value() > 1) {
            duplicateConfiguredIps.insert(it.key());
        }
    }

    for (auto it = configuredMacCounts.cbegin(); it != configuredMacCounts.cend(); ++it) {
        if (it.value() > 1) {
            duplicateConfiguredMacs.insert(it.key());
        }
    }

    int mismatchCount = 0;
    int missingCount = 0;
    int blockingCount = 0;
    bool liveDuplicateSeen = false;
    int onlineCount = 0;
    int warningCount = 0;
    int errorCount = 0;
    int offlineCount = 0;

    for (auto* card : cameraCards_) {
        int source = card->sourceType();
        QString configuredIp = normalizeIp(card->ipAddress());
        QString configuredMac = normalizeMac(card->macAddress());

        QString detectedIp = "Offline";
        QString statusText = "Disabled";
        QColor statusColor("#888888");

        if (source != 2) {
            statusText = "Unassigned MAC";
            statusColor = QColor("#E0A800");

            if (duplicateConfiguredIps.contains(configuredIp)) {
                statusText = "Duplicate IP";
                statusColor = QColor("#FF5A5A");
                blockingCount++;
                errorCount++;
            } else if (source == 1 && (configuredMac.isEmpty() || configuredMac == "NONE/AUTO")) {
                if (emulationActive) {
                    // Emulation is on but this card is Real: there is no real
                    // hardware to attach to, and the MAC requirement is moot.
                    statusText = "Offline - no hardware";
                    statusColor = QColor("#6E7681");
                    missingCount++;
                    offlineCount++;
                } else {
                    missingCount++;
                    warningCount++;
                }
            } else if (source == 1 && duplicateConfiguredMacs.contains(configuredMac)) {
                statusText = "Duplicate MAC";
                statusColor = QColor("#FF5A5A");
                blockingCount++;
                errorCount++;
            } else if (source == 0) {
                if (emulationActive) {
                    statusText = "Emulated";
                    statusColor = QColor("#4CAF50");
                    onlineCount++;
                } else {
                    // Selector is OFF: emulated devices are not created, so a
                    // card set to Emulated cannot activate until it is switched on.
                    statusText = "Offline - emulation off";
                    statusColor = QColor("#6E7681");
                    offlineCount++;
                }
            } else if (macToDevice.contains(configuredMac)) {
                const GigEDeviceInfo& dev = macToDevice[configuredMac];
                detectedIp = QString::fromStdString(dev.ipAddress);
                QString normalizedDetectedIp = normalizeIp(detectedIp);

                if (liveIpToMacs.value(normalizedDetectedIp).size() > 1) {
                    statusText = "Duplicate live IP";
                    statusColor = QColor("#FF5A5A");
                    liveDuplicateSeen = true;
                    blockingCount++;
                    errorCount++;
                } else if (normalizedDetectedIp == configuredIp) {
                    statusText = "Online";
                    statusColor = QColor("#4CAF50");
                    onlineCount++;
                } else {
                    statusText = "IP mismatch";
                    statusColor = QColor("#E0A800");
                    mismatchCount++;
                    warningCount++;
                }
            } else if (source == 1) {
                if (emulationActive) {
                    // Emulation is on but this Real card has no matching real
                    // device - explain the cause instead of a generic Offline.
                    statusText = "Offline - no hardware";
                    statusColor = QColor("#6E7681");
                    missingCount++;
                    offlineCount++;
                } else {
                    statusText = "Offline";
                    statusColor = QColor("#6E7681");
                    missingCount++;
                    offlineCount++;
                }
            }
        } else {
            offlineCount++;
        }

        card->setDetectedIp(detectedIp);
        card->setStatus(statusText, statusColor);
    }

    // Update network summary header
    int totalCount = cameraCards_.size();
    networkSummaryHeader_->setCameraCounts(totalCount, onlineCount, warningCount, errorCount, offlineCount);

    // Vision NIC link speed: use the first configured Real camera's IP to find
    // the interface (all cameras usually share the same vision NIC).
    {
        int linkMbps = -1;
        for (const auto& cam : CameraConfig::getCameras()) {
            if (cam.source == 1 && !cam.ipAddress.isEmpty()) {
                linkMbps = CameraManager::getLinkSpeedMbpsForIp(cam.ipAddress);
                break;
            }
        }
        networkSummaryHeader_->setLinkSpeedMbps(linkMbps);
    }

    // Update summary text
    QStringList summary;
    QColor summaryColor = QColor("#4CAF50");

    if (blockingCount > 0) {
        summaryColor = QColor("#FF5A5A");
        if (!duplicateConfiguredIps.isEmpty()) {
            summary << "Duplicate configured IPs detected";
        }
        if (!duplicateConfiguredMacs.isEmpty()) {
            summary << "Duplicate configured MACs detected";
        }
        if (liveDuplicateSeen) {
            summary << "Duplicate live IP detected";
        }
    }

    if (mismatchCount > 0) {
        if (summaryColor != QColor("#FF5A5A")) {
            summaryColor = QColor("#E0A800");
        }
        summary << QString("%1 camera%2 have IP mismatch").arg(mismatchCount).arg(mismatchCount == 1 ? "" : "s");
    }

    if (missingCount > 0) {
        if (summaryColor == QColor("#4CAF50")) {
            summaryColor = QColor("#E0A800");
        }
        summary << QString("%1 camera%2 not visible").arg(missingCount).arg(missingCount == 1 ? "" : "s");
    }

    if (summary.isEmpty()) {
        networkSummaryHeader_->setNetworkStatus("Network OK: All cameras configured correctly", QColor("#4CAF50"));
    } else {
        networkSummaryHeader_->setNetworkStatus(summary.join(" | "), summaryColor);
    }

    // Keep the read-only Fixed IP registry in sync with live detection.
    refreshFixedIpList();
}
