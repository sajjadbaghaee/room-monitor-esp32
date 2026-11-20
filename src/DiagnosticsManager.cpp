#include "DiagnosticsManager.h"

#include <SPIFFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "AppConfig.h"
#include "AppState.h"
#include "CertificateManager.h"
#include "FilesystemManager.h"
#include "SettingsManager.h"
#include "TimeManager.h"
#include "VersionInfo.h"
#include "WebSecurity.h"

#include "freertos/semphr.h"

namespace
{
SemaphoreHandle_t diagnosticsMutex = nullptr;
TaskHandle_t sensorTaskHandle = nullptr;
TaskHandle_t serviceTaskHandle = nullptr;

uint32_t bootCounter = 0;
uint32_t watchdogCounter = 0;
esp_reset_reason_t resetReason = ESP_RST_UNKNOWN;

uint32_t wifiReconnectCount = 0;
uint8_t lastWifiDisconnectReason = 0;
uint32_t lastWifiConnectedMs = 0;

uint32_t mqttReconnectCount = 0;
uint32_t mqttConnectAttempts = 0;
uint32_t lastMqttConnectedMs = 0;
uint32_t lastMqttPublishMs = 0;
uint32_t lastMqttCommandMs = 0;
String lastMqttError;
String lastMqttTestMessage;
String lastMqttTestOutcome;
uint32_t lastMqttTestMs = 0;

uint32_t lastNtpSyncMs = 0;

uint32_t lastBsecSampleMs = 0;
uint32_t lastSoundSampleMs = 0;
uint32_t lastLightSampleMs = 0;
uint32_t bsecFailureCount = 0;
uint32_t soundFailureCount = 0;
uint32_t lightFailureCount = 0;
uint32_t bsecConsecutiveFailures = 0;
uint32_t soundConsecutiveFailures = 0;
uint32_t lightConsecutiveFailures = 0;
int lastBsecStatus = 0;
int lastBmeStatus = 0;
uint8_t lastIaqAccuracy = 0;

uint32_t loginSuccessCount = 0;
uint32_t loginFailureCount = 0;
uint32_t lastControlledRestartMs = 0;

bool lockDiagnostics()
{
    if (diagnosticsMutex == nullptr) return true;
    return xSemaphoreTake(diagnosticsMutex, pdMS_TO_TICKS(50)) == pdTRUE;
}

void unlockDiagnostics()
{
    if (diagnosticsMutex != nullptr) xSemaphoreGive(diagnosticsMutex);
}

String jsonEscape(const String &value)
{
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i)
    {
        const char c = value[i];
        switch (c)
        {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<uint8_t>(c) >= 0x20) out += c;
            break;
        }
    }
    return out;
}

bool isWatchdogReset(esp_reset_reason_t reason)
{
    return reason == ESP_RST_INT_WDT ||
           reason == ESP_RST_TASK_WDT ||
           reason == ESP_RST_WDT;
}

String resetReasonName(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON: return "Power-on reset";
    case ESP_RST_EXT: return "External reset";
    case ESP_RST_SW: return "Software restart";
    case ESP_RST_PANIC: return "Exception / panic";
    case ESP_RST_INT_WDT: return "Interrupt watchdog";
    case ESP_RST_TASK_WDT: return "Task watchdog";
    case ESP_RST_WDT: return "Other watchdog";
    case ESP_RST_DEEPSLEEP: return "Deep-sleep wake";
    case ESP_RST_BROWNOUT: return "Brownout";
    case ESP_RST_SDIO: return "SDIO reset";
    default: return "Unknown";
    }
}

uint32_t ageSeconds(uint32_t timestampMs)
{
    if (timestampMs == 0) return 0;
    return (millis() - timestampMs) / 1000UL;
}

bool stale(uint32_t timestampMs, uint32_t thresholdMs)
{
    return timestampMs == 0 || (millis() - timestampMs) > thresholdMs;
}
} // namespace

void DiagnosticsManager::begin()
{
    if (diagnosticsMutex == nullptr)
        diagnosticsMutex = xSemaphoreCreateMutex();

    resetReason = esp_reset_reason();
    bootCounter = SettingsManager::getUInt(Config::NvsKey::BOOT_COUNT, 0) + 1U;
    SettingsManager::saveUInt(Config::NvsKey::BOOT_COUNT, bootCounter);

    watchdogCounter = SettingsManager::getUInt(Config::NvsKey::WDT_COUNT, 0);
    if (isWatchdogReset(resetReason))
    {
        ++watchdogCounter;
        SettingsManager::saveUInt(Config::NvsKey::WDT_COUNT, watchdogCounter);
    }

    Serial.print("Diagnostics: boot #");
    Serial.print(bootCounter);
    Serial.print(", reset reason: ");
    Serial.println(resetReasonName(resetReason));
}

void DiagnosticsManager::registerTasks(TaskHandle_t sensorTask, TaskHandle_t serviceTask)
{
    sensorTaskHandle = sensorTask;
    serviceTaskHandle = serviceTask;
}

void DiagnosticsManager::recordWifiConnected()
{
    if (!lockDiagnostics()) return;
    lastWifiConnectedMs = millis();
    unlockDiagnostics();
}

void DiagnosticsManager::recordWifiDisconnected(uint8_t reason)
{
    if (!lockDiagnostics()) return;
    ++wifiReconnectCount;
    lastWifiDisconnectReason = reason;
    unlockDiagnostics();
}

void DiagnosticsManager::recordMqttConnectAttempt()
{
    if (!lockDiagnostics()) return;
    ++mqttConnectAttempts;
    unlockDiagnostics();
}

void DiagnosticsManager::recordMqttConnected()
{
    if (!lockDiagnostics()) return;
    if (lastMqttConnectedMs != 0) ++mqttReconnectCount;
    lastMqttConnectedMs = millis();
    // Preserve the last transport/broker error after recovery so field
    // diagnostics can still explain why the previous connection dropped.
    unlockDiagnostics();
}

void DiagnosticsManager::recordMqttDisconnected()
{
    // Connection state itself lives in AppState; the counter is updated on the
    // next successful reconnection so initial startup is not counted as one.
}

void DiagnosticsManager::recordMqttError(const String &message)
{
    if (!lockDiagnostics()) return;
    lastMqttError = message;
    unlockDiagnostics();
}

void DiagnosticsManager::recordMqttPublish()
{
    if (!lockDiagnostics()) return;
    lastMqttPublishMs = millis();
    unlockDiagnostics();
}

void DiagnosticsManager::recordMqttCommand()
{
    if (!lockDiagnostics()) return;
    lastMqttCommandMs = millis();
    unlockDiagnostics();
}

void DiagnosticsManager::recordMqttTest(const String &outcome, const String &message)
{
    if (!lockDiagnostics()) return;
    lastMqttTestOutcome = outcome;
    lastMqttTestMessage = message;
    lastMqttTestMs = millis();
    unlockDiagnostics();
}

void DiagnosticsManager::recordNtpSync()
{
    if (!lockDiagnostics()) return;
    lastNtpSyncMs = millis();
    unlockDiagnostics();
}

void DiagnosticsManager::recordBsecSample(bool success, int bsecStatus, int bmeStatus, uint8_t accuracy)
{
    if (!lockDiagnostics()) return;
    lastBsecStatus = bsecStatus;
    lastBmeStatus = bmeStatus;
    lastIaqAccuracy = accuracy;
    if (success)
    {
        lastBsecSampleMs = millis();
        bsecConsecutiveFailures = 0;
    }
    else
    {
        ++bsecFailureCount;
        ++bsecConsecutiveFailures;
    }
    unlockDiagnostics();
}

void DiagnosticsManager::recordSoundSample(bool success)
{
    if (!lockDiagnostics()) return;
    if (success)
    {
        lastSoundSampleMs = millis();
        soundConsecutiveFailures = 0;
    }
    else
    {
        ++soundFailureCount;
        ++soundConsecutiveFailures;
    }
    unlockDiagnostics();
}

void DiagnosticsManager::recordLightSample(bool success)
{
    if (!lockDiagnostics()) return;
    if (success)
    {
        lastLightSampleMs = millis();
        lightConsecutiveFailures = 0;
    }
    else
    {
        ++lightFailureCount;
        ++lightConsecutiveFailures;
    }
    unlockDiagnostics();
}

void DiagnosticsManager::recordLoginSuccess()
{
    if (!lockDiagnostics()) return;
    ++loginSuccessCount;
    unlockDiagnostics();
}

void DiagnosticsManager::recordLoginFailure()
{
    if (!lockDiagnostics()) return;
    ++loginFailureCount;
    unlockDiagnostics();
}

void DiagnosticsManager::recordControlledRestart()
{
    if (!lockDiagnostics()) return;
    lastControlledRestartMs = millis();
    unlockDiagnostics();
}

uint32_t DiagnosticsManager::bootCount()
{
    return bootCounter;
}

uint32_t DiagnosticsManager::watchdogResetCount()
{
    return watchdogCounter;
}

String DiagnosticsManager::resetReasonText()
{
    return resetReasonName(resetReason);
}

String DiagnosticsManager::json()
{
    // Take a consistent RAM snapshot before doing SPIFFS/NVS/certificate work.
    uint32_t wifiReconnects, wifiConnectedMs, mqttReconnects, mqttAttempts;
    uint32_t mqttConnectedMs, mqttPublishMs, mqttCommandMs, mqttTestMs, ntpSyncMs;
    uint32_t bsecMs, soundMs, lightMs;
    uint32_t bsecFailures, soundFailures, lightFailures;
    uint32_t bsecConsecutive, soundConsecutive, lightConsecutive;
    uint32_t loginOk, loginFail, controlledRestartMs;
    uint8_t wifiReason, iaqAccuracy;
    int bsecStatus, bmeStatus;
    String mqttError, mqttTestMessage, mqttTestOutcome;

    if (lockDiagnostics())
    {
        wifiReconnects = wifiReconnectCount;
        wifiReason = lastWifiDisconnectReason;
        wifiConnectedMs = lastWifiConnectedMs;
        mqttReconnects = mqttReconnectCount;
        mqttAttempts = mqttConnectAttempts;
        mqttConnectedMs = lastMqttConnectedMs;
        mqttPublishMs = lastMqttPublishMs;
        mqttCommandMs = lastMqttCommandMs;
        mqttTestMs = lastMqttTestMs;
        mqttTestOutcome = lastMqttTestOutcome;
        mqttError = lastMqttError;
        mqttTestMessage = lastMqttTestMessage;
        ntpSyncMs = lastNtpSyncMs;
        bsecMs = lastBsecSampleMs;
        soundMs = lastSoundSampleMs;
        lightMs = lastLightSampleMs;
        bsecFailures = bsecFailureCount;
        soundFailures = soundFailureCount;
        lightFailures = lightFailureCount;
        bsecConsecutive = bsecConsecutiveFailures;
        soundConsecutive = soundConsecutiveFailures;
        lightConsecutive = lightConsecutiveFailures;
        bsecStatus = lastBsecStatus;
        bmeStatus = lastBmeStatus;
        iaqAccuracy = lastIaqAccuracy;
        loginOk = loginSuccessCount;
        loginFail = loginFailureCount;
        controlledRestartMs = lastControlledRestartMs;
        unlockDiagnostics();
    }
    else
    {
        wifiReconnects = wifiConnectedMs = mqttReconnects = mqttAttempts = 0;
        mqttConnectedMs = mqttPublishMs = mqttCommandMs = mqttTestMs = ntpSyncMs = 0;
        bsecMs = soundMs = lightMs = 0;
        bsecFailures = soundFailures = lightFailures = 0;
        bsecConsecutive = soundConsecutive = lightConsecutive = 0;
        wifiReason = iaqAccuracy = 0;
        bsecStatus = bmeStatus = 0;
        mqttTestOutcome = "";
        loginOk = loginFail = controlledRestartMs = 0;
    }

    const bool wifiConnected = WiFi.status() == WL_CONNECTED;
    const bool fsReady = FilesystemManager::isReady();
    const uint32_t totalFs = fsReady ? static_cast<uint32_t>(SPIFFS.totalBytes()) : 0;
    const uint32_t usedFs = fsReady ? static_cast<uint32_t>(SPIFFS.usedBytes()) : 0;
    const UBaseType_t sensorWatermark = sensorTaskHandle != nullptr ? uxTaskGetStackHighWaterMark(sensorTaskHandle) : 0;
    const UBaseType_t serviceWatermark = serviceTaskHandle != nullptr ? uxTaskGetStackHighWaterMark(serviceTaskHandle) : 0;
    const CertificateInfo cert = CertificateManager::inspect(settings.mqttCaCert);

    String json;
    json.reserve(5200);
    json = "{\"firmware\":{\"name\":\"";
    json += VersionInfo::FIRMWARE_NAME;
    json += "\",\"version\":\"";
    json += VersionInfo::FIRMWARE_VERSION;
    json += "\",\"build\":\"";
    json += jsonEscape(VersionInfo::buildTimestamp());
    json += "\",\"git\":\"";
    json += jsonEscape(VersionInfo::GIT_SHA);
    json += "\",\"board\":\"";
    json += VersionInfo::BOARD_NAME;
    json += "\"},\"runtime\":{\"uptimeSeconds\":";
    json += String(millis() / 1000UL);
    json += ",\"bootCount\":" + String(bootCounter);
    json += ",\"watchdogResetCount\":" + String(watchdogCounter);
    json += ",\"resetReason\":\"" + jsonEscape(resetReasonName(resetReason)) + "\"";
    json += ",\"freeHeapBytes\":" + String(static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
    json += ",\"minFreeHeapBytes\":" + String(static_cast<unsigned long>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)));
    json += ",\"largestFreeBlockBytes\":" + String(static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    json += ",\"controlledRestartAgeSec\":" + String(ageSeconds(controlledRestartMs));
    json += "},\"wifi\":{\"connected\":";
    json += wifiConnected ? "true" : "false";
    json += ",\"rssiDbm\":" + String(wifiConnected ? WiFi.RSSI() : 0);
    json += ",\"reconnectCount\":" + String(wifiReconnects);
    json += ",\"lastDisconnectReason\":" + String(wifiReason);
    const char *wifiReasonText = wifiReason == 0
                                     ? "None since boot"
                                     : WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(wifiReason));
    json += ",\"lastDisconnectReasonText\":\"" + jsonEscape(String(wifiReasonText != nullptr ? wifiReasonText : "Unknown")) + "\"";
    json += ",\"lastConnectedAgeSec\":" + String(ageSeconds(wifiConnectedMs));
    json += "},\"mqtt\":{\"connected\":";
    json += appState.network.mqttConnected ? "true" : "false";
    json += ",\"tls\":";
    json += settings.mqttTlsEnabled ? "true" : "false";
    json += ",\"host\":\"" + jsonEscape(settings.mqttHost) + "\"";
    json += ",\"port\":" + String(settings.mqttPort);
    json += ",\"connectAttempts\":" + String(mqttAttempts);
    json += ",\"reconnectCount\":" + String(mqttReconnects);
    json += ",\"lastConnectedAgeSec\":" + String(ageSeconds(mqttConnectedMs));
    json += ",\"lastPublishAgeSec\":" + String(ageSeconds(mqttPublishMs));
    json += ",\"lastCommandAgeSec\":" + String(ageSeconds(mqttCommandMs));
    json += ",\"lastError\":\"" + jsonEscape(mqttError) + "\"";
    json += ",\"lastTestAgeSec\":" + String(ageSeconds(mqttTestMs));
    json += ",\"lastTestSuccess\":";
    json += mqttTestOutcome == "pass" ? "true" : "false";
    json += ",\"lastTestOutcome\":\"" + jsonEscape(mqttTestOutcome) + "\"";
    json += ",\"lastTestMessage\":\"" + jsonEscape(mqttTestMessage) + "\"}";
    json += ",\"time\":{\"systemTimeValid\":";
    json += TimeManager::systemTimeValid() ? "true" : "false";
    json += ",\"lastNtpSyncAgeSec\":" + String(ageSeconds(ntpSyncMs)) + "}";
    json += ",\"filesystem\":{\"mounted\":";
    json += fsReady ? "true" : "false";
    json += ",\"totalBytes\":" + String(totalFs);
    json += ",\"usedBytes\":" + String(usedFs);
    json += ",\"freeBytes\":" + String(totalFs >= usedFs ? totalFs - usedFs : 0) + "}";
    json += ",\"certificate\":{\"present\":";
    json += cert.present ? "true" : "false";
    json += ",\"validPem\":";
    json += cert.validPem ? "true" : "false";
    json += ",\"timeKnown\":";
    json += cert.timeKnown ? "true" : "false";
    json += ",\"expired\":";
    json += cert.expired ? "true" : "false";
    json += ",\"notYetValid\":";
    json += cert.notYetValid ? "true" : "false";
    json += ",\"subject\":\"" + jsonEscape(cert.subject) + "\"";
    json += ",\"issuer\":\"" + jsonEscape(cert.issuer) + "\"";
    json += ",\"validTo\":\"" + jsonEscape(cert.validTo) + "\"";
    json += ",\"sha256\":\"" + jsonEscape(cert.sha256Fingerprint) + "\"}";
    json += ",\"sensors\":{\"bsec\":{\"healthy\":";
    json += (!stale(bsecMs, Config::BSEC_STALE_AFTER_MS) && bsecConsecutive == 0) ? "true" : "false";
    json += ",\"stale\":";
    json += stale(bsecMs, Config::BSEC_STALE_AFTER_MS) ? "true" : "false";
    json += ",\"lastSampleAgeSec\":" + String(ageSeconds(bsecMs));
    json += ",\"failureCount\":" + String(bsecFailures);
    json += ",\"consecutiveFailures\":" + String(bsecConsecutive);
    json += ",\"bsecStatus\":" + String(bsecStatus);
    json += ",\"bmeStatus\":" + String(bmeStatus);
    json += ",\"iaqAccuracy\":" + String(iaqAccuracy) + "},\"sound\":{\"healthy\":";
    json += (!stale(soundMs, Config::SOUND_LIGHT_STALE_AFTER_MS) && soundConsecutive == 0) ? "true" : "false";
    json += ",\"stale\":";
    json += stale(soundMs, Config::SOUND_LIGHT_STALE_AFTER_MS) ? "true" : "false";
    json += ",\"lastSampleAgeSec\":" + String(ageSeconds(soundMs));
    json += ",\"failureCount\":" + String(soundFailures);
    json += ",\"consecutiveFailures\":" + String(soundConsecutive) + "},\"light\":{\"healthy\":";
    json += (!stale(lightMs, Config::SOUND_LIGHT_STALE_AFTER_MS) && lightConsecutive == 0) ? "true" : "false";
    json += ",\"stale\":";
    json += stale(lightMs, Config::SOUND_LIGHT_STALE_AFTER_MS) ? "true" : "false";
    json += ",\"lastSampleAgeSec\":" + String(ageSeconds(lightMs));
    json += ",\"failureCount\":" + String(lightFailures);
    json += ",\"consecutiveFailures\":" + String(lightConsecutive) + "}}";
    json += ",\"tasks\":{\"sensorStackHighWaterWords\":" + String(static_cast<unsigned long>(sensorWatermark));
    json += ",\"serviceStackHighWaterWords\":" + String(static_cast<unsigned long>(serviceWatermark)) + "}";
    json += ",\"security\":{\"activeSessions\":" + String(WebSecurity::activeSessionCount());
    json += ",\"loginSuccessCount\":" + String(loginOk);
    json += ",\"loginFailureCount\":" + String(loginFail) + "}";
    json += ",\"ota\":{\"lastResult\":\"" + jsonEscape(SettingsManager::getString(Config::NvsKey::OTA_RESULT, "none")) + "\"";
    json += ",\"lastTarget\":\"" + jsonEscape(SettingsManager::getString(Config::NvsKey::OTA_TARGET, "")) + "\"";
    json += ",\"lastSha256\":\"" + jsonEscape(SettingsManager::getString(Config::NvsKey::OTA_SHA256, "")) + "\"}}";
    return json;
}
