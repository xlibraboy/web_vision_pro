#include "OpcUaClientService.h"

#include <QByteArray>
#include <QDateTime>
#include <chrono>
#include <memory>
#include <vector>

extern "C" {
#include <open62541.h>
}

namespace {
struct TriggerMonitorState {
    OpcUaTriggerTagSettings settings;
    bool hasPreviousValue = false;
    bool previousActive = false;
    qint64 lastTriggeredMs = 0;
};

struct LatestSpeedSample {
    bool valid = false;
    QString tagName;
    QString nodeId;
    QString unit;
    QString sampleTimeUtc;
    double value = 0.0;
    qint64 receivedAtMs = 0;
};

struct MonitorCallbackContext {
    OpcUaClientService* service = nullptr;
    enum class Kind {
        Trigger,
        Speed
    } kind = Kind::Trigger;
    TriggerMonitorState* triggerState = nullptr;
    LatestSpeedSample* latestSpeed = nullptr;
    OpcUaTriggerTagSettings triggerSettings;
    OpcUaSpeedTagSettings speedSettings;
    int positionDirectionSign = 1;
};

QString statusCodeText(UA_StatusCode code) {
    return QString::fromUtf8(UA_StatusCode_name(code));
}

UA_NodeId parseNodeIdString(const QString& text, UA_StatusCode* status) {
    UA_NodeId nodeId;
    UA_NodeId_init(&nodeId);

    QByteArray utf8 = text.toUtf8();
    UA_String uaText;
    uaText.length = static_cast<size_t>(utf8.size());
    uaText.data = reinterpret_cast<UA_Byte*>(utf8.data());
    *status = UA_NodeId_parse(&nodeId, uaText);
    return nodeId;
}

bool extractBooleanValue(const UA_DataValue* value, bool* result) {
    if (!value || !result || !value->hasValue) {
        return false;
    }

    const UA_Variant& variant = value->value;
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        *result = *static_cast<UA_Boolean*>(variant.data);
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_SBYTE])) {
        *result = *static_cast<UA_SByte*>(variant.data) != 0;
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_BYTE])) {
        *result = *static_cast<UA_Byte*>(variant.data) != 0;
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_INT16])) {
        *result = *static_cast<UA_Int16*>(variant.data) != 0;
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_UINT16])) {
        *result = *static_cast<UA_UInt16*>(variant.data) != 0;
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_INT32])) {
        *result = *static_cast<UA_Int32*>(variant.data) != 0;
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_UINT32])) {
        *result = *static_cast<UA_UInt32*>(variant.data) != 0;
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_INT64])) {
        *result = *static_cast<UA_Int64*>(variant.data) != 0;
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_UINT64])) {
        *result = *static_cast<UA_UInt64*>(variant.data) != 0;
        return true;
    }
    return false;
}

bool extractNumericValue(const UA_DataValue* value, double* result) {
    if (!value || !result || !value->hasValue) {
        return false;
    }

    const UA_Variant& variant = value->value;
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_DOUBLE])) {
        *result = *static_cast<UA_Double*>(variant.data);
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_FLOAT])) {
        *result = *static_cast<UA_Float*>(variant.data);
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_INT16])) {
        *result = *static_cast<UA_Int16*>(variant.data);
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_UINT16])) {
        *result = *static_cast<UA_UInt16*>(variant.data);
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_INT32])) {
        *result = *static_cast<UA_Int32*>(variant.data);
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_UINT32])) {
        *result = *static_cast<UA_UInt32*>(variant.data);
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_INT64])) {
        *result = static_cast<double>(*static_cast<UA_Int64*>(variant.data));
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_UINT64])) {
        *result = static_cast<double>(*static_cast<UA_UInt64*>(variant.data));
        return true;
    }
    if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        *result = *static_cast<UA_Boolean*>(variant.data) ? 1.0 : 0.0;
        return true;
    }
    return false;
}

bool stopRequested(const std::atomic<bool>& flag) {
    return flag.load(std::memory_order_acquire);
}

void dataChangeCallback(UA_Client*, UA_UInt32, void*, UA_UInt32, void* monContext, UA_DataValue* value) {
    auto* context = static_cast<MonitorCallbackContext*>(monContext);
    if (!context || !context->service) {
        return;
    }

    if (context->kind == MonitorCallbackContext::Kind::Speed) {
        if (!context->latestSpeed) {
            return;
        }
        double rawValue = 0.0;
        if (!extractNumericValue(value, &rawValue)) {
            context->service->emitStatus(QString("OPC UA speed tag '%1' published a non-numeric value.")
                .arg(context->speedSettings.name));
            return;
        }

        context->latestSpeed->valid = true;
        context->latestSpeed->tagName = context->speedSettings.name;
        context->latestSpeed->nodeId = context->speedSettings.nodeId;
        context->latestSpeed->unit = context->speedSettings.unit;
        context->latestSpeed->value = (rawValue * context->speedSettings.scale) + context->speedSettings.offset;
        context->latestSpeed->sampleTimeUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        context->latestSpeed->receivedAtMs = QDateTime::currentMSecsSinceEpoch();
        return;
    }

    if (!context->triggerState) {
        return;
    }

    bool rawValue = false;
    if (!extractBooleanValue(value, &rawValue)) {
        context->service->emitStatus(QString("OPC UA trigger tag '%1' published a non-boolean value.")
            .arg(context->triggerSettings.name));
        return;
    }

    const bool active = (rawValue == context->triggerSettings.activeState);
    if (!context->triggerState->hasPreviousValue) {
        context->triggerState->hasPreviousValue = true;
        context->triggerState->previousActive = active;
        return;
    }

    bool shouldFire = false;
    const QString edgeMode = context->triggerSettings.edgeMode.trimmed().toLower();
    if (edgeMode == QStringLiteral("falling")) {
        shouldFire = context->triggerState->previousActive && !active;
    } else if (edgeMode == QStringLiteral("either")) {
        shouldFire = context->triggerState->previousActive != active;
    } else {
        shouldFire = !context->triggerState->previousActive && active;
    }
    context->triggerState->previousActive = active;

    if (!shouldFire) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (context->triggerSettings.minimumIntervalMs > 0
            && context->triggerState->lastTriggeredMs > 0
            && (nowMs - context->triggerState->lastTriggeredMs) < context->triggerSettings.minimumIntervalMs) {
        return;
    }
    context->triggerState->lastTriggeredMs = nowMs;

    OpcUaClientService::TriggerEvent triggerEvent;
    triggerEvent.tagName = context->triggerSettings.name;
    triggerEvent.nodeId = context->triggerSettings.nodeId;
    triggerEvent.positionDirectionSign = context->positionDirectionSign >= 0 ? 1 : -1;

    if (context->latestSpeed && context->latestSpeed->valid) {
        triggerEvent.speedTagName = context->latestSpeed->tagName;
        triggerEvent.speedTagNodeId = context->latestSpeed->nodeId;
        triggerEvent.speedUnit = context->latestSpeed->unit;
        triggerEvent.speedSampleTimeUtc = context->latestSpeed->sampleTimeUtc;
        triggerEvent.speedValue = context->latestSpeed->value;
        triggerEvent.hasSpeed = true;
        triggerEvent.speedStale = (nowMs - context->latestSpeed->receivedAtMs) > context->speedSettings.staleTimeoutMs;
    }

    context->service->dispatchTriggerEvent(triggerEvent);
}
} // namespace


OpcUaClientService::OpcUaClientService()
    : settings_(CameraConfig::getDefaultOpcUaSettings()) {
}

OpcUaClientService::~OpcUaClientService() {
    stop();
}

void OpcUaClientService::setSettings(const OpcUaSettings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = settings;
}

void OpcUaClientService::setTriggerCallback(TriggerCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    triggerCallback_ = std::move(callback);
}

void OpcUaClientService::setStatusCallback(StatusCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    statusCallback_ = std::move(callback);
}

void OpcUaClientService::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable() || !settings_.enabled) {
        return;
    }
    stopRequested_.store(false, std::memory_order_release);
    worker_ = std::thread(&OpcUaClientService::run, this);
}

void OpcUaClientService::stop() {
    stopRequested_.store(true, std::memory_order_release);
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool OpcUaClientService::isRunning() const {
    return worker_.joinable();
}

void OpcUaClientService::dispatchTriggerEvent(const TriggerEvent& event) {
    TriggerCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = triggerCallback_;
    }
    if (callback) {
        callback(event);
    }
}

void OpcUaClientService::emitStatus(const QString& message) {
    StatusCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = statusCallback_;
    }
    if (callback) {
        callback(message);
    }
}

void OpcUaClientService::run() {
    OpcUaSettings settings;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings = settings_;
    }

    if (!settings.enabled) {
        return;
    }

    while (!stopRequested(stopRequested_)) {
        UA_Client* client = UA_Client_new();
        UA_ClientConfig_setDefault(UA_Client_getConfig(client));

        const QByteArray endpointUtf8 = settings.endpointUrl.toUtf8();
        const UA_StatusCode connectStatus = settings.useUsernamePassword
            ? UA_Client_connectUsername(client,
                endpointUtf8.constData(),
                settings.username.toUtf8().constData(),
                settings.password.toUtf8().constData())
            : UA_Client_connect(client, endpointUtf8.constData());

        if (connectStatus != UA_STATUSCODE_GOOD) {
            emitStatus(QString("OPC UA connect failed: %1").arg(statusCodeText(connectStatus)));
            UA_Client_delete(client);
            std::this_thread::sleep_for(std::chrono::milliseconds(settings.reconnectIntervalMs));
            continue;
        }

        emitStatus(QString("OPC UA connected: %1").arg(settings.endpointUrl));

        UA_CreateSubscriptionRequest subscriptionRequest = UA_CreateSubscriptionRequest_default();
        subscriptionRequest.requestedPublishingInterval = settings.publishIntervalMs;
        UA_CreateSubscriptionResponse subscriptionResponse = UA_Client_Subscriptions_create(
            client, subscriptionRequest, nullptr, nullptr, nullptr);

        if (subscriptionResponse.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
            emitStatus(QString("OPC UA subscription failed: %1")
                .arg(statusCodeText(subscriptionResponse.responseHeader.serviceResult)));
            UA_Client_disconnect(client);
            UA_Client_delete(client);
            std::this_thread::sleep_for(std::chrono::milliseconds(settings.reconnectIntervalMs));
            continue;
        }

        LatestSpeedSample latestSpeed;
        std::vector<TriggerMonitorState> triggerStates;
        std::vector<UA_NodeId> nodeIds;
        std::vector<std::unique_ptr<MonitorCallbackContext>> callbackContexts;
        triggerStates.reserve(settings.triggerTags.size());
        nodeIds.reserve(settings.triggerTags.size() + 1);

        int monitoredItemCount = 0;
        for (const auto& triggerSettings : settings.triggerTags) {
            if (!triggerSettings.enabled || triggerSettings.nodeId.trimmed().isEmpty()) {
                continue;
            }

            UA_StatusCode parseStatus = UA_STATUSCODE_GOOD;
            UA_NodeId nodeId = parseNodeIdString(triggerSettings.nodeId, &parseStatus);
            if (parseStatus != UA_STATUSCODE_GOOD) {
                emitStatus(QString("OPC UA trigger NodeId parse failed for '%1': %2")
                    .arg(triggerSettings.name, statusCodeText(parseStatus)));
                continue;
            }

            triggerStates.push_back({triggerSettings, false, false, 0});
            auto context = std::make_unique<MonitorCallbackContext>();
            context->service = this;
            context->kind = MonitorCallbackContext::Kind::Trigger;
            context->triggerState = &triggerStates.back();
            context->latestSpeed = &latestSpeed;
            context->triggerSettings = triggerSettings;
            context->speedSettings = settings.speedTag;
            context->positionDirectionSign = settings.positionDirectionSign;

            UA_MonitoredItemCreateRequest request = UA_MonitoredItemCreateRequest_default(nodeId);
            request.requestedParameters.samplingInterval = settings.publishIntervalMs;
            request.requestedParameters.queueSize = 1;
            UA_MonitoredItemCreateResult response = UA_Client_MonitoredItems_createDataChange(
                client,
                subscriptionResponse.subscriptionId,
                UA_TIMESTAMPSTORETURN_BOTH,
                request,
                context.get(),
                dataChangeCallback,
                nullptr);

            if (response.statusCode != UA_STATUSCODE_GOOD) {
                emitStatus(QString("OPC UA trigger subscription failed for '%1': %2")
                    .arg(triggerSettings.name, statusCodeText(response.statusCode)));
                UA_NodeId_clear(&nodeId);
                triggerStates.pop_back();
                continue;
            }

            nodeIds.push_back(nodeId);
            callbackContexts.push_back(std::move(context));
            ++monitoredItemCount;
        }

        if (settings.speedTag.enabled && !settings.speedTag.nodeId.trimmed().isEmpty()) {
            UA_StatusCode parseStatus = UA_STATUSCODE_GOOD;
            UA_NodeId nodeId = parseNodeIdString(settings.speedTag.nodeId, &parseStatus);
            if (parseStatus != UA_STATUSCODE_GOOD) {
                emitStatus(QString("OPC UA speed NodeId parse failed for '%1': %2")
                    .arg(settings.speedTag.name, statusCodeText(parseStatus)));
            } else {
                auto context = std::make_unique<MonitorCallbackContext>();
                context->service = this;
                context->kind = MonitorCallbackContext::Kind::Speed;
                context->latestSpeed = &latestSpeed;
                context->speedSettings = settings.speedTag;
                context->positionDirectionSign = settings.positionDirectionSign;

                UA_MonitoredItemCreateRequest request = UA_MonitoredItemCreateRequest_default(nodeId);
                request.requestedParameters.samplingInterval = settings.publishIntervalMs;
                request.requestedParameters.queueSize = 1;
                UA_MonitoredItemCreateResult response = UA_Client_MonitoredItems_createDataChange(
                    client,
                    subscriptionResponse.subscriptionId,
                    UA_TIMESTAMPSTORETURN_BOTH,
                    request,
                    context.get(),
                    dataChangeCallback,
                    nullptr);

                if (response.statusCode != UA_STATUSCODE_GOOD) {
                    emitStatus(QString("OPC UA speed subscription failed for '%1': %2")
                        .arg(settings.speedTag.name, statusCodeText(response.statusCode)));
                    UA_NodeId_clear(&nodeId);
                } else {
                    nodeIds.push_back(nodeId);
                    callbackContexts.push_back(std::move(context));
                    ++monitoredItemCount;
                }
            }
        }

        if (monitoredItemCount == 0) {
            emitStatus(QStringLiteral("OPC UA connected, but no monitored tags are configured successfully."));
            UA_Client_disconnect(client);
            UA_Client_delete(client);
            std::this_thread::sleep_for(std::chrono::milliseconds(settings.reconnectIntervalMs));
            continue;
        }

        while (!stopRequested(stopRequested_)) {
            const UA_StatusCode iterateStatus = UA_Client_run_iterate(client, 100);
            if (iterateStatus != UA_STATUSCODE_GOOD) {
                emitStatus(QString("OPC UA connection lost: %1").arg(statusCodeText(iterateStatus)));
                break;
            }
        }

        for (UA_NodeId& nodeId : nodeIds) {
            UA_NodeId_clear(&nodeId);
        }
        UA_Client_disconnect(client);
        UA_Client_delete(client);

        if (!stopRequested(stopRequested_)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(settings.reconnectIntervalMs));
        }
    }
}
