#include "MqttManager.h"

#include <Arduino.h>
#include <list>
#include <WiFi.h>
#include <WiFiClient.h>
#include <mqtt_client.h>
#include <esp_err.h>
#include <esp_heap_caps.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "AppConfig.h"
#include "AppState.h"
#include "CertificateManager.h"
#include "DiagnosticsManager.h"
#include "SettingsManager.h"
#include "TelemetryService.h"
#include "TimeManager.h"

namespace
{
esp_mqtt_client_handle_t mqttClient = nullptr;
bool mqttClientStarted = false;
volatile int mqttSubscriptionMessageId = -1;
volatile bool mqttSubscriptionAcknowledged = false;
volatile bool mqttSubscriptionRequestFailed = false;
uint32_t lastConnectRequestMs = 0;
uint32_t lastTlsTimeNoticeMs = 0;

SemaphoreHandle_t mqttQueueMutex = nullptr;
std::list<String> mqttQueue;

String mqttUri;
String resolvedClientId;
String mqttCommandTopic;
String mqttCommandBuffer;
bool mqttCommandRelevant = false;

bool dequeue(String &message)
{
    if (mqttQueueMutex == nullptr)
    {
        return false;
    }

    if (xSemaphoreTake(mqttQueueMutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return false;
    }

    const bool available = !mqttQueue.empty();
    if (available)
    {
        message = mqttQueue.front();
        mqttQueue.pop_front();
    }

    xSemaphoreGive(mqttQueueMutex);
    return available;
}

void requeueFront(const String &message)
{
    if (mqttQueueMutex == nullptr)
    {
        return;
    }

    if (xSemaphoreTake(mqttQueueMutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return;
    }

    mqttQueue.push_front(message);
    while (mqttQueue.size() > Config::MAX_QUEUE_DEPTH)
    {
        mqttQueue.pop_back();
    }

    xSemaphoreGive(mqttQueueMutex);
}

bool mqttTopicMatchesFilter(const String &filter, const String &topic)
{
    // MQTT wildcards are valid only as complete topic levels. `+` matches one
    // level; terminal `#` matches the remainder (including zero levels).
    int filterPos = 0;
    int topicPos = 0;

    while (true)
    {
        const int filterSlash = filter.indexOf('/', filterPos);
        const int topicSlash = topic.indexOf('/', topicPos);
        const String filterLevel = filterSlash >= 0
                                       ? filter.substring(filterPos, filterSlash)
                                       : filter.substring(filterPos);
        const String topicLevel = topicSlash >= 0
                                      ? topic.substring(topicPos, topicSlash)
                                      : topic.substring(topicPos);

        if (filterLevel == "#")
        {
            return filterSlash < 0; // `#` is valid only as the final level.
        }

        if (filterLevel != "+" && filterLevel != topicLevel)
        {
            return false;
        }

        const bool filterDone = filterSlash < 0;
        const bool topicDone = topicSlash < 0;
        if (filterDone || topicDone)
        {
            // A trailing /# can match zero additional topic levels.
            if (topicDone && !filterDone &&
                filter.substring(filterSlash + 1) == "#")
            {
                return true;
            }
            return filterDone && topicDone;
        }

        filterPos = filterSlash + 1;
        topicPos = topicSlash + 1;
    }
}

void processCommand(const String &message)
{
    if (message.length() < 4)
    {
        return;
    }

    DiagnosticsManager::recordMqttCommand();
    const String command = message.substring(0, 4);
    String value = message.substring(4);
    value.trim();

    if (command == "Ping")
    {
        if (value.toInt() == 1)
        {
            TelemetryService::queuePing();
        }
    }
    else if (command == "Cfrr")
    {
        if (value.toInt() == 1)
        {
            TelemetryService::queueConfiguration();
        }
    }
    else if (command == "Dvrr")
    {
        // Reserved by the existing protocol.
    }
    else if (command == "Rate")
    {
        SettingsManager::savePositiveUInt(Config::NvsKey::SEND_INTERVAL, value, settings.sendIntervalSec);
    }
    else if (command == "IamA")
    {
        SettingsManager::savePositiveUInt(Config::NvsKey::ALIVE_INTERVAL, value, settings.aliveIntervalSec);
    }
    else if (command == "Ofte")
    {
        SettingsManager::saveFloat(Config::NvsKey::OFFSET_TEMPERATURE, value, settings.temperatureOffset);
    }
    else if (command == "Ofhu")
    {
        SettingsManager::saveFloat(Config::NvsKey::OFFSET_HUMIDITY, value, settings.humidityOffset);
    }
    else if (command == "Ofaq")
    {
        SettingsManager::saveFloat(Config::NvsKey::OFFSET_IAQ, value, settings.iaqOffset);
    }
    else if (command == "Ofco")
    {
        SettingsManager::saveFloat(Config::NvsKey::OFFSET_CO2, value, settings.co2Offset);
    }
    else if (command == "Ofvo")
    {
        SettingsManager::saveFloat(Config::NvsKey::OFFSET_VOC, value, settings.vocOffset);
    }
    else if (command == "Ofap")
    {
        SettingsManager::saveFloat(Config::NvsKey::OFFSET_PRESSURE, value, settings.pressureOffset);
    }
    else if (command == "Ofli")
    {
        SettingsManager::saveInt(Config::NvsKey::OFFSET_LIGHT, value, settings.lightOffset);
    }
    else if (command == "Ofso")
    {
        SettingsManager::saveInt(Config::NvsKey::OFFSET_SOUND, value, settings.soundOffset);
    }
    else if (command == "Dfte")
    {
        SettingsManager::saveNonNegativeFloat(Config::NvsKey::DB_TEMPERATURE, value, settings.dbTemperature);
    }
    else if (command == "Dfhu")
    {
        SettingsManager::saveNonNegativeFloat(Config::NvsKey::DB_HUMIDITY, value, settings.dbHumidity);
    }
    else if (command == "Dfap")
    {
        SettingsManager::saveNonNegativeFloat(Config::NvsKey::DB_PRESSURE, value, settings.dbPressure);
    }
    else if (command == "Dfaq")
    {
        SettingsManager::saveNonNegativeFloat(Config::NvsKey::DB_IAQ, value, settings.dbIaq);
    }
    else if (command == "Dfco")
    {
        SettingsManager::saveNonNegativeFloat(Config::NvsKey::DB_CO2, value, settings.dbCo2);
    }
    else if (command == "Dfvo")
    {
        SettingsManager::saveNonNegativeFloat(Config::NvsKey::DB_VOC, value, settings.dbVoc);
    }
    else if (command == "Dfso")
    {
        SettingsManager::saveNonNegativeFloat(Config::NvsKey::DB_SOUND, value, settings.dbSound);
    }
    else if (command == "Dfli")
    {
        SettingsManager::saveNonNegativeFloat(Config::NvsKey::DB_LIGHT, value, settings.dbLight);
    }
}

void logMqttError(esp_mqtt_event_handle_t event)
{
    Serial.print("MQTT error");

    if (event == nullptr || event->error_handle == nullptr)
    {
        Serial.println(".");
        return;
    }

    const esp_mqtt_error_codes_t *error = event->error_handle;
    Serial.print(" (type ");
    Serial.print(static_cast<int>(error->error_type));
    Serial.println(").");

    // ESP-IDF 4.4 exposes the TLS/transport details below. Keeping these in
    // the serial log makes certificate-chain, hostname, socket, and broker
    // authentication failures distinguishable during commissioning.
    if (error->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
    {
        Serial.print("  esp-tls error: 0x");
        Serial.print(static_cast<uint32_t>(error->esp_tls_last_esp_err), HEX);
        Serial.print(" (");
        Serial.print(esp_err_to_name(error->esp_tls_last_esp_err));
        Serial.println(")");
        Serial.print("  TLS stack error: 0x");
        Serial.println(static_cast<uint32_t>(error->esp_tls_stack_err), HEX);
        Serial.print("  Certificate verify flags: 0x");
        Serial.println(static_cast<uint32_t>(error->esp_tls_cert_verify_flags), HEX);
        Serial.print("  Socket errno: ");
        Serial.println(error->esp_transport_sock_errno);
    }
    else if (error->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
    {
        Serial.print("  Broker CONNACK return code: ");
        Serial.println(static_cast<int>(error->connect_return_code));
    }

    String diagnosticMessage = "MQTT error type " + String(static_cast<int>(error->error_type));
    if (error->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
    {
        diagnosticMessage += ", esp-tls=0x" + String(static_cast<uint32_t>(error->esp_tls_last_esp_err), HEX);
        diagnosticMessage += ", verify=0x" + String(static_cast<uint32_t>(error->esp_tls_cert_verify_flags), HEX);
        diagnosticMessage += ", errno=" + String(error->esp_transport_sock_errno);
    }
    else if (error->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
    {
        diagnosticMessage += ", CONNACK=" + String(static_cast<int>(error->connect_return_code));
    }
    DiagnosticsManager::recordMqttError(diagnosticMessage);

    if (settings.mqttTlsEnabled)
    {
        Serial.println(
            "MQTT TLS hint: verify CA chain, broker hostname/SAN match, credentials, port, and system time.");
        if (settings.mqttHost.indexOf('.') >= 0 && settings.mqttHost.length() > 0)
        {
            bool numericHost = true;
            for (size_t i = 0; i < settings.mqttHost.length(); ++i)
            {
                const char c = settings.mqttHost[i];
                if ((c < '0' || c > '9') && c != '.')
                {
                    numericHost = false;
                    break;
                }
            }
            if (numericHost)
            {
                Serial.println(
                    "MQTT TLS note: broker is configured by IP address. The server certificate must contain this IP in its SAN; otherwise configure the certificate DNS hostname instead.");
            }
        }
    }
    else
    {
        Serial.println("MQTT hint: verify broker address, port, credentials, and network reachability.");
    }
}

esp_err_t mqttEventHandler(esp_mqtt_event_handle_t event)
{
    if (event == nullptr)
    {
        return ESP_OK;
    }

    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        appState.network.mqttConnected = true;
        lastConnectRequestMs = millis();
        DiagnosticsManager::recordMqttConnected();

        mqttSubscriptionAcknowledged = false;
        mqttSubscriptionRequestFailed = false;
        mqttSubscriptionMessageId = esp_mqtt_client_subscribe(
            event->client,
            settings.mqttSubscribeTopic.c_str(),
            settings.mqttQos);
        if (mqttSubscriptionMessageId < 0)
        {
            mqttSubscriptionRequestFailed = true;
            Serial.println("MQTT subscribe request failed.");
        }

        TelemetryService::queueDeviceInfo();
        Serial.print("MQTT connected");
        Serial.println(settings.mqttTlsEnabled ? " securely (TLS + CA verification)." : ".");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        if (mqttSubscriptionMessageId < 0 || event->msg_id == mqttSubscriptionMessageId)
        {
            mqttSubscriptionAcknowledged = true;
            mqttSubscriptionRequestFailed = false;
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        appState.network.mqttConnected = false;
        mqttSubscriptionAcknowledged = false;
        DiagnosticsManager::recordMqttDisconnected();
        Serial.println("MQTT disconnected; automatic reconnect remains enabled.");
        break;

    case MQTT_EVENT_DATA:
        // ESP-MQTT can split large incoming messages across several DATA
        // events. Commands are small, but reassembly keeps the implementation
        // correct and prevents truncated commands from being interpreted.
        if (event->current_data_offset == 0)
        {
            mqttCommandTopic = "";
            mqttCommandBuffer = "";
            mqttCommandRelevant = false;

            if (event->topic != nullptr && event->topic_len > 0)
            {
                mqttCommandTopic.reserve(event->topic_len);
                for (int i = 0; i < event->topic_len; ++i)
                {
                    mqttCommandTopic += event->topic[i];
                }
                mqttCommandRelevant = mqttTopicMatchesFilter(settings.mqttSubscribeTopic, mqttCommandTopic);
            }

            if (mqttCommandRelevant && event->total_data_len > 0 && event->total_data_len <= 2048)
            {
                mqttCommandBuffer.reserve(event->total_data_len);
            }
            else if (event->total_data_len > 2048)
            {
                mqttCommandRelevant = false;
                Serial.println("MQTT command ignored: payload exceeds 2048 bytes.");
            }
        }

        if (mqttCommandRelevant && event->data != nullptr && event->data_len > 0)
        {
            for (int i = 0; i < event->data_len; ++i)
            {
                mqttCommandBuffer += event->data[i];
            }
        }

        if (event->current_data_offset + event->data_len >= event->total_data_len)
        {
            if (mqttCommandRelevant)
            {
                processCommand(mqttCommandBuffer);
            }
            mqttCommandBuffer = "";
            mqttCommandTopic = "";
            mqttCommandRelevant = false;
        }
        break;

    case MQTT_EVENT_ERROR:
        appState.network.mqttConnected = false;
        mqttSubscriptionAcknowledged = false;
        logMqttError(event);
        break;

    default:
        break;
    }

    return ESP_OK;
}

bool publishMessage(const String &message)
{
    if (!appState.network.mqttConnected || mqttClient == nullptr)
    {
        return false;
    }

    // Enqueue into the ESP-IDF MQTT outbox so the service task does not block
    // on network transmission. store=true also permits QoS 0 messages to use
    // this non-blocking path.
    const int messageId = esp_mqtt_client_enqueue(
        mqttClient,
        settings.mqttPublishTopic.c_str(),
        message.c_str(),
        static_cast<int>(message.length()),
        settings.mqttQos,
        settings.mqttPublishRetain ? 1 : 0,
        true);

    if (messageId >= 0) DiagnosticsManager::recordMqttPublish();
    return messageId >= 0;
}

bool tlsConfigurationReady()
{
    if (!settings.mqttTlsEnabled)
    {
        return true;
    }

    if (settings.mqttCaCert.length() == 0)
    {
        Serial.println("MQTT TLS is enabled but no CA certificate is configured.");
        return false;
    }

    if (!TimeManager::systemTimeValid())
    {
        const uint32_t now = millis();
        if (now - lastTlsTimeNoticeMs >= 10000UL || lastTlsTimeNoticeMs == 0)
        {
            lastTlsTimeNoticeMs = now;
            Serial.println("MQTT TLS waiting for valid NTP/system time before certificate verification.");
        }
        return false;
    }

    return true;
}

String mqttConnackMessage(uint8_t code)
{
    switch (code)
    {
    case 0: return "Accepted";
    case 1: return "Unacceptable protocol version";
    case 2: return "Client identifier rejected";
    case 3: return "Broker unavailable";
    case 4: return "Bad username or password";
    case 5: return "Not authorized";
    default: return "Unknown CONNACK code " + String(code);
    }
}

bool mqttConnectionSettingsMatch(const AppSettings &candidate)
{
    return candidate.mqttHost == settings.mqttHost &&
           candidate.mqttPort == settings.mqttPort &&
           candidate.mqttSubscribeTopic == settings.mqttSubscribeTopic &&
           candidate.mqttQos == settings.mqttQos &&
           candidate.mqttUsername == settings.mqttUsername &&
           candidate.mqttPassword == settings.mqttPassword &&
           candidate.mqttClientId == settings.mqttClientId &&
           candidate.mqttKeepAliveSec == settings.mqttKeepAliveSec &&
           candidate.mqttCleanSession == settings.mqttCleanSession &&
           candidate.mqttTlsEnabled == settings.mqttTlsEnabled &&
           candidate.mqttCaCert == settings.mqttCaCert;
}

const char *stepState(bool checked, bool ok, bool required = true)
{
    if (!required) return "na";
    if (!checked) return "not-run";
    return ok ? "pass" : "fail";
}

void appendJsonEscaped(String &json, const String &value)
{
    for (size_t i = 0; i < value.length(); ++i)
    {
        const char c = value[i];
        if (c == '"') json += "\\\"";
        else if (c == '\\') json += "\\\\";
        else if (c == '\n') json += "\\n";
        else if (c == '\r') json += "\\r";
        else if (static_cast<uint8_t>(c) >= 0x20) json += c;
    }
}

struct MqttTestContext
{
    const AppSettings *candidate = nullptr;
    volatile bool done = false;
    volatile bool tlsChecked = false;
    volatile bool tlsOk = false;
    volatile bool mqttChecked = false;
    volatile bool mqttOk = false;
    volatile bool subscribeChecked = false;
    volatile bool subscribeOk = false;
    volatile bool failed = false;
    int subscribeMessageId = -1;
    int errorType = 0;
    int espTlsError = 0;
    int tlsStackError = 0;
    uint32_t certVerifyFlags = 0;
    int socketErrno = 0;
    int connackCode = -1;
    String message;
};

MqttTestContext *activeMqttTest = nullptr;
volatile bool mqttTestBusy = false;

esp_err_t mqttTestEventHandler(esp_mqtt_event_handle_t event)
{
    MqttTestContext *ctx = activeMqttTest;
    if (ctx == nullptr || event == nullptr || ctx->candidate == nullptr)
        return ESP_OK;

    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        // CONNECTED proves transport/TLS (when enabled) and broker CONNACK.
        ctx->tlsChecked = ctx->candidate->mqttTlsEnabled;
        ctx->tlsOk = true;
        ctx->mqttChecked = true;
        ctx->mqttOk = true;
        ctx->message = "MQTT login accepted.";
        ctx->subscribeMessageId = esp_mqtt_client_subscribe(
            event->client,
            ctx->candidate->mqttSubscribeTopic.c_str(),
            ctx->candidate->mqttQos);
        if (ctx->subscribeMessageId < 0)
        {
            ctx->subscribeChecked = true;
            ctx->message = "MQTT login succeeded, but the subscription request failed.";
            ctx->failed = true;
            ctx->done = true;
        }
        break;

    case MQTT_EVENT_SUBSCRIBED:
        if (ctx->subscribeMessageId < 0 || event->msg_id == ctx->subscribeMessageId)
        {
            ctx->subscribeChecked = true;
            ctx->subscribeOk = true;
            ctx->message = "MQTT login and command-topic subscription succeeded.";
            ctx->done = true;
        }
        break;

    case MQTT_EVENT_ERROR:
        if (event->error_handle != nullptr)
        {
            const esp_mqtt_error_codes_t *error = event->error_handle;
            ctx->errorType = static_cast<int>(error->error_type);
            if (error->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
            {
                // CONNACK refusal proves TCP and TLS (for mqtts://) completed.
                ctx->mqttChecked = true;
                ctx->tlsChecked = ctx->candidate->mqttTlsEnabled;
                ctx->tlsOk = true;
                ctx->connackCode = static_cast<int>(error->connect_return_code);
                ctx->message = "MQTT login rejected: " +
                               mqttConnackMessage(static_cast<uint8_t>(error->connect_return_code)) + ".";
            }
            else if (error->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
            {
                ctx->tlsChecked = ctx->candidate->mqttTlsEnabled;
                ctx->espTlsError = static_cast<int>(error->esp_tls_last_esp_err);
                ctx->tlsStackError = error->esp_tls_stack_err;
                ctx->certVerifyFlags = error->esp_tls_cert_verify_flags;
                ctx->socketErrno = error->esp_transport_sock_errno;
                ctx->message = ctx->candidate->mqttTlsEnabled
                                   ? "Independent TLS handshake failed; see transport diagnostics below."
                                   : "Independent MQTT transport failed; see transport diagnostics below.";
            }
            else
            {
                ctx->mqttChecked = true;
                ctx->message = "Independent MQTT connection failed.";
            }
        }
        else
        {
            ctx->mqttChecked = true;
            ctx->message = "Independent MQTT connection failed.";
        }
        ctx->failed = true;
        ctx->done = true;
        break;

    case MQTT_EVENT_DISCONNECTED:
        if (!ctx->done && !ctx->mqttOk)
        {
            ctx->message = "Broker disconnected before MQTT login completed.";
            ctx->failed = true;
            ctx->done = true;
        }
        break;

    default:
        break;
    }

    return ESP_OK;
}

String boolJson(bool value)
{
    return value ? "true" : "false";
}
} // namespace

void MqttManager::begin()
{
    mqttQueueMutex = xSemaphoreCreateMutex();
    if (mqttQueueMutex == nullptr)
    {
        Serial.println("Failed to create MQTT queue mutex.");
    }

    mqttUri.reserve(settings.mqttHost.length() + 32);
    mqttUri = settings.mqttTlsEnabled ? "mqtts://" : "mqtt://";
    mqttUri += settings.mqttHost;
    mqttUri += ':';
    mqttUri += String(settings.mqttPort);

    if (settings.mqttTlsEnabled && settings.mqttCaCert.length() == 0)
    {
        Serial.println("MQTT TLS configuration invalid: CA certificate is missing.");
        return;
    }

    if (settings.mqttTlsEnabled)
    {
        Serial.print("MQTT TLS CA certificate loaded from NVS: ");
        Serial.print(settings.mqttCaCert.length());
        Serial.println(" characters.");
    }

    // Preserve the historical AsyncMqttClient-generated ID when no explicit
    // client ID is configured. This avoids changing broker-side session
    // identity merely because the transport implementation moved to ESP-MQTT.
    if (settings.mqttClientId.length() > 0)
    {
        resolvedClientId = settings.mqttClientId;
    }
    else
    {
        char generatedClientId[32];
        snprintf(
            generatedClientId,
            sizeof(generatedClientId),
            "esp32-%06llx",
            static_cast<unsigned long long>(ESP.getEfuseMac()));
        resolvedClientId = generatedClientId;
    }

    esp_mqtt_client_config_t mqttConfig = {};
    mqttConfig.uri = mqttUri.c_str();
    mqttConfig.client_id = resolvedClientId.c_str();
    mqttConfig.username = settings.mqttUsername.length() > 0 ? settings.mqttUsername.c_str() : nullptr;
    mqttConfig.password = settings.mqttPassword.length() > 0 ? settings.mqttPassword.c_str() : nullptr;
    mqttConfig.keepalive = settings.mqttKeepAliveSec;
    mqttConfig.disable_clean_session = settings.mqttCleanSession ? 0 : 1;
    mqttConfig.disable_auto_reconnect = false;
    mqttConfig.reconnect_timeout_ms = Config::MQTT_RECONNECT_INTERVAL_MS;
    mqttConfig.buffer_size = Config::MQTT_IN_BUFFER_SIZE;
    mqttConfig.out_buffer_size = Config::MQTT_OUT_BUFFER_SIZE;
    mqttConfig.event_handle = mqttEventHandler;

    if (settings.mqttTlsEnabled)
    {
        // ESP-MQTT/ESP-TLS validates the broker certificate against this CA.
        // Common-name/SAN verification remains enabled (the secure default),
        // so the configured mqttHost should normally be the DNS name present
        // in the broker certificate rather than a raw IP address.
        mqttConfig.cert_pem = settings.mqttCaCert.c_str();
        mqttConfig.cert_len = 0; // Null-terminated PEM string.
        mqttConfig.skip_cert_common_name_check = false;
    }

    mqttClient = esp_mqtt_client_init(&mqttConfig);
    if (mqttClient == nullptr)
    {
        Serial.println("ERROR: ESP-MQTT client initialization failed.");
        return;
    }

    Serial.print("MQTT configured: ");
    Serial.print(mqttUri);
    Serial.println(settings.mqttTlsEnabled ? " (TLS + CA verification)" : " (plain TCP)");
}

void MqttManager::connect()
{
    if (!appState.network.wifiConnected || mqttClient == nullptr || appState.network.mqttConnected)
    {
        return;
    }

    if (!tlsConfigurationReady())
    {
        return;
    }

    const uint32_t now = millis();
    if (lastConnectRequestMs != 0 && now - lastConnectRequestMs < Config::MQTT_RECONNECT_INTERVAL_MS)
    {
        return;
    }
    lastConnectRequestMs = now;
    DiagnosticsManager::recordMqttConnectAttempt();

    if (!mqttClientStarted)
    {
        const esp_err_t result = esp_mqtt_client_start(mqttClient);
        if (result == ESP_OK)
        {
            mqttClientStarted = true;
            Serial.println("MQTT client task started.");
        }
        else
        {
            Serial.print("MQTT client start failed: ");
            Serial.println(static_cast<int>(result));
        }
        return;
    }

    // Auto reconnect is enabled in ESP-MQTT. This explicit request provides a
    // deterministic recovery kick after Wi-Fi becomes available again.
    const esp_err_t result = esp_mqtt_client_reconnect(mqttClient);
    if (result != ESP_OK && result != ESP_FAIL)
    {
        Serial.print("MQTT reconnect request returned: ");
        Serial.println(static_cast<int>(result));
    }
}

void MqttManager::handleWifiConnected()
{
    // For TLS this may intentionally wait until SNTP has established a valid
    // system clock. service() will retry without blocking the Wi-Fi callback.
    connect();
}

void MqttManager::handleWifiDisconnected()
{
    appState.network.mqttConnected = false;
    mqttSubscriptionAcknowledged = false;
    // Do not destroy/stop the client. ESP-MQTT retains its state and automatic
    // reconnect mechanism, while our Wi-Fi manager separately restores the STA
    // connection. Once Wi-Fi is back, connect() gives the broker reconnect an
    // immediate kick.
}

void MqttManager::enqueue(const String &message)
{
    if (mqttQueueMutex == nullptr)
    {
        return;
    }

    if (xSemaphoreTake(mqttQueueMutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return;
    }

    if (mqttQueue.size() >= Config::MAX_QUEUE_DEPTH)
    {
        mqttQueue.pop_front();
    }

    mqttQueue.push_back(message);
    xSemaphoreGive(mqttQueueMutex);
}

void MqttManager::service()
{
    if (appState.network.wifiConnected && !appState.network.mqttConnected)
    {
        connect();
    }

    if (!appState.network.mqttConnected)
    {
        return;
    }

    String message;
    if (!dequeue(message))
    {
        return;
    }

    if (!publishMessage(message))
    {
        requeueFront(message);
    }
}


String MqttManager::testConnection(const AppSettings &testSettings)
{
    bool dnsChecked = false, dnsOk = false;
    bool tcpChecked = false, tcpOk = false;
    bool timeChecked = false, timeOk = !testSettings.mqttTlsEnabled;
    bool caChecked = false, caOk = !testSettings.mqttTlsEnabled;
    bool tlsChecked = false, tlsOk = !testSettings.mqttTlsEnabled;
    bool mqttChecked = false, mqttOk = false;
    bool subscribeChecked = false, subscribeOk = false;
    const bool liveConnected = appState.network.mqttConnected;
    const bool activeConfiguration = mqttConnectionSettingsMatch(testSettings);
    String mode = activeConfiguration ? "live" : "independent";
    String outcome = "fail";
    String resolvedIp;
    String message;

    uint32_t freeHeapBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    uint32_t largestBlockBefore = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    uint32_t freeHeapAfter = freeHeapBefore;
    uint32_t largestBlockAfter = largestBlockBefore;
    int errorType = 0;
    int espTlsError = 0;
    int tlsStackError = 0;
    uint32_t certVerifyFlags = 0;
    int socketErrno = 0;
    int connackCode = -1;

    IPAddress resolved;
    if (mqttTestBusy)
    {
        message = "Another independent MQTT test is already running.";
    }
    else if (WiFi.status() != WL_CONNECTED)
    {
        message = "Wi-Fi is not connected.";
    }
    else
    {
        dnsChecked = true;
        dnsOk = WiFi.hostByName(testSettings.mqttHost.c_str(), resolved) == 1;
        if (dnsOk) resolvedIp = resolved.toString();
        else message = "DNS/host resolution failed.";
    }

    if (dnsOk && testSettings.mqttTlsEnabled)
    {
        timeChecked = true;
        timeOk = TimeManager::systemTimeValid();
        caChecked = true;
        String certificateError;
        caOk = CertificateManager::validatePem(testSettings.mqttCaCert, certificateError);
        if (!timeOk)
            message = "System time is not synchronized; TLS certificate validity cannot be checked.";
        else if (!caOk)
            message = certificateError;
    }

    const bool prechecksOk = dnsOk && timeOk && caOk;

    if (activeConfiguration)
    {
        // The current production session is the strongest possible test of the
        // saved settings. Reuse it instead of allocating a second TLS session.
        // Fresh DNS/time/CA prechecks are still reported separately, but a
        // transient precheck failure does not erase proof from a live session.
        const String precheckMessage = message;
        if (liveConnected)
        {
            tcpChecked = true;
            tcpOk = true;
            tlsChecked = testSettings.mqttTlsEnabled;
            tlsOk = true;
            mqttChecked = true;
            mqttOk = true;

            if (mqttSubscriptionAcknowledged)
            {
                subscribeChecked = true;
                subscribeOk = true;
                if (prechecksOk)
                {
                    outcome = "pass";
                    message = testSettings.mqttTlsEnabled
                                  ? "Active production MQTT/TLS connection and command-topic subscription are healthy."
                                  : "Active production MQTT connection and command-topic subscription are healthy.";
                }
                else
                {
                    outcome = "partial";
                    message = "The live MQTT session is healthy, but a fresh local precheck failed: " + precheckMessage;
                }
            }
            else if (mqttSubscriptionRequestFailed)
            {
                subscribeChecked = true;
                outcome = "fail";
                message = "Active MQTT connection is healthy, but the command-topic subscription request failed.";
            }
            else
            {
                outcome = "partial";
                message = "Active MQTT connection is healthy; subscription acknowledgement has not been observed yet.";
            }
        }
        else
        {
            outcome = "fail";
            message = "These are the active MQTT settings, but the production client is currently disconnected. No duplicate TLS client was started; review Last error in Diagnostics.";
        }
    }
    else if (prechecksOk)
    {
        // Unsaved settings: first prove basic reachability with a lightweight
        // raw TCP probe. This keeps TCP separate from TLS in the UI.
        tcpChecked = true;
        WiFiClient tcpProbe;
        tcpOk = tcpProbe.connect(resolved, testSettings.mqttPort, Config::MQTT_TEST_TCP_TIMEOUT_MS) == 1;
        tcpProbe.stop();

        if (!tcpOk)
        {
            outcome = "fail";
            message = "TCP connection to the candidate broker/port failed.";
        }
        else
        {
            const bool lowHeapWhileLive = liveConnected &&
                (freeHeapBefore < Config::MQTT_TEST_MIN_FREE_HEAP_BYTES ||
                 largestBlockBefore < Config::MQTT_TEST_MIN_LARGEST_BLOCK_BYTES);

            if (lowHeapWhileLive)
            {
                outcome = "partial";
                message = "DNS/TCP/time/CA prechecks passed. Full TLS/MQTT test was not started because the production MQTT session is active and heap margin is low; save/restart to validate the changed settings without a second TLS client.";
            }
            else
            {
                mqttTestBusy = true;

                String testUri = testSettings.mqttTlsEnabled ? "mqtts://" : "mqtt://";
                testUri += testSettings.mqttHost;
                testUri += ':';
                testUri += String(testSettings.mqttPort);

                char temporaryClientId[40];
                snprintf(
                    temporaryClientId,
                    sizeof(temporaryClientId),
                    "RoomMonitor-Test-%06llx",
                    static_cast<unsigned long long>(ESP.getEfuseMac()));

                MqttTestContext ctx;
                ctx.candidate = &testSettings;
                activeMqttTest = &ctx;

                esp_mqtt_client_config_t testConfig = {};
                testConfig.uri = testUri.c_str();
                testConfig.client_id = temporaryClientId;
                testConfig.username = testSettings.mqttUsername.length() > 0 ? testSettings.mqttUsername.c_str() : nullptr;
                testConfig.password = testSettings.mqttPassword.length() > 0 ? testSettings.mqttPassword.c_str() : nullptr;
                testConfig.keepalive = testSettings.mqttKeepAliveSec;
                testConfig.disable_clean_session = 0;
                testConfig.disable_auto_reconnect = true;
                testConfig.buffer_size = Config::MQTT_IN_BUFFER_SIZE;
                testConfig.out_buffer_size = Config::MQTT_OUT_BUFFER_SIZE;
                testConfig.event_handle = mqttTestEventHandler;

                if (testSettings.mqttTlsEnabled)
                {
                    testConfig.cert_pem = testSettings.mqttCaCert.c_str();
                    testConfig.cert_len = 0;
                    testConfig.skip_cert_common_name_check = false;
                }

                esp_mqtt_client_handle_t testClient = esp_mqtt_client_init(&testConfig);
                if (testClient == nullptr)
                {
                    outcome = "partial";
                    message = "TCP precheck passed, but there was not enough runtime capacity to initialize the independent MQTT client.";
                }
                else
                {
                    const esp_err_t startResult = esp_mqtt_client_start(testClient);
                    if (startResult != ESP_OK)
                    {
                        outcome = "partial";
                        message = "TCP precheck passed, but the independent MQTT client could not start: ";
                        message += esp_err_to_name(startResult);
                        message += '.';
                    }
                    else
                    {
                        const uint32_t started = millis();
                        while (!ctx.done && millis() - started < Config::MQTT_TEST_TIMEOUT_MS)
                            delay(10);

                        if (!ctx.done && ctx.message.length() == 0)
                            ctx.message = "Independent MQTT test timed out.";

                        tlsChecked = ctx.tlsChecked;
                        tlsOk = testSettings.mqttTlsEnabled ? ctx.tlsOk : true;
                        mqttChecked = ctx.mqttChecked || ctx.mqttOk;
                        mqttOk = ctx.mqttOk;
                        subscribeChecked = ctx.subscribeChecked;
                        subscribeOk = ctx.subscribeOk;
                        message = ctx.message;
                        errorType = ctx.errorType;
                        espTlsError = ctx.espTlsError;
                        tlsStackError = ctx.tlsStackError;
                        certVerifyFlags = ctx.certVerifyFlags;
                        socketErrno = ctx.socketErrno;
                        connackCode = ctx.connackCode;

                        if (mqttOk && subscribeOk)
                            outcome = "pass";
                        else if (!ctx.done)
                            outcome = "partial";
                        else
                            outcome = "fail";

                        esp_mqtt_client_stop(testClient);
                    }
                    esp_mqtt_client_destroy(testClient);
                }

                activeMqttTest = nullptr;
                mqttTestBusy = false;
            }
        }
    }

    freeHeapAfter = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    largestBlockAfter = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    DiagnosticsManager::recordMqttTest(outcome, message);

    String json;
    json.reserve(1250);
    json = "{\"ok\":" + boolJson(outcome == "pass");
    json += ",\"outcome\":\"" + outcome + "\"";
    json += ",\"mode\":\"" + mode + "\"";
    json += ",\"liveConnected\":" + boolJson(liveConnected);
    json += ",\"message\":\"";
    appendJsonEscaped(json, message);
    json += "\",\"resolvedIp\":\"" + resolvedIp + "\"";
    json += ",\"steps\":{\"dns\":\"" + String(stepState(dnsChecked, dnsOk)) + "\"";
    json += ",\"tcp\":\"" + String(stepState(tcpChecked, tcpOk)) + "\"";
    json += ",\"time\":\"" + String(stepState(timeChecked, timeOk, testSettings.mqttTlsEnabled)) + "\"";
    json += ",\"ca\":\"" + String(stepState(caChecked, caOk, testSettings.mqttTlsEnabled)) + "\"";
    json += ",\"tls\":\"" + String(stepState(tlsChecked, tlsOk, testSettings.mqttTlsEnabled)) + "\"";
    json += ",\"mqttLogin\":\"" + String(stepState(mqttChecked, mqttOk)) + "\"";
    json += ",\"subscribe\":\"" + String(stepState(subscribeChecked, subscribeOk)) + "\"}";
    json += ",\"details\":{\"freeHeapBeforeBytes\":" + String(freeHeapBefore);
    json += ",\"largestBlockBeforeBytes\":" + String(largestBlockBefore);
    json += ",\"freeHeapAfterBytes\":" + String(freeHeapAfter);
    json += ",\"largestBlockAfterBytes\":" + String(largestBlockAfter);
    json += ",\"errorType\":" + String(errorType);
    json += ",\"espTlsError\":" + String(espTlsError);
    json += ",\"tlsStackError\":" + String(tlsStackError);
    json += ",\"certVerifyFlags\":" + String(certVerifyFlags);
    json += ",\"socketErrno\":" + String(socketErrno);
    json += ",\"connackCode\":" + String(connackCode);
    json += "}}";
    return json;
}
