#include "OpcUaClientService.h"

#include <QDateTime>
#include <QOpcUaNode>
#include <QTimer>

OpcUaClientService::OpcUaClientService(QObject* parent)
    : QObject(parent)
    , settings_(CameraConfig::getDefaultOpcUaSettings())
{
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);
    connect(reconnectTimer_, &QTimer::timeout, this, &OpcUaClientService::connectToServer);
}

OpcUaClientService::~OpcUaClientService() {
    stop();
}

void OpcUaClientService::setSettings(const OpcUaSettings& settings) {
    settings_ = settings;
}

bool OpcUaClientService::isRunning() const {
    return running_;
}

void OpcUaClientService::start() {
    if (running_ || !settings_.enabled) return;
    running_ = true;

    if (!provider_) {
        provider_ = new QOpcUaProvider(this);
    }
    connectToServer();
}

void OpcUaClientService::stop() {
    running_ = false;
    reconnectTimer_->stop();

    // Clean up nodes
    for (auto& state : triggerStates_) {
        if (state.node) { state.node->deleteLater(); state.node = nullptr; }
    }
    triggerStates_.clear();
    if (speedNode_) { speedNode_->deleteLater(); speedNode_ = nullptr; }

    if (client_) {
        client_->disconnectFromEndpoint();
        client_->deleteLater();
        client_ = nullptr;
    }
    latestSpeed_ = {};
}

void OpcUaClientService::connectToServer() {
    if (!running_) return;

    if (client_) {
        client_->deleteLater();
        client_ = nullptr;
    }

    const QStringList backends = QOpcUaProvider::availableBackends();
    if (backends.isEmpty()) {
        emit statusChanged(QStringLiteral("OPC UA: no backend available (install qt-opcua open62541 plugin)"));
        return;
    }
    // Prefer open62541
    const QString backend = backends.contains(QStringLiteral("open62541"))
        ? QStringLiteral("open62541") : backends.first();

    client_ = provider_->createClient(backend);
    if (!client_) {
        emit statusChanged(QString("OPC UA: failed to create client with backend '%1'").arg(backend));
        return;
    }
    client_->setParent(this);

    connect(client_, &QOpcUaClient::connected,    this, &OpcUaClientService::onConnected);
    connect(client_, &QOpcUaClient::disconnected, this, &OpcUaClientService::onDisconnected);
    connect(client_, &QOpcUaClient::errorOccurred, this, &OpcUaClientService::onErrorOccurred);
    connect(client_, &QOpcUaClient::stateChanged,  this, &OpcUaClientService::onStateChanged);

    if (settings_.useUsernamePassword) {
        QOpcUaAuthenticationInformation auth;
        auth.setUsernameAuthentication(settings_.username, settings_.password);
        client_->setAuthenticationInformation(auth);
    }

    client_->connectToEndpoint(QUrl(settings_.endpointUrl));
}

void OpcUaClientService::onConnected() {
    emit statusChanged(QString("OPC UA connected: %1").arg(settings_.endpointUrl));
    subscribeNodes();
}

void OpcUaClientService::onDisconnected() {
    emit statusChanged(QStringLiteral("OPC UA disconnected"));
    for (auto& state : triggerStates_) {
        if (state.node) { state.node->deleteLater(); state.node = nullptr; }
    }
    triggerStates_.clear();
    if (speedNode_) { speedNode_->deleteLater(); speedNode_ = nullptr; }
    latestSpeed_ = {};
    scheduleReconnect();
}

void OpcUaClientService::onErrorOccurred(QOpcUaClient::ClientError error) {
    emit statusChanged(QString("OPC UA error: %1").arg(static_cast<int>(error)));
}

void OpcUaClientService::onStateChanged(QOpcUaClient::ClientState state) {
    if (state == QOpcUaClient::Disconnected && running_) {
        scheduleReconnect();
    }
}

void OpcUaClientService::scheduleReconnect() {
    if (running_ && !reconnectTimer_->isActive()) {
        reconnectTimer_->start(settings_.reconnectIntervalMs);
    }
}

QOpcUaNode* OpcUaClientService::createSubscribedNode(const QString& nodeId) {
    QOpcUaNode* node = client_->node(nodeId);
    if (!node) return nullptr;

    QOpcUaMonitoringParameters params(settings_.publishIntervalMs);
    node->enableMonitoring(QOpcUa::NodeAttribute::Value, params);
    return node;
}

void OpcUaClientService::subscribeNodes() {
    triggerStates_.clear();

    for (const auto& tagSettings : settings_.triggerTags) {
        if (!tagSettings.enabled || tagSettings.nodeId.trimmed().isEmpty()) continue;

        TriggerState state;
        state.settings = tagSettings;
        state.node = createSubscribedNode(tagSettings.nodeId);
        if (!state.node) {
            emit statusChanged(QString("OPC UA: failed to create node for trigger '%1'").arg(tagSettings.name));
            continue;
        }
        connect(state.node, &QOpcUaNode::attributeUpdated,
                this, &OpcUaClientService::onNodeValueChanged);
        triggerStates_.append(std::move(state));
    }

    if (settings_.speedTag.enabled && !settings_.speedTag.nodeId.trimmed().isEmpty()) {
        speedNode_ = createSubscribedNode(settings_.speedTag.nodeId);
        if (speedNode_) {
            connect(speedNode_, &QOpcUaNode::attributeUpdated,
                    this, &OpcUaClientService::onNodeValueChanged);
        }
    }

    if (triggerStates_.isEmpty() && !speedNode_) {
        emit statusChanged(QStringLiteral("OPC UA connected, but no monitored tags configured."));
    }
}

void OpcUaClientService::onNodeValueChanged(QOpcUa::Types /*type*/, const QVariant& value,
                                             const QDateTime& /*serverTimestamp*/,
                                             QOpcUa::UaStatusCode statusCode)
{
    if (statusCode != QOpcUa::UaStatusCode::Good) return;

    QOpcUaNode* senderNode = qobject_cast<QOpcUaNode*>(sender());
    if (!senderNode) return;

    // Speed node
    if (senderNode == speedNode_) {
        bool ok = false;
        const double raw = value.toDouble(&ok);
        if (!ok) return;
        latestSpeed_.valid = true;
        latestSpeed_.value = (raw * settings_.speedTag.scale) + settings_.speedTag.offset;
        latestSpeed_.sampleTimeUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        latestSpeed_.receivedAtMs = QDateTime::currentMSecsSinceEpoch();
        return;
    }

    // Trigger node
    for (auto& state : triggerStates_) {
        if (state.node != senderNode) continue;

        const bool raw = value.toBool();
        const bool active = (raw == state.settings.activeState);

        if (!state.hasPrev) {
            state.hasPrev = true;
            state.prevActive = active;
            return;
        }

        const QString edgeMode = state.settings.edgeMode.trimmed().toLower();
        bool shouldFire = false;
        if (edgeMode == QLatin1String("falling"))
            shouldFire = state.prevActive && !active;
        else if (edgeMode == QLatin1String("either"))
            shouldFire = state.prevActive != active;
        else
            shouldFire = !state.prevActive && active;

        state.prevActive = active;
        if (!shouldFire) return;

        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (state.settings.minimumIntervalMs > 0 && state.lastFiredMs > 0
                && (nowMs - state.lastFiredMs) < state.settings.minimumIntervalMs) return;
        state.lastFiredMs = nowMs;

        TriggerEvent ev;
        ev.tagName = state.settings.name;
        ev.nodeId  = state.settings.nodeId;
        ev.positionDirectionSign = settings_.positionDirectionSign >= 0 ? 1 : -1;

        if (latestSpeed_.valid) {
            ev.speedTagName      = settings_.speedTag.name;
            ev.speedTagNodeId    = settings_.speedTag.nodeId;
            ev.speedUnit         = settings_.speedTag.unit;
            ev.speedSampleTimeUtc = latestSpeed_.sampleTimeUtc;
            ev.speedValue        = latestSpeed_.value;
            ev.hasSpeed          = true;
            ev.speedStale        = (nowMs - latestSpeed_.receivedAtMs) > settings_.speedTag.staleTimeoutMs;
        }

        emit triggerFired(ev);
        return;
    }
}
