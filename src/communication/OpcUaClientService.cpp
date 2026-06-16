#include "OpcUaClientService.h"

#include <QDateTime>
#include <QMetaEnum>
#include <QOpcUaAuthenticationInformation>
#include <QOpcUaClient>
#include <QOpcUaMonitoringParameters>
#include <QOpcUaNode>
#include <QOpcUaProvider>
#include <QTimer>

namespace {
constexpr int kOpcUaPort = 4840;

QString normalizedNodeId(const QString& nodeId) {
    return nodeId.trimmed();
}
}

OpcUaClientService::OpcUaClientService(QObject* parent)
    : QObject(parent)
    , settings_(CameraConfig::getDefaultOpcUaSettings()) {
}

OpcUaClientService::~OpcUaClientService() {
    stop();
}

void OpcUaClientService::setSettings(const OpcUaSettings& settings) {
    const bool shouldRestart = isRunning();
    settings_ = settings;
    if (shouldRestart) {
        stop();
        start();
    }
}

void OpcUaClientService::start() {
    if (client_ || connectAttemptInFlight_ || !settings_.enabled) {
        return;
    }

    shouldReconnect_ = true;
    connectClient();
}

void OpcUaClientService::stop() {
    shouldReconnect_ = false;
    connectAttemptInFlight_ = false;
    clearMonitoredNodes();

    if (client_) {
        disconnect(client_, nullptr, this, nullptr);
        if (client_->state() != QOpcUaClient::Disconnected) {
            client_->disconnectFromEndpoint();
        }
        client_->deleteLater();
        client_ = nullptr;
    }

    resetRuntimeState();
}

bool OpcUaClientService::isRunning() const {
    return client_ || connectAttemptInFlight_;
}

void OpcUaClientService::connectClient() {
    if (!shouldReconnect_ || client_ || connectAttemptInFlight_ || !settings_.enabled) {
        return;
    }

    QOpcUaProvider provider;
    const QString backend = backendName();
    if (backend.isEmpty()) {
        emitStatus(QStringLiteral("Qt OPC UA backend 'open62541' is not available. Install the Qt OPC UA open62541 plugin."));
        return;
    }

    client_ = provider.createClient(backend);
    if (!client_) {
        emitStatus(QStringLiteral("Failed to create Qt OPC UA client for backend '%1'.").arg(backend));
        return;
    }

    connectAttemptInFlight_ = true;
    resetRuntimeState();

    connect(client_, &QOpcUaClient::stateChanged, this, &OpcUaClientService::handleClientStateChanged);
    connect(client_, &QOpcUaClient::errorChanged, this, &OpcUaClientService::handleClientErrorChanged);

    QOpcUaEndpointDescription endpoint;
    endpoint.setEndpointUrl(settings_.endpointUrl.trimmed());
    endpoint.setSecurityPolicy(QStringLiteral("http://opcfoundation.org/UA/SecurityPolicy#None"));
    endpoint.setSecurityMode(QOpcUaEndpointDescription::MessageSecurityMode::None);

    QOpcUaAuthenticationInformation authentication;
    if (settings_.useUsernamePassword) {
        authentication.setUsernameAuthentication(settings_.username, settings_.password);
    } else {
        authentication.setAnonymousAuthentication();
    }
    client_->setAuthenticationInformation(authentication);
    client_->connectToEndpoint(endpoint);
}

void OpcUaClientService::reconnectLater() {
    if (!shouldReconnect_ || settings_.reconnectIntervalMs < 0) {
        return;
    }

    QTimer::singleShot(settings_.reconnectIntervalMs, this, &OpcUaClientService::connectClient);
}

void OpcUaClientService::handleClientStateChanged(QOpcUaClient::ClientState state) {
    switch (state) {
    case QOpcUaClient::Connected:
        connectAttemptInFlight_ = false;
        emitStatus(QStringLiteral("OPC UA connected: %1").arg(settings_.endpointUrl.trimmed()));
        monitorConfiguredNodes();
        break;
    case QOpcUaClient::Connecting:
        emitStatus(QStringLiteral("Connecting to OPC UA server: %1").arg(settings_.endpointUrl.trimmed()));
        break;
    case QOpcUaClient::Closing:
        emitStatus(QStringLiteral("Closing OPC UA connection."));
        break;
    case QOpcUaClient::Disconnected: {
        const bool hadClient = client_ != nullptr;
        connectAttemptInFlight_ = false;
        clearMonitoredNodes();
        resetRuntimeState();
        if (hadClient && shouldReconnect_) {
            emitStatus(QStringLiteral("OPC UA disconnected. Reconnecting..."));
            client_->deleteLater();
            client_ = nullptr;
            reconnectLater();
        } else if (hadClient) {
            client_->deleteLater();
            client_ = nullptr;
        }
        break;
    }
    }
}

void OpcUaClientService::handleClientErrorChanged(QOpcUaClient::ClientError error) {
    if (error == QOpcUaClient::NoError) {
        return;
    }
    emitStatus(QStringLiteral("OPC UA error: %1").arg(clientErrorText(error)));
}

void OpcUaClientService::handleTriggerValueChanged(const QVariant& value) {
    auto* node = qobject_cast<QOpcUaNode*>(sender());
    if (!node) {
        return;
    }

    const QString nodeId = normalizedNodeId(node->nodeId());
    for (TriggerMonitorState& state : triggerStates_) {
        if (state.node && normalizedNodeId(state.node->nodeId()) == nodeId) {
            processTriggerValue(state, value);
            return;
        }
    }
}

void OpcUaClientService::handleSpeedValueChanged(const QVariant& value) {
    processSpeedValue(value);
}

void OpcUaClientService::handleAttributeUpdated(QOpcUa::NodeAttribute attribute, const QVariant& value) {
    if (attribute != QOpcUa::NodeAttribute::Value) {
        return;
    }

    auto* node = qobject_cast<QOpcUaNode*>(sender());
    if (!node) {
        return;
    }

    if (speedNode_ && node == speedNode_) {
        handleSpeedValueChanged(value);
        return;
    }

    handleTriggerValueChanged(value);
}

void OpcUaClientService::resetRuntimeState() {
    latestSpeed_ = LatestSpeedSample{};
    for (TriggerMonitorState& state : triggerStates_) {
        state.hasPreviousValue = false;
        state.previousActive = false;
        state.lastTriggeredMs = 0;
    }
}

void OpcUaClientService::clearMonitoredNodes() {
    for (TriggerMonitorState& state : triggerStates_) {
        if (state.node) {
            disconnect(state.node, nullptr, this, nullptr);
            state.node->deleteLater();
            state.node = nullptr;
        }
    }
    triggerStates_.clear();

    if (speedNode_) {
        disconnect(speedNode_, nullptr, this, nullptr);
        speedNode_->deleteLater();
        speedNode_ = nullptr;
    }
}

void OpcUaClientService::monitorConfiguredNodes() {
    clearMonitoredNodes();

    for (const auto& triggerSettings : settings_.triggerTags) {
        if (triggerSettings.enabled && !normalizedNodeId(triggerSettings.nodeId).isEmpty()) {
            monitorTriggerTag(triggerSettings);
        }
    }

    if (settings_.speedTag.enabled && !normalizedNodeId(settings_.speedTag.nodeId).isEmpty()) {
        monitorSpeedTag(settings_.speedTag);
    }

    if (triggerStates_.isEmpty() && !speedNode_) {
        emitStatus(QStringLiteral("OPC UA connected, but no monitored tags are configured successfully."));
    }
}

void OpcUaClientService::monitorTriggerTag(const OpcUaTriggerTagSettings& triggerSettings) {
    if (!client_) {
        return;
    }

    QOpcUaNode* node = client_->node(normalizedNodeId(triggerSettings.nodeId));
    if (!node) {
        emitStatus(QStringLiteral("OPC UA trigger subscription failed for '%1': invalid NodeId.").arg(triggerSettings.name));
        return;
    }

    TriggerMonitorState state;
    state.settings = triggerSettings;
    state.node = node;
    triggerStates_.append(state);
    TriggerMonitorState& storedState = triggerStates_.last();

    connect(node, &QOpcUaNode::attributeUpdated, this, &OpcUaClientService::handleAttributeUpdated);
    if (!node->enableMonitoring(QOpcUa::NodeAttribute::Value, QOpcUaMonitoringParameters(settings_.publishIntervalMs))) {
        emitStatus(QStringLiteral("OPC UA trigger subscription failed for '%1'.").arg(triggerSettings.name));
        disconnect(node, nullptr, this, nullptr);
        node->deleteLater();
        triggerStates_.removeLast();
    }
}

void OpcUaClientService::monitorSpeedTag(const OpcUaSpeedTagSettings& speedSettings) {
    if (!client_) {
        return;
    }

    QOpcUaNode* node = client_->node(normalizedNodeId(speedSettings.nodeId));
    if (!node) {
        emitStatus(QStringLiteral("OPC UA speed subscription failed for '%1': invalid NodeId.").arg(speedSettings.name));
        return;
    }

    speedNode_ = node;
    connect(node, &QOpcUaNode::attributeUpdated, this, &OpcUaClientService::handleAttributeUpdated);
    if (!node->enableMonitoring(QOpcUa::NodeAttribute::Value, QOpcUaMonitoringParameters(settings_.publishIntervalMs))) {
        emitStatus(QStringLiteral("OPC UA speed subscription failed for '%1'.").arg(speedSettings.name));
        disconnect(node, nullptr, this, nullptr);
        node->deleteLater();
        speedNode_ = nullptr;
    }
}

void OpcUaClientService::emitStatus(const QString& message) {
    emit statusChanged(message);
}

void OpcUaClientService::dispatchTriggerEvent(const TriggerEvent& event) {
    emit triggerReceived(event);
}

void OpcUaClientService::processTriggerValue(TriggerMonitorState& state, const QVariant& value) {
    bool rawValue = false;
    if (!extractBooleanValue(value, &rawValue)) {
        emitStatus(QStringLiteral("OPC UA trigger tag '%1' published a non-boolean value.").arg(state.settings.name));
        return;
    }

    const bool active = (rawValue == state.settings.activeState);
    if (!state.hasPreviousValue) {
        state.hasPreviousValue = true;
        state.previousActive = active;
        return;
    }

    bool shouldFire = false;
    const QString edgeMode = state.settings.edgeMode.trimmed().toLower();
    if (edgeMode == QStringLiteral("falling")) {
        shouldFire = state.previousActive && !active;
    } else if (edgeMode == QStringLiteral("either")) {
        shouldFire = state.previousActive != active;
    } else {
        shouldFire = !state.previousActive && active;
    }
    state.previousActive = active;

    if (!shouldFire) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (state.settings.minimumIntervalMs > 0
            && state.lastTriggeredMs > 0
            && (nowMs - state.lastTriggeredMs) < state.settings.minimumIntervalMs) {
        return;
    }
    state.lastTriggeredMs = nowMs;

    TriggerEvent triggerEvent;
    triggerEvent.tagName = state.settings.name;
    triggerEvent.nodeId = normalizedNodeId(state.settings.nodeId);
    triggerEvent.positionDirectionSign = settings_.positionDirectionSign >= 0 ? 1 : -1;

    if (latestSpeed_.valid) {
        triggerEvent.speedTagName = latestSpeed_.tagName;
        triggerEvent.speedTagNodeId = latestSpeed_.nodeId;
        triggerEvent.speedUnit = latestSpeed_.unit;
        triggerEvent.speedSampleTimeUtc = latestSpeed_.sampleTimeUtc;
        triggerEvent.speedValue = latestSpeed_.value;
        triggerEvent.hasSpeed = true;
        triggerEvent.speedStale = (nowMs - latestSpeed_.receivedAtMs) > settings_.speedTag.staleTimeoutMs;
    }

    dispatchTriggerEvent(triggerEvent);
}

void OpcUaClientService::processSpeedValue(const QVariant& value) {
    double rawValue = 0.0;
    if (!extractNumericValue(value, &rawValue)) {
        emitStatus(QStringLiteral("OPC UA speed tag '%1' published a non-numeric value.").arg(settings_.speedTag.name));
        return;
    }

    latestSpeed_.valid = true;
    latestSpeed_.tagName = settings_.speedTag.name;
    latestSpeed_.nodeId = normalizedNodeId(settings_.speedTag.nodeId);
    latestSpeed_.unit = settings_.speedTag.unit;
    latestSpeed_.value = (rawValue * settings_.speedTag.scale) + settings_.speedTag.offset;
    latestSpeed_.sampleTimeUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    latestSpeed_.receivedAtMs = QDateTime::currentMSecsSinceEpoch();
}

bool OpcUaClientService::extractBooleanValue(const QVariant& value, bool* result) const {
    if (!result || !value.isValid() || value.isNull()) {
        return false;
    }

    switch (value.type()) {
    case QMetaType::Bool:
        *result = value.toBool();
        return true;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::SChar:
    case QMetaType::UChar:
        *result = value.toLongLong() != 0;
        return true;
    default:
        return false;
    }
}

bool OpcUaClientService::extractNumericValue(const QVariant& value, double* result) const {
    if (!result || !value.isValid() || value.isNull()) {
        return false;
    }

    bool ok = false;
    const double converted = value.toDouble(&ok);
    if (!ok) {
        return false;
    }

    *result = converted;
    return true;
}

QString OpcUaClientService::clientErrorText(QOpcUaClient::ClientError error) const {
    const QMetaEnum metaEnum = QMetaEnum::fromType<QOpcUaClient::ClientError>();
    const char* key = metaEnum.valueToKey(error);
    return key ? QString::fromLatin1(key) : QStringLiteral("UnknownError");
}

QString OpcUaClientService::backendName() const {
    QOpcUaProvider provider;
    const QStringList backends = provider.availableBackends();
    for (const QString& backend : backends) {
        if (backend.compare(QStringLiteral("open62541"), Qt::CaseInsensitive) == 0) {
            return backend;
        }
    }
    return QString();
}
