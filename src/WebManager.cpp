#include "WebManager.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <cstdlib>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "AdminLoginPage.h"
#include "AppConfig.h"
#include "AppState.h"
#include "CertificateManager.h"
#include "DiagnosticsManager.h"
#include "FilesystemManager.h"
#include "MqttManager.h"
#include "OtaManager.h"
#include "SettingsManager.h"
#include "SharedUiCss.h"
#include "WebSecurity.h"
#include "WifiSettingsPage.h"
#include "WiFiManager.h"
#include "VersionInfo.h"

namespace
{
AsyncWebServer server(80);

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
            if (static_cast<uint8_t>(c) >= 0x20)
            {
                out += c;
            }
            break;
        }
    }

    return out;
}

String processor(const String &var)
{
    const SensorReadings &s = appState.sensors;

    if (var == "esp32_time") return String(millis());
    if (var == "Temp_web") return String(s.temperature, 2);
    if (var == "Hum_web") return String(s.humidity, 2);
    if (var == "AirQuality_web") return String(s.iaq, 2);
    if (var == "CO2_web") return String(s.co2, 2);
    if (var == "VOC_web") return String(s.voc, 2);
    if (var == "Pressure_web") return String(s.pressure, 2);
    if (var == "Sound_web") return String(s.sound, 0);
    if (var == "Light_web") return String(s.light, 0);

    return "";
}

void addSecurityHeaders(AsyncWebServerResponse *response)
{
    if (response == nullptr) return;
    response->addHeader("X-Content-Type-Options", "nosniff");
    response->addHeader("X-Frame-Options", "DENY");
    response->addHeader("Referrer-Policy", "no-referrer");
    response->addHeader("Cross-Origin-Resource-Policy", "same-origin");
    response->addHeader(
        "Content-Security-Policy",
        "default-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
}

void addNoStoreHeaders(AsyncWebServerResponse *response)
{
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    addSecurityHeaders(response);
}

void sendFilesystemUnavailable(AsyncWebServerRequest *request)
{
    // Recovery UI is firmware-resident and uses the same shared visual system
    // as every other Room Monitor page.
    static const char RECOVERY_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#0b1220"><title>Recovery | Evolved.City Room Monitor</title><link rel="stylesheet" href="/ui.css?v=7.3.2"><style>.recovery-card{max-width:720px}.recovery-actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}.recovery-actions a{display:inline-grid;min-height:44px;place-items:center;padding:0 15px;border-radius:10px;text-decoration:none;font-size:.8rem;font-weight:800}.recovery-actions .primary{background:var(--primary);color:#fff}.recovery-actions .secondary{border:1px solid var(--border-strong);background:#fff;color:var(--text)}</style></head><body class="app-page"><header class="topbar"><div class="topbar-inner"><a class="brand" href="/"><span class="brand-mark">E</span><span class="brand-copy"><strong>Evolved.City</strong><small>Room Monitor</small></span></a><div class="topbar-actions"><span class="status-pill"><span class="status-dot locked"></span>Recovery mode</span></div></div></header><main class="page-shell"><section class="page-heading"><div><p class="eyebrow">Device recovery</p><h1>Web filesystem unavailable</h1><p class="page-subtitle">The firmware is running, but the SPIFFS dashboard image could not be mounted.</p></div></section><nav class="page-tabs" aria-label="Room monitor pages"><a class="tab" href="/">Dashboard</a><a class="tab" href="/measurements.html">Measurements</a><a class="tab" href="/wifi">Wi-Fi</a><a class="tab" href="/configuration">Configuration</a><a class="tab" href="/diagnostics">Diagnostics</a><a class="tab active" href="/system">System</a></nav><section class="admin-card recovery-card"><div class="admin-card-body"><h2>Restore the web interface</h2><p>Upload a valid <strong>spiffs.bin</strong> image from the protected System page, then allow the device to restart.</p><div class="recovery-actions"><a class="primary" href="/system">System update</a><a class="secondary" href="/wifi">Wi-Fi settings</a></div></div></section><footer class="page-footer"><span>Evolved.City Room Monitoring</span><span class="footer-separator">•</span><span>Recovery interface</span></footer></main></body></html>
)HTML";

    AsyncWebServerResponse *response = request->beginResponse(503, "text/html", RECOVERY_PAGE);
    addNoStoreHeaders(response);
    request->send(response);
}

void sendWifiPage(AsyncWebServerRequest *request)
{
    if (!WebSecurity::requireAdminPageUnlessProvisioning(request, "/wifi"))
    {
        return;
    }

    AsyncWebServerResponse *response = request->beginResponse(
        200,
        "text/html",
        WifiUi::WIFI_PAGE);
    addNoStoreHeaders(response);
    request->send(response);
}

void scheduleRestartAfterSettingsSave()
{
    // Never delay inside an AsyncWebServer callback. A tiny FreeRTOS task gives
    // the browser time to receive the success response before rebooting.
    xTaskCreate(
        [](void *)
        {
            DiagnosticsManager::recordControlledRestart();
            vTaskDelay(pdMS_TO_TICKS(2200));
            ESP.restart();
            vTaskDelete(nullptr);
        },
        "SettingsRestart",
        2048,
        nullptr,
        1,
        nullptr);
}

String wifiStatusJson()
{
    String connectedSsid;
    if (WiFi.status() == WL_CONNECTED)
    {
        connectedSsid = WiFi.SSID();
    }

    String json;
    json.reserve(300);
    json = "{\"mode\":\"";
    json += jsonEscape(WiFiManager::modeLabel());
    json += "\",\"connectedSsid\":\"";
    json += jsonEscape(connectedSsid);
    json += "\",\"savedSsid\":\"";
    json += jsonEscape(settings.wifiSsid);
    json += "\",\"ip\":\"";
    json += jsonEscape(WiFiManager::activeIpAddress());
    json += "\",\"persistedIp\":\"";
    json += jsonEscape(settings.persistedIp);
    json += "\",\"apMode\":";
    json += WiFiManager::isAccessPointMode() ? "true" : "false";
    json += "}";
    return json;
}

void sendJson(AsyncWebServerRequest *request, int code, const String &body)
{
    AsyncWebServerResponse *response = request->beginResponse(code, "application/json", body);
    addNoStoreHeaders(response);
    request->send(response);
}


String postValue(AsyncWebServerRequest *request, const char *name)
{
    if (request == nullptr || !request->hasParam(name, true))
    {
        return "";
    }

    const AsyncWebParameter *parameter = request->getParam(name, true);
    return parameter != nullptr ? parameter->value() : String("");
}


bool parseUnsignedInRange(
    AsyncWebServerRequest *request,
    const char *name,
    uint32_t minimum,
    uint32_t maximum,
    uint32_t &value,
    String &error)
{
    String text = postValue(request, name);
    text.trim();
    if (text.length() == 0)
    {
        error = String(name) + " is required.";
        return false;
    }

    char *end = nullptr;
    const unsigned long parsed = strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || parsed < minimum || parsed > maximum)
    {
        error = String(name) + " is outside the allowed range.";
        return false;
    }

    value = static_cast<uint32_t>(parsed);
    return true;
}

bool parseIntInRange(
    AsyncWebServerRequest *request,
    const char *name,
    long minimum,
    long maximum,
    int &value,
    String &error)
{
    String text = postValue(request, name);
    text.trim();
    if (text.length() == 0)
    {
        error = String(name) + " is required.";
        return false;
    }

    char *end = nullptr;
    const long parsed = strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || parsed < minimum || parsed > maximum)
    {
        error = String(name) + " is outside the allowed range.";
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

bool parseFloatInRange(
    AsyncWebServerRequest *request,
    const char *name,
    float minimum,
    float maximum,
    float &value,
    String &error)
{
    String text = postValue(request, name);
    text.trim();
    if (text.length() == 0)
    {
        error = String(name) + " is required.";
        return false;
    }

    char *end = nullptr;
    const float parsed = strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed) || parsed < minimum || parsed > maximum)
    {
        error = String(name) + " is outside the allowed range.";
        return false;
    }

    value = parsed;
    return true;
}

bool readRequiredText(
    AsyncWebServerRequest *request,
    const char *name,
    size_t maximumLength,
    String &value,
    String &error)
{
    value = postValue(request, name);
    value.trim();
    if (value.length() == 0)
    {
        error = String(name) + " is required.";
        return false;
    }
    if (value.length() > maximumLength)
    {
        error = String(name) + " is too long.";
        return false;
    }
    return true;
}

bool validateMqttTopic(const String &topic, bool publishTopic, String &error)
{
    if (topic.length() == 0 || topic.length() > 128)
    {
        error = "MQTT topics must contain 1–128 characters.";
        return false;
    }

    for (size_t i = 0; i < topic.length(); ++i)
    {
        if (static_cast<uint8_t>(topic[i]) < 0x20)
        {
            error = "MQTT topics cannot contain control characters.";
            return false;
        }
    }

    if (publishTopic && (topic.indexOf('#') >= 0 || topic.indexOf('+') >= 0))
    {
        error = "The MQTT publish topic cannot contain # or + wildcards.";
        return false;
    }

    if (!publishTopic)
    {
        int start = 0;
        while (start <= static_cast<int>(topic.length()))
        {
            int slash = topic.indexOf('/', start);
            if (slash < 0) slash = topic.length();
            const String level = topic.substring(start, slash);
            if ((level.indexOf('#') >= 0 && level != "#") ||
                (level.indexOf('+') >= 0 && level != "+"))
            {
                error = "MQTT wildcards must occupy an entire topic level.";
                return false;
            }
            if (level == "#" && slash < static_cast<int>(topic.length()))
            {
                error = "MQTT # wildcard is valid only as the final topic level.";
                return false;
            }
            if (slash >= static_cast<int>(topic.length())) break;
            start = slash + 1;
        }
    }

    return true;
}

String normalizePemCertificate(String pem)
{
    return CertificateManager::normalizePem(pem);
}

bool validateCaCertificate(const String &pem, String &error)
{
    return CertificateManager::validatePem(pem, error);
}

String configurationJson()
{
    String json;
    // Keep the general configuration response compact. The CA certificate is
    // fetched lazily from a dedicated protected endpoint only when needed.
    json.reserve(4800);
    json = "{\"nodeId\":\"";
    json += jsonEscape(settings.nodeId);
    json += "\",\"mqttHost\":\"";
    json += jsonEscape(settings.mqttHost);
    json += "\",\"mqttPort\":";
    json += String(settings.mqttPort);
    json += ",\"mqttPublishTopic\":\"";
    json += jsonEscape(settings.mqttPublishTopic);
    json += "\",\"mqttSubscribeTopic\":\"";
    json += jsonEscape(settings.mqttSubscribeTopic);
    json += "\",\"mqttQos\":";
    json += String(settings.mqttQos);
    json += ",\"mqttUsername\":\"";
    json += jsonEscape(settings.mqttUsername);
    json += "\",\"mqttPasswordSet\":";
    json += settings.mqttPassword.length() > 0 ? "true" : "false";
    json += ",\"mqttClientId\":\"";
    json += jsonEscape(settings.mqttClientId);
    json += "\",\"mqttKeepAliveSec\":";
    json += String(settings.mqttKeepAliveSec);
    json += ",\"mqttCleanSession\":";
    json += settings.mqttCleanSession ? "true" : "false";
    json += ",\"mqttPublishRetain\":";
    json += settings.mqttPublishRetain ? "true" : "false";
    json += ",\"mqttTlsEnabled\":";
    json += settings.mqttTlsEnabled ? "true" : "false";
    json += ",\"mqttCaCertSet\":";
    json += settings.mqttCaCert.length() > 0 ? "true" : "false";
    json += ",\"mqttCaCertLength\":";
    json += String(settings.mqttCaCert.length());

    json += ",\"sendIntervalSec\":";
    json += String(settings.sendIntervalSec);
    json += ",\"aliveIntervalSec\":";
    json += String(settings.aliveIntervalSec);

    json += ",\"offsets\":{\"temperature\":";
    json += String(settings.temperatureOffset, 3);
    json += ",\"humidity\":";
    json += String(settings.humidityOffset, 3);
    json += ",\"iaq\":";
    json += String(settings.iaqOffset, 3);
    json += ",\"co2\":";
    json += String(settings.co2Offset, 3);
    json += ",\"voc\":";
    json += String(settings.vocOffset, 3);
    json += ",\"pressure\":";
    json += String(settings.pressureOffset, 3);
    json += ",\"light\":";
    json += String(settings.lightOffset);
    json += ",\"sound\":";
    json += String(settings.soundOffset);
    json += "}";

    json += ",\"deadbands\":{\"temperature\":";
    json += String(settings.dbTemperature, 3);
    json += ",\"humidity\":";
    json += String(settings.dbHumidity, 3);
    json += ",\"pressure\":";
    json += String(settings.dbPressure, 3);
    json += ",\"iaq\":";
    json += String(settings.dbIaq, 3);
    json += ",\"co2\":";
    json += String(settings.dbCo2, 3);
    json += ",\"voc\":";
    json += String(settings.dbVoc, 3);
    json += ",\"sound\":";
    json += String(settings.dbSound, 3);
    json += ",\"light\":";
    json += String(settings.dbLight, 3);
    json += "}";

    json += ",\"sensorIds\":{\"temperature\":\"";
    json += jsonEscape(settings.idTemperature);
    json += "\",\"humidity\":\"";
    json += jsonEscape(settings.idHumidity);
    json += "\",\"co2\":\"";
    json += jsonEscape(settings.idCo2);
    json += "\",\"airQuality\":\"";
    json += jsonEscape(settings.idAirQuality);
    json += "\",\"light\":\"";
    json += jsonEscape(settings.idLight);
    json += "\",\"gas\":\"";
    json += jsonEscape(settings.idGas);
    json += "\",\"pressure\":\"";
    json += jsonEscape(settings.idPressure);
    json += "\",\"sound\":\"";
    json += jsonEscape(settings.idSound);
    json += "\",\"ip\":\"";
    json += jsonEscape(settings.idIp);
    json += "\",\"mac\":\"";
    json += jsonEscape(settings.idMac);
    json += "\",\"alive\":\"";
    json += jsonEscape(settings.idAlive);
    json += "\",\"localAddress\":\"";
    json += jsonEscape(settings.idLocalAddress);
    json += "\",\"ping\":\"";
    json += jsonEscape(settings.idPing);
    json += "\"}}";
    return json;
}

bool buildConfigurationCandidate(
    AsyncWebServerRequest *request,
    AppSettings &candidate,
    String &error)
{
    candidate = settings;

    if (!readRequiredText(request, "nodeId", 64, candidate.nodeId, error)) return false;
    for (size_t i = 0; i < candidate.nodeId.length(); ++i)
    {
        const char c = candidate.nodeId[i];
        const bool allowed = (c >= 'a' && c <= 'z') ||
                             (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') ||
                             c == '-' || c == '_' || c == '.';
        if (!allowed)
        {
            error = "Node ID may contain only letters, numbers, dot, dash, and underscore.";
            return false;
        }
    }
    if (!readRequiredText(request, "mqttHost", 128, candidate.mqttHost, error)) return false;
    if (candidate.mqttHost.indexOf(' ') >= 0 || candidate.mqttHost.indexOf("://") >= 0)
    {
        error = "MQTT host must be only a hostname or IP address (do not include mqtt:// or mqtts://).";
        return false;
    }

    uint32_t numeric = 0;
    if (!parseUnsignedInRange(request, "mqttPort", 1, 65535, numeric, error)) return false;
    candidate.mqttPort = static_cast<uint16_t>(numeric);

    if (!readRequiredText(request, "mqttPublishTopic", 128, candidate.mqttPublishTopic, error)) return false;
    if (!validateMqttTopic(candidate.mqttPublishTopic, true, error)) return false;
    if (!readRequiredText(request, "mqttSubscribeTopic", 128, candidate.mqttSubscribeTopic, error)) return false;
    if (!validateMqttTopic(candidate.mqttSubscribeTopic, false, error)) return false;

    if (!parseUnsignedInRange(request, "mqttQos", 0, 2, numeric, error)) return false;
    candidate.mqttQos = static_cast<uint8_t>(numeric);

    candidate.mqttUsername = postValue(request, "mqttUsername");
    candidate.mqttUsername.trim();
    if (candidate.mqttUsername.length() > 64)
    {
        error = "MQTT username is too long.";
        return false;
    }

    const bool clearMqttCredentials = postValue(request, "clearMqttCredentials") == "1";
    const String submittedMqttPassword = postValue(request, "mqttPassword");
    if (submittedMqttPassword.length() > 128)
    {
        error = "MQTT password is too long.";
        return false;
    }

    if (clearMqttCredentials || candidate.mqttUsername.length() == 0)
    {
        candidate.mqttUsername = "";
        candidate.mqttPassword = "";
    }
    else if (submittedMqttPassword.length() > 0)
    {
        candidate.mqttPassword = submittedMqttPassword;
    }

    candidate.mqttClientId = postValue(request, "mqttClientId");
    candidate.mqttClientId.trim();
    if (candidate.mqttClientId.length() > 64)
    {
        error = "MQTT client ID is too long.";
        return false;
    }

    if (!parseUnsignedInRange(request, "mqttKeepAliveSec", 5, 3600, numeric, error)) return false;
    candidate.mqttKeepAliveSec = static_cast<uint16_t>(numeric);
    candidate.mqttCleanSession = postValue(request, "mqttCleanSession") == "1";
    candidate.mqttPublishRetain = postValue(request, "mqttPublishRetain") == "1";
    candidate.mqttTlsEnabled = postValue(request, "mqttTlsEnabled") == "1";

    const bool clearMqttCaCert = postValue(request, "clearMqttCaCert") == "1";
    String submittedCaCert = postValue(request, "mqttCaCert");
    if (submittedCaCert.length() > Config::MQTT_CA_CERT_MAX_LENGTH + 256)
    {
        error = "The submitted MQTT CA certificate is too large.";
        return false;
    }

    if (clearMqttCaCert)
    {
        candidate.mqttCaCert = "";
    }
    else if (submittedCaCert.length() > 0)
    {
        submittedCaCert = normalizePemCertificate(submittedCaCert);
        if (!validateCaCertificate(submittedCaCert, error)) return false;
        candidate.mqttCaCert = submittedCaCert;
    }

    if (candidate.mqttTlsEnabled && !validateCaCertificate(candidate.mqttCaCert, error))
    {
        return false;
    }

    if (!parseUnsignedInRange(request, "sendIntervalSec", 1, 86400, candidate.sendIntervalSec, error)) return false;
    if (!parseUnsignedInRange(request, "aliveIntervalSec", 1, 86400, candidate.aliveIntervalSec, error)) return false;

    if (!parseFloatInRange(request, "temperatureOffset", -50.0f, 50.0f, candidate.temperatureOffset, error)) return false;
    if (!parseFloatInRange(request, "humidityOffset", -100.0f, 100.0f, candidate.humidityOffset, error)) return false;
    if (!parseFloatInRange(request, "iaqOffset", -500.0f, 500.0f, candidate.iaqOffset, error)) return false;
    if (!parseFloatInRange(request, "co2Offset", -5000.0f, 5000.0f, candidate.co2Offset, error)) return false;
    if (!parseFloatInRange(request, "vocOffset", -50.0f, 50.0f, candidate.vocOffset, error)) return false;
    if (!parseFloatInRange(request, "pressureOffset", -1000.0f, 1000.0f, candidate.pressureOffset, error)) return false;

    if (!parseIntInRange(request, "lightOffset", -100000, 100000, candidate.lightOffset, error)) return false;
    if (!parseIntInRange(request, "soundOffset", -200, 200, candidate.soundOffset, error)) return false;

    if (!parseFloatInRange(request, "dbTemperature", 0.0f, 1000.0f, candidate.dbTemperature, error)) return false;
    if (!parseFloatInRange(request, "dbHumidity", 0.0f, 1000.0f, candidate.dbHumidity, error)) return false;
    if (!parseFloatInRange(request, "dbPressure", 0.0f, 10000.0f, candidate.dbPressure, error)) return false;
    if (!parseFloatInRange(request, "dbIaq", 0.0f, 10000.0f, candidate.dbIaq, error)) return false;
    if (!parseFloatInRange(request, "dbCo2", 0.0f, 100000.0f, candidate.dbCo2, error)) return false;
    if (!parseFloatInRange(request, "dbVoc", 0.0f, 1000.0f, candidate.dbVoc, error)) return false;
    if (!parseFloatInRange(request, "dbSound", 0.0f, 1000.0f, candidate.dbSound, error)) return false;
    if (!parseFloatInRange(request, "dbLight", 0.0f, 1000000.0f, candidate.dbLight, error)) return false;

    struct IdBinding { const char *name; String *target; };
    IdBinding ids[] = {
        {"idTemperature", &candidate.idTemperature},
        {"idHumidity", &candidate.idHumidity},
        {"idCo2", &candidate.idCo2},
        {"idAirQuality", &candidate.idAirQuality},
        {"idLight", &candidate.idLight},
        {"idGas", &candidate.idGas},
        {"idPressure", &candidate.idPressure},
        {"idSound", &candidate.idSound},
        {"idIp", &candidate.idIp},
        {"idMac", &candidate.idMac},
        {"idAlive", &candidate.idAlive},
        {"idLocalAddress", &candidate.idLocalAddress},
        {"idPing", &candidate.idPing},
    };

    for (IdBinding &id : ids)
    {
        if (!readRequiredText(request, id.name, 64, *id.target, error)) return false;
    }

    return true;
}


String configurationExportJson()
{
    // Deliberately excludes Wi-Fi password, MQTT password, administrator
    // credential/session data, and runtime secrets. The CA trust anchor is
    // public information and is included to simplify multi-device deployment.
    String json;
    json.reserve(7600);
    json = "{\"schema\":4,\"firmwareVersion\":\"";
    json += VersionInfo::FIRMWARE_VERSION;
    json += "\",\"nodeId\":\"" + jsonEscape(settings.nodeId) + "\"";
    json += ",\"mqttHost\":\"" + jsonEscape(settings.mqttHost) + "\"";
    json += ",\"mqttPort\":" + String(settings.mqttPort);
    json += ",\"mqttPublishTopic\":\"" + jsonEscape(settings.mqttPublishTopic) + "\"";
    json += ",\"mqttSubscribeTopic\":\"" + jsonEscape(settings.mqttSubscribeTopic) + "\"";
    json += ",\"mqttQos\":" + String(settings.mqttQos);
    json += ",\"mqttUsername\":\"" + jsonEscape(settings.mqttUsername) + "\"";
    json += ",\"mqttClientId\":\"" + jsonEscape(settings.mqttClientId) + "\"";
    json += ",\"mqttKeepAliveSec\":" + String(settings.mqttKeepAliveSec);
    json += ",\"mqttCleanSession\":";
    json += settings.mqttCleanSession ? "true" : "false";
    json += ",\"mqttPublishRetain\":";
    json += settings.mqttPublishRetain ? "true" : "false";
    json += ",\"mqttTlsEnabled\":";
    json += settings.mqttTlsEnabled ? "true" : "false";
    json += ",\"mqttCaCert\":\"" + jsonEscape(settings.mqttCaCert) + "\"";
    json += ",\"sendIntervalSec\":" + String(settings.sendIntervalSec);
    json += ",\"aliveIntervalSec\":" + String(settings.aliveIntervalSec);
    json += ",\"offsets\":{\"temperature\":" + String(settings.temperatureOffset, 3);
    json += ",\"humidity\":" + String(settings.humidityOffset, 3);
    json += ",\"iaq\":" + String(settings.iaqOffset, 3);
    json += ",\"co2\":" + String(settings.co2Offset, 3);
    json += ",\"voc\":" + String(settings.vocOffset, 3);
    json += ",\"pressure\":" + String(settings.pressureOffset, 3);
    json += ",\"light\":" + String(settings.lightOffset);
    json += ",\"sound\":" + String(settings.soundOffset) + "}";
    json += ",\"deadbands\":{\"temperature\":" + String(settings.dbTemperature, 3);
    json += ",\"humidity\":" + String(settings.dbHumidity, 3);
    json += ",\"pressure\":" + String(settings.dbPressure, 3);
    json += ",\"iaq\":" + String(settings.dbIaq, 3);
    json += ",\"co2\":" + String(settings.dbCo2, 3);
    json += ",\"voc\":" + String(settings.dbVoc, 3);
    json += ",\"sound\":" + String(settings.dbSound, 3);
    json += ",\"light\":" + String(settings.dbLight, 3) + "}";
    json += ",\"sensorIds\":{\"temperature\":\"" + jsonEscape(settings.idTemperature) + "\"";
    json += ",\"humidity\":\"" + jsonEscape(settings.idHumidity) + "\"";
    json += ",\"co2\":\"" + jsonEscape(settings.idCo2) + "\"";
    json += ",\"airQuality\":\"" + jsonEscape(settings.idAirQuality) + "\"";
    json += ",\"light\":\"" + jsonEscape(settings.idLight) + "\"";
    json += ",\"gas\":\"" + jsonEscape(settings.idGas) + "\"";
    json += ",\"pressure\":\"" + jsonEscape(settings.idPressure) + "\"";
    json += ",\"sound\":\"" + jsonEscape(settings.idSound) + "\"";
    json += ",\"ip\":\"" + jsonEscape(settings.idIp) + "\"";
    json += ",\"mac\":\"" + jsonEscape(settings.idMac) + "\"";
    json += ",\"alive\":\"" + jsonEscape(settings.idAlive) + "\"";
    json += ",\"localAddress\":\"" + jsonEscape(settings.idLocalAddress) + "\"";
    json += ",\"ping\":\"" + jsonEscape(settings.idPing) + "\"}}";
    return json;
}

void registerRuntimeConfigurationRoutes()
{
    server.on(
        "/api/config",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdmin(request)) return;
            sendJson(request, 200, configurationJson());
        });

    server.on(
        "/api/config/mqtt-ca",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdmin(request)) return;
            AsyncWebServerResponse *response = request->beginResponse(
                200,
                "text/plain; charset=utf-8",
                settings.mqttCaCert);
            addNoStoreHeaders(response);
            request->send(response);
        });

    server.on(
        "/api/config/certificate",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdmin(request)) return;
            sendJson(request, 200, CertificateManager::infoJson(settings.mqttCaCert));
        });

    server.on(
        "/api/config/export",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdmin(request)) return;
            AsyncWebServerResponse *response = request->beginResponse(
                200,
                "application/json",
                configurationExportJson());
            addNoStoreHeaders(response);
            response->addHeader(
                "Content-Disposition",
                "attachment; filename=room-monitor-configuration.json");
            request->send(response);
        });

    server.on(
        "/api/config/test-mqtt",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdmin(request)) return;
            if (!WebSecurity::requireStateChange(request)) return;

            AppSettings candidate;
            String error;
            if (!buildConfigurationCandidate(request, candidate, error))
            {
                String body = "{\"ok\":false,\"message\":\"";
                body += jsonEscape(error);
                body += "\"}";
                sendJson(request, 400, body);
                return;
            }

            sendJson(request, 200, MqttManager::testConnection(candidate));
        });

    server.on(
        "/api/config/save",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdmin(request)) return;
            if (!WebSecurity::requireStateChange(request)) return;

            AppSettings candidate;
            String error;
            if (!buildConfigurationCandidate(request, candidate, error))
            {
                String body = "{\"ok\":false,\"message\":\"";
                body += jsonEscape(error);
                body += "\"}";
                sendJson(request, 400, body);
                return;
            }

            if (!SettingsManager::saveAll(candidate, false))
            {
                sendJson(request, 500, "{\"ok\":false,\"message\":\"Configuration could not be written to NVS.\"}");
                return;
            }

            sendJson(request, 200, "{\"ok\":true,\"message\":\"Configuration saved. Device is restarting to apply node and MQTT settings.\"}");
            scheduleRestartAfterSettingsSave();
        });
}

void registerAdminSecurityRoutes()
{
    // These routes are firmware-resident so login/logout and password recovery
    // remain available even while the SPIFFS web image is being replaced.
    server.on(
        "/api/admin/session",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            const bool authenticated = WebSecurity::isAdminAuthenticated(request);
            String body = "{\"authenticated\":";
            body += authenticated ? "true" : "false";
            body += ",\"username\":\"";
            body += WebSecurity::adminUsername();
            body += "\",\"runtimePassword\":";
            body += WebSecurity::hasRuntimePassword() ? "true" : "false";
            body += "}";
            sendJson(request, 200, body);
        });

    server.on(
        "/api/admin/login",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            const String username = postValue(request, "username");
            const String password = postValue(request, "password");

            String sessionToken;
            String message;
            const bool ok = WebSecurity::login(
                username,
                password,
                sessionToken,
                message);

            String body = "{\"ok\":";
            body += ok ? "true" : "false";
            body += ",\"message\":\"";
            body += jsonEscape(message);
            body += "\"}";

            AsyncWebServerResponse *response = request->beginResponse(
                ok ? 200 : 401,
                "application/json",
                body);
            addNoStoreHeaders(response);
            if (ok)
            {
                WebSecurity::addSessionCookie(response, sessionToken);
            }
            request->send(response);
        });

    server.on(
        "/api/admin/logout",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdmin(request)) return;
            if (!WebSecurity::requireStateChange(request)) return;
            AsyncWebServerResponse *response = request->beginResponse(
                200,
                "application/json",
                "{\"ok\":true,\"message\":\"Signed out.\"}");
            addNoStoreHeaders(response);
            WebSecurity::logout(request, response);
            request->send(response);
        });

    server.on(
        "/api/admin/password",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdmin(request))
            {
                return;
            }
            if (!WebSecurity::requireStateChange(request)) return;

            const String currentPassword = postValue(request, "currentPassword");
            const String newPassword = postValue(request, "newPassword");
            const String confirmPassword = postValue(request, "confirmPassword");

            String message;
            const bool ok = WebSecurity::changeAdminPassword(
                currentPassword,
                newPassword,
                confirmPassword,
                message);

            String body = "{\"ok\":";
            body += ok ? "true" : "false";
            body += ",\"message\":\"";
            body += jsonEscape(message);
            body += "\"}";

            AsyncWebServerResponse *response = request->beginResponse(
                ok ? 200 : 400,
                "application/json",
                body);
            addNoStoreHeaders(response);

            // Password changes invalidate every RAM session. Explicitly expire
            // the browser cookie too so the UI immediately returns to signed-out.
            if (ok)
            {
                WebSecurity::logout(request, response);
            }
            request->send(response);
        });

    server.on(
        "/login",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            AsyncWebServerResponse *response = request->beginResponse(
                200,
                "text/html",
                AdminUi::LOGIN_PAGE);
            addNoStoreHeaders(response);
            request->send(response);
        });
}

String jsonNumber(float value, unsigned int decimals)
{
    // Arduino String(float, decimalPlaces) expects an unsigned int for the
    // decimal-place argument. Using uint8_t here is ambiguous with the integer
    // String constructors in Arduino-ESP32 2.0.17.
    // JSON has no NaN value, so unavailable sensor data is reported as null.
    return std::isfinite(value) ? String(value, decimals) : String("null");
}

String measurementsJson()
{
    const SensorReadings &s = appState.sensors;

    String json;
    json.reserve(260);
    json = "{\"uptimeMs\":";
    json += String(millis());
    json += ",\"temperature\":";
    json += jsonNumber(s.temperature, 2);
    json += ",\"humidity\":";
    json += jsonNumber(s.humidity, 2);
    json += ",\"co2\":";
    json += jsonNumber(s.co2, 2);
    json += ",\"iaq\":";
    json += jsonNumber(s.iaq, 2);
    json += ",\"voc\":";
    json += jsonNumber(s.voc, 2);
    json += ",\"pressure\":";
    json += jsonNumber(s.pressure, 2);
    json += ",\"sound\":";
    json += jsonNumber(s.sound, 0);
    json += ",\"light\":";
    json += jsonNumber(s.light, 0);
    json += "}";
    return json;
}

void addVersionedAssetCacheHeaders(AsyncWebServerResponse *response)
{
    // The HTML refers to CSS through a versioned URL (?v=7.3.2). This allows
    // the browser to keep CSS locally between page changes without risking a
    // stale stylesheet after an update: bump the asset version on each UI
    // release and the URL changes automatically.
    response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
    addSecurityHeaders(response);
}

void registerSharedUiRoute()
{
    server.on(
        "/ui.css",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            AsyncWebServerResponse *response = request->beginResponse(
                200,
                "text/css",
                SharedUi::COMMON_CSS);
            addVersionedAssetCacheHeaders(response);
            request->send(response);
        });
}

void registerStaticAssetRoutes()
{
    // style.css is the largest shared SPIFFS asset. Serve it through an
    // explicit route before the catch-all static handler so page-to-page
    // navigation reuses the browser cache instead of retransmitting ~24 KiB.
    server.on(
        "/style.css",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!FilesystemManager::isReady())
            {
                request->send(503, "text/css", "");
                return;
            }

            AsyncWebServerResponse *response = request->beginResponse(
                SPIFFS,
                "/style.css",
                "text/css");
            addVersionedAssetCacheHeaders(response);
            request->send(response);
        });
}

void registerMeasurementApiRoutes()
{
    // Register this API before the root page route because ESPAsyncWebServer
    // callback handlers can prefix-match child paths. The measurements page
    // polls this single endpoint and keeps its history in browser memory.
    server.on(
        "/api/measurements",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            sendJson(request, 200, measurementsJson());
        });
}

bool saveWifiSettingsFromRequest(AsyncWebServerRequest *request, String &message)
{
    if (!request->hasParam("ssid", true))
    {
        message = "Wi-Fi network is required.";
        return false;
    }

    const AsyncWebParameter *ssidParam = request->getParam("ssid", true);
    if (ssidParam == nullptr)
    {
        message = "Wi-Fi network is required.";
        return false;
    }

    String newSsid = ssidParam->value();
    newSsid.trim();

    if (newSsid.length() == 0 || newSsid.length() > 32)
    {
        message = "Select a valid Wi-Fi network name.";
        return false;
    }

    String newPassword;
    if (request->hasParam("pass", true))
    {
        const AsyncWebParameter *passParam = request->getParam("pass", true);
        if (passParam != nullptr)
        {
            newPassword = passParam->value();
        }
    }

    // A blank password keeps the existing password only when the SSID did not
    // change. For a different SSID, blank continues to mean an open network.
    if (newPassword.length() == 0)
    {
        if (newSsid == settings.wifiSsid && settings.wifiPassword.length() > 0)
        {
            newPassword = settings.wifiPassword;
        }
        else
        {
            newPassword = "myNull";
        }
    }

    String newPersistedIp = "NaV";
    if (request->hasParam("ip", true))
    {
        const AsyncWebParameter *ipParam = request->getParam("ip", true);
        if (ipParam != nullptr)
        {
            newPersistedIp = ipParam->value();
            newPersistedIp.trim();
            if (newPersistedIp.length() == 0)
            {
                newPersistedIp = "NaV";
            }
        }
    }

    const bool ssidSaved = SettingsManager::saveString(Config::NvsKey::WIFI_SSID, newSsid);
    const bool passwordSaved = SettingsManager::saveString(Config::NvsKey::WIFI_PASSWORD, newPassword);
    const bool ipSaved = SettingsManager::saveString(Config::NvsKey::LAST_IP, newPersistedIp);

    if (!ssidSaved || !passwordSaved || !ipSaved)
    {
        message = "The Wi-Fi settings could not be written to NVS.";
        return false;
    }

    settings.wifiSsid = newSsid;
    settings.wifiPassword = newPassword;
    settings.persistedIp = newPersistedIp;
    appState.network.ip = newPersistedIp;

    message = "Wi-Fi settings saved successfully. Device is restarting…";
    return true;
}


void registerConfigurationPageRoutes()
{
    // Device configuration is intentionally a separate protected page rather
    // than part of the live Dashboard. Register both friendly and file-style
    // URLs before the catch-all static handler so the SPIFFS file cannot be
    // fetched without an administrator session. The route is also available
    // in provisioning mode, where it still requires administrator login.
    auto sendConfigurationPage = [](AsyncWebServerRequest *request)
    {
        if (!WebSecurity::requireAdminPage(request, "/configuration"))
        {
            return;
        }

        if (!FilesystemManager::isReady())
        {
            sendFilesystemUnavailable(request);
            return;
        }

        AsyncWebServerResponse *response = request->beginResponse(
            SPIFFS,
            "/configuration.html",
            "text/html");
        addNoStoreHeaders(response);
        request->send(response);
    };

    server.on("/configuration", HTTP_GET, sendConfigurationPage);
    server.on("/configuration.html", HTTP_GET, sendConfigurationPage);
}

void registerWifiManagementRoutes()
{
    // Register API endpoints before page routes. ESPAsyncWebServer treats a
    // route such as /wifi as a prefix for GET subpaths, so specific handlers
    // must appear first.
    server.on(
        "/api/wifi/status",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdminUnlessProvisioning(request)) return;
            sendJson(request, 200, wifiStatusJson());
        });

    server.on(
        "/api/wifi/scan",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdminUnlessProvisioning(request)) return;
            if (!WebSecurity::requireStateChange(request)) return;
            WiFiManager::requestNetworkScan();
            sendJson(request, 202, "{\"ok\":true,\"message\":\"Wi-Fi scan started.\"}");
        });

    server.on(
        "/api/wifi/networks",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdminUnlessProvisioning(request)) return;
            sendJson(request, 200, WiFiManager::networkScanJson());
        });

    server.on(
        "/wifi/save",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdminUnlessProvisioning(request)) return;
            if (!WebSecurity::requireStateChange(request)) return;
            String message;
            const bool ok = saveWifiSettingsFromRequest(request, message);

            String body = "{\"ok\":";
            body += ok ? "true" : "false";
            body += ",\"message\":\"";
            body += jsonEscape(message);
            body += "\"}";

            sendJson(request, ok ? 200 : 400, body);
            if (ok)
            {
                scheduleRestartAfterSettingsSave();
            }
        });

    // Compatibility endpoint for earlier Wi-Fi manager forms.
    server.on(
        "/setssid",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdminUnlessProvisioning(request)) return;
            // Preserve form-style compatibility only in factory-reset provisioning.
            // During normal authenticated operation, require the same CSRF barrier
            // as the current /wifi/save API.
            if (!WebSecurity::isProvisioningMode() && !WebSecurity::requireStateChange(request)) return;
            String message;
            const bool ok = saveWifiSettingsFromRequest(request, message);
            AsyncWebServerResponse *response = request->beginResponse(
                ok ? 200 : 400,
                "text/plain",
                message);
            addNoStoreHeaders(response);
            request->send(response);

            if (ok)
            {
                scheduleRestartAfterSettingsSave();
            }
        });

    server.on(
        "/wifimanager.html",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdminPageUnlessProvisioning(request, "/wifi")) return;
            request->redirect("/wifi");
        });

    server.on(
        "/system/wifi",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdminPageUnlessProvisioning(request, "/wifi")) return;
            request->redirect("/wifi");
        });

    server.on(
        "/wifi",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            sendWifiPage(request);
        });
}

void registerDiagnosticsRoutes()
{
    server.on(
        "/api/diagnostics",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdmin(request)) return;
            sendJson(request, 200, DiagnosticsManager::json());
        });

    auto sendDiagnosticsPage = [](AsyncWebServerRequest *request)
    {
        if (!WebSecurity::requireAdminPage(request, "/diagnostics")) return;
        if (!FilesystemManager::isReady())
        {
            sendFilesystemUnavailable(request);
            return;
        }

        AsyncWebServerResponse *response = request->beginResponse(
            SPIFFS,
            "/diagnostics.html",
            "text/html");
        addNoStoreHeaders(response);
        request->send(response);
    };

    server.on("/diagnostics", HTTP_GET, sendDiagnosticsPage);
    server.on("/diagnostics.html", HTTP_GET, sendDiagnosticsPage);
}

void startWifiManager()
{
    WiFiManager::startConfigurationAccessPoint();
    WiFiManager::requestNetworkScan();

    server.on(
        "/",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            // Factory-reset setup must not depend on SPIFFS. The complete
            // Wi-Fi manager is embedded in firmware and remains recoverable.
            sendWifiPage(request);
        });
}

void startNormalWebUi()
{
    server.on(
        "/",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!FilesystemManager::isReady())
            {
                sendFilesystemUnavailable(request);
                return;
            }

            AsyncWebServerResponse *response = request->beginResponse(
                SPIFFS,
                "/index.html",
                "text/html",
                false,
                processor);
            addNoStoreHeaders(response);
            request->send(response);
        });

    // Measurements are fetched by JavaScript from /api/measurements so this
    // page can accumulate live history without reloading.
    server.on(
        "/measurements.html",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!FilesystemManager::isReady())
            {
                sendFilesystemUnavailable(request);
                return;
            }

            // This page is API-driven rather than template-driven. Serving it
            // raw avoids '%' characters in chart JavaScript/labels being
            // interpreted as ESPAsyncWebServer template delimiters.
            AsyncWebServerResponse *response = request->beginResponse(
                SPIFFS,
                "/measurements.html",
                "text/html");
            addNoStoreHeaders(response);
            request->send(response);
        });

    if (FilesystemManager::isReady())
    {
        // HTML and any unversioned fallback assets remain no-store. Shared
        // versioned CSS is handled by the explicit /ui.css and /style.css
        // routes above and receives long-lived immutable caching.
        server.serveStatic("/", SPIFFS, "/")
            .setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
    }
}
} // namespace

void WebManager::begin()
{
    // Shared firmware-resident CSS is registered first so every page, including
    // recovery/admin pages, uses the same typography, sizing, and navigation.
    registerSharedUiRoute();
    registerStaticAssetRoutes();

    // Session login/logout/password routes must be available before any protected
    // page handlers and before the root/static handlers.
    registerAdminSecurityRoutes();
    registerRuntimeConfigurationRoutes();
    registerConfigurationPageRoutes();

    // Specific API routes must be registered before /system, the root page,
    // and the catch-all static handler.
    registerWifiManagementRoutes();
    registerMeasurementApiRoutes();
    registerDiagnosticsRoutes();

    // Register system/info before /system inside OtaManager. This is important
    // because ESPAsyncWebServer uses prefix matching for callback handlers.
    OtaManager::begin(server);

    if (settings.wifiSsid.length() == 0)
    {
        startWifiManager();
    }
    else
    {
        startNormalWebUi();
    }

    server.begin();

    Serial.println("Web server started. System/OTA endpoint: /system");
    Serial.println("Wi-Fi manager endpoint: /wifi");
    Serial.println("Protected configuration endpoint: /configuration");
    Serial.println("Diagnostics endpoint: /diagnostics");
}
