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
// Push-hold re-evaluation cadence. The per-tag Cooldown (minimumIntervalMs)
// throttles the actual fire rate; this tick only needs to stay responsive.
constexpr int kPushHoldTickMs = 250;

QString normalizedNodeId(const QString& nodeId) {
    return nodeId.trimmed();
}
}

OpcUaClientService::OpcUaClientService(QObject* parent)
    : QObject(parent)
    , settings_(CameraConfig::getDefaultOpcUaSettings()) {
    pushHoldTimer_ = new QTimer(this);
    pushHoldTimer_->setInterval(kPushHoldTickMs);
    connect(pushHoldTimer_, &QTimer::timeout,
            this, &OpcUaClientService::onPushHoldTimerTick);
}

OpcUaClientService::~OpcUaClientService() {
    stop();
}

void OpcUaClientService::setSettings(const OpcUaSettings& settings) {
    // Restart only if the service is already active; otherwise the caller
    // (MainWindow::applyOpcUaSettings) drives start() once, which keeps
    // simulated-tag synthesis from firing twice per apply.
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
    // Keep the fixed simulated speed sample available for manual/Live triggers.
    synthesizeSimulatedSpeed();

    // Push-hold: while a tag is held (a Live tag reading True, or a manual
    // button pressed), a periodic timer re-evaluates it and repeats the trigger
    // per Cooldown.
    if (hasRealMonitoredTags() || hasSimulatedTags()) {
        pushHoldTimer_->start();
        onPushHoldTimerTick();
    }

    if (hasRealMonitoredTags()) {
        connectClient();
    } else if (hasSimulatedTags()) {
        emitStatus(QStringLiteral("OPC UA: all enabled tags are simulated — server connection skipped."));
    } else {
        emitStatus(QStringLiteral("OPC UA enabled, but no tags are configured."));
    }
}

void OpcUaClientService::stop() {
    shouldReconnect_ = false;
    connectAttemptInFlight_ = false;
    if (pushHoldTimer_) {
        pushHoldTimer_->stop();
    }
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
    emit runtimeStatusChanged(currentRuntimeStatus());
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
        // Real subscriptions are re-established on reconnect; keep the fixed
        // simulated speed sample available as well.
        synthesizeSimulatedSpeed();
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
            state.hasLastValue = true;
            state.lastValue = value;
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
    {
        QMutexLocker locker(&speedMutex_);
        latestSpeed_ = LatestSpeedSample{};
    }
    for (TriggerMonitorState& state : triggerStates_) {
        state.hasLastValue = false;
        state.lastValue = QVariant();
        state.lastTriggeredMs = 0;
    }
    manualTriggerHeld_.clear();
    manualLastFiredMs_.clear();
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

    for (int i = 0; i < static_cast<int>(settings_.triggerTags.size()); ++i) {
        const OpcUaTriggerTagSettings& triggerSettings = settings_.triggerTags[static_cast<size_t>(i)];
        if (triggerSettings.enabled && !triggerSettings.simulated
                && !normalizedNodeId(triggerSettings.nodeId).isEmpty()) {
            monitorTriggerTag(triggerSettings, i);
        }
    }

    if (settings_.speedTag.enabled && !settings_.speedTag.simulated
            && !normalizedNodeId(settings_.speedTag.nodeId).isEmpty()) {
        monitorSpeedTag(settings_.speedTag);
    }

    if (triggerStates_.isEmpty() && !speedNode_) {
        emitStatus(hasSimulatedTags()
            ? QStringLiteral("OPC UA connected; live subscriptions failed, but simulated tags are active.")
            : QStringLiteral("OPC UA connected, but no monitored tags are configured successfully."));
    }
}

void OpcUaClientService::monitorTriggerTag(const OpcUaTriggerTagSettings& triggerSettings, int tagIndex) {
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
    state.tagIndex = tagIndex;
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

bool OpcUaClientService::hasRealMonitoredTags() const {
    for (const auto& triggerSettings : settings_.triggerTags) {
        if (triggerSettings.enabled && !triggerSettings.simulated
                && !normalizedNodeId(triggerSettings.nodeId).isEmpty()) {
            return true;
        }
    }
    return settings_.speedTag.enabled && !settings_.speedTag.simulated
        && !normalizedNodeId(settings_.speedTag.nodeId).isEmpty();
}

bool OpcUaClientService::hasSimulatedTags() const {
    for (const auto& triggerSettings : settings_.triggerTags) {
        if (triggerSettings.enabled && triggerSettings.simulated) {
            return true;
        }
    }
    return settings_.speedTag.enabled && settings_.speedTag.simulated;
}

void OpcUaClientService::setManualTriggerHeld(int tagIndex, bool held,
                                              const OpcUaTriggerTagSettings& tagSettings) {
    if (tagIndex < 0) {
        return;
    }
    if (held) {
        // Use the UI row's live config (not the possibly-stale service copy), so
        // the button works even before the OPC UA settings are saved.
        manualTriggerHeld_[tagIndex] = tagSettings;
        // Make sure a configured simulated speed is available even if the OPC UA
        // service was never started (the button is a standalone test trigger).
        synthesizeSimulatedSpeed();
        // First fire is immediate; the timer repeats it while held.
        dispatchManualTrigger(tagSettings, tagIndex);
        if (pushHoldTimer_ && !pushHoldTimer_->isActive()) {
            pushHoldTimer_->start();
        }
    } else {
        manualTriggerHeld_.remove(tagIndex);
    }
}

bool OpcUaClientService::currentSpeedMperMin(double* mPerMin) const {
    if (!mPerMin) {
        return false;
    }
    QMutexLocker locker(&speedMutex_);
    if (!latestSpeed_.valid) {
        return false;
    }
    // Refuse to align against a speed sample that has gone stale: a dead speed
    // would produce wrong frame offsets across the camera group.
    if ((QDateTime::currentMSecsSinceEpoch() - latestSpeed_.receivedAtMs)
            > settings_.speedTag.staleTimeoutMs) {
        return false;
    }
    *mPerMin = latestSpeed_.value;
    return true;
}

QString OpcUaClientService::speedUnit() const {
    return settings_.speedTag.unit;
}

void OpcUaClientService::refreshSimulatedSpeed() {
    synthesizeSimulatedSpeed();
}

void OpcUaClientService::releaseAllManualTriggers() {
    manualTriggerHeld_.clear();
    manualLastFiredMs_.clear();
    // The next tick (or the parking check below) will stop the timer if nothing
    // else needs it.
    if (pushHoldTimer_ && !settings_.enabled && triggerStates_.isEmpty()) {
        pushHoldTimer_->stop();
    }
}

void OpcUaClientService::dispatchManualTrigger(const OpcUaTriggerTagSettings& tagSettings, int tagIndex) {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const auto it = manualLastFiredMs_.constFind(tagIndex);
    if (tagSettings.minimumIntervalMs > 0 && it != manualLastFiredMs_.constEnd()
            && it.value() > 0 && (nowMs - it.value()) < tagSettings.minimumIntervalMs) {
        return;
    }
    manualLastFiredMs_[tagIndex] = nowMs;
    dispatchTriggerFor(tagSettings);
}

void OpcUaClientService::onPushHoldTimerTick() {
    // Keep the simulated Machine Speed sample fresh: synthesizing only at
    // start/connect lets it go stale after staleTimeoutMs, so later manual/Live
    // triggers would capture no speed. The simulated value is a fixed constant,
    // so re-publishing it is harmless. No-op unless enabled && simulated.
    synthesizeSimulatedSpeed();
    // Real tags: the server only pushes value changes, so re-evaluate the cached
    // value each tick to keep firing while the tag stays True (push-hold).
    // Warnings are suppressed here: the value is unchanged since its last push,
    // and the attribute-update path already reported any problem once.
    for (TriggerMonitorState& state : triggerStates_) {
        if (state.hasLastValue) {
            processTriggerValue(state, state.lastValue, false);
        }
    }
    // Manual push-hold buttons: repeat while held using the row's config
    // snapshot captured at press time.
    for (auto it = manualTriggerHeld_.cbegin(); it != manualTriggerHeld_.cend(); ++it) {
        if (it.key() >= 0) {
            dispatchManualTrigger(it.value(), it.key());
        }
    }
    emit runtimeStatusChanged(currentRuntimeStatus());

    // When nothing is being re-evaluated (no real subscriptions, no held
    // buttons) and the service isn't otherwise active, park the timer. This
    // covers a push-hold button pressed while OPC UA was never started.
    if (!settings_.enabled && manualTriggerHeld_.isEmpty() && triggerStates_.isEmpty()) {
        pushHoldTimer_->stop();
    }
}

void OpcUaClientService::synthesizeSimulatedSpeed() {
    if (!settings_.speedTag.enabled || !settings_.speedTag.simulated) {
        return;
    }
    processSpeedValue(QVariant(settings_.speedTag.simulatedValue));
}

OpcUaRuntimeStatus OpcUaClientService::currentRuntimeStatus() const {
    OpcUaRuntimeStatus status;
    status.serviceRunning = pushHoldTimer_ && pushHoldTimer_->isActive();

    if (!settings_.enabled) {
        status.clientStateText = QStringLiteral("Idle (OPC UA disabled)");
    } else if (client_ && client_->state() == QOpcUaClient::Connected) {
        status.clientConnected = true;
        status.clientStateText = QStringLiteral("Connected: %1").arg(settings_.endpointUrl.trimmed());
    } else if (client_ && client_->state() == QOpcUaClient::Connecting) {
        status.connecting = true;
        status.clientStateText = QStringLiteral("Connecting…");
    } else if (hasSimulatedTags() && !hasRealMonitoredTags()) {
        // All configured tags are simulated: the connection was deliberately
        // skipped, so "reconnecting" would be misleading.
        status.clientStateText = QStringLiteral("Simulated only (no server)");
    } else if (shouldReconnect_) {
        status.clientStateText = QStringLiteral("Disconnected (reconnecting)");
    } else {
        status.clientStateText = QStringLiteral("Disconnected");
    }

    {
        QMutexLocker locker(&speedMutex_);
        if (latestSpeed_.valid) {
            status.speedValid = true;
            status.speedText = QString::number(latestSpeed_.value, 'f', 2)
                + QStringLiteral(" ") + latestSpeed_.unit;
            status.speedStale = (QDateTime::currentMSecsSinceEpoch() - latestSpeed_.receivedAtMs)
                > settings_.speedTag.staleTimeoutMs;
        } else {
            status.speedText = QStringLiteral("—");
        }
    }

    const int tagCount = static_cast<int>(settings_.triggerTags.size());
    status.tags.reserve(tagCount);
    for (int i = 0; i < tagCount; ++i) {
        const OpcUaTriggerTagSettings& tag = settings_.triggerTags[static_cast<size_t>(i)];
        OpcUaTagRuntimeStatus tagStatus;
        tagStatus.tagIndex = i;
        tagStatus.name = tag.name;
        tagStatus.nodeId = normalizedNodeId(tag.nodeId);
        tagStatus.simulated = tag.simulated;
        tagStatus.enabled = tag.enabled;
        const auto holdIt = manualTriggerHeld_.constFind(i);
        tagStatus.held = holdIt != manualTriggerHeld_.constEnd();
        tagStatus.valueText = QStringLiteral("—");

        const auto lastFireIt = manualLastFiredMs_.constFind(i);
        if (lastFireIt != manualLastFiredMs_.constEnd()) {
            tagStatus.lastFiredMs = lastFireIt.value();
        }

        if (tagStatus.held) {
            // A held manual button drives the input True regardless of whether
            // the service's copy of the tag is enabled/simulated yet.
            tagStatus.active = true;
            tagStatus.valueText = QStringLiteral("True");
        } else if (tag.simulated) {
            tagStatus.active = false;
        } else {
            for (const TriggerMonitorState& state : triggerStates_) {
                if (state.tagIndex == i && state.hasLastValue) {
                    bool asBool = false;
                    if (extractBooleanValue(state.lastValue, &asBool)) {
                        // Match the firing logic: bool AND integer values count.
                        tagStatus.active = asBool;
                        tagStatus.valueText = asBool
                            ? QStringLiteral("True") : QStringLiteral("False");
                    } else {
                        tagStatus.valueText = state.lastValue.toString();
                    }
                    if (state.lastTriggeredMs > 0) {
                        tagStatus.lastFiredMs = state.lastTriggeredMs;
                    }
                    break;
                }
            }
        }
        status.tags.append(tagStatus);
    }
    return status;
}

void OpcUaClientService::emitStatus(const QString& message) {
    emit statusChanged(message);
}

void OpcUaClientService::dispatchTriggerEvent(const TriggerEvent& event) {
    emit triggerReceived(event);
}

void OpcUaClientService::processTriggerValue(TriggerMonitorState& state, const QVariant& value, bool emitWarnings) {
    bool rawValue = false;
    if (!extractBooleanValue(value, &rawValue)) {
        if (emitWarnings) {
            emitStatus(QStringLiteral("OPC UA trigger tag '%1' published a non-boolean value.").arg(state.settings.name));
        }
        return;
    }

    // Push-hold: fire while the Live tag reads True, repeating every Cooldown ms
    // while it stays True. Releasing (False) stops it.
    if (!rawValue) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (state.settings.minimumIntervalMs > 0
            && state.lastTriggeredMs > 0
            && (nowMs - state.lastTriggeredMs) < state.settings.minimumIntervalMs) {
        return;
    }
    state.lastTriggeredMs = nowMs;

    dispatchTriggerFor(state.settings);
}

void OpcUaClientService::dispatchTriggerFor(const OpcUaTriggerTagSettings& tagSettings) {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    TriggerEvent triggerEvent;
    triggerEvent.tagName = tagSettings.name;
    triggerEvent.nodeId = normalizedNodeId(tagSettings.nodeId);
    triggerEvent.positionDirectionSign = settings_.positionDirectionSign >= 0 ? 1 : -1;
    triggerEvent.group = tagSettings.group;
    triggerEvent.positionMm = tagSettings.positionMm;

    {
        QMutexLocker locker(&speedMutex_);
        if (latestSpeed_.valid) {
            triggerEvent.speedTagName = latestSpeed_.tagName;
            triggerEvent.speedTagNodeId = latestSpeed_.nodeId;
            triggerEvent.speedUnit = latestSpeed_.unit;
            triggerEvent.speedSampleTimeUtc = latestSpeed_.sampleTimeUtc;
            triggerEvent.speedValue = latestSpeed_.value;
            triggerEvent.hasSpeed = true;
            triggerEvent.speedStale = (nowMs - latestSpeed_.receivedAtMs) > settings_.speedTag.staleTimeoutMs;
        }
    }

    dispatchTriggerEvent(triggerEvent);
}

void OpcUaClientService::processSpeedValue(const QVariant& value) {
    double rawValue = 0.0;
    if (!extractNumericValue(value, &rawValue)) {
        emitStatus(QStringLiteral("OPC UA speed tag '%1' published a non-numeric value.").arg(settings_.speedTag.name));
        return;
    }

    {
        QMutexLocker locker(&speedMutex_);
        latestSpeed_.valid = true;
        latestSpeed_.tagName = settings_.speedTag.name;
        latestSpeed_.nodeId = normalizedNodeId(settings_.speedTag.nodeId);
        latestSpeed_.unit = settings_.speedTag.unit;
        latestSpeed_.value = (rawValue * settings_.speedTag.scale) + settings_.speedTag.offset;
        latestSpeed_.sampleTimeUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        latestSpeed_.receivedAtMs = QDateTime::currentMSecsSinceEpoch();
    }
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
