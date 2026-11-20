#include "WiFiManager.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "AppConfig.h"
#include "AppState.h"
#include "DiagnosticsManager.h"
#include "MqttManager.h"
#include "SettingsManager.h"
#include "TelemetryService.h"
#include "TimeManager.h"

namespace
{
TimerHandle_t wifiReconnectTimer = nullptr;
bool mdnsStarted = false;
bool scanRequested = false;
bool stationConnectionStarted = false;
String cachedScanJson = "{\"scanning\":false,\"networks\":[]}";

constexpr size_t MAX_WEB_SCAN_NETWORKS = 32;

struct ScanEntry
{
    String ssid;
    int32_t rssi = -127;
    bool secure = false;
};

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

void ensureScanCapableMode()
{
    const wifi_mode_t mode = WiFi.getMode();

    if (mode == WIFI_MODE_NULL)
    {
        WiFi.mode(WIFI_STA);
    }
    else if (mode == WIFI_MODE_AP)
    {
        // Preserve the configuration AP while enabling the station interface
        // required for background network scans.
        WiFi.mode(WIFI_AP_STA);
    }
}

String buildScanResultJson(int count)
{
    ScanEntry entries[MAX_WEB_SCAN_NETWORKS];
    size_t used = 0;

    for (int i = 0; i < count; ++i)
    {
        String ssid = WiFi.SSID(i);
        ssid.trim();

        // Empty SSIDs are hidden networks. The UI provides a manual-entry
        // option for them because a hidden scan result cannot identify a name.
        if (ssid.length() == 0)
        {
            continue;
        }

        const int32_t rssi = WiFi.RSSI(i);
        const bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

        bool duplicate = false;
        for (size_t j = 0; j < used; ++j)
        {
            if (entries[j].ssid == ssid)
            {
                duplicate = true;
                // Keep the strongest access point when several radios publish
                // the same SSID.
                if (rssi > entries[j].rssi)
                {
                    entries[j].rssi = rssi;
                    entries[j].secure = secure;
                }
                break;
            }
        }

        if (!duplicate && used < MAX_WEB_SCAN_NETWORKS)
        {
            entries[used].ssid = ssid;
            entries[used].rssi = rssi;
            entries[used].secure = secure;
            ++used;
        }
    }

    // Strongest network first. The list is intentionally small, so this
    // selection sort avoids pulling in another container dependency.
    for (size_t i = 0; i < used; ++i)
    {
        size_t strongest = i;
        for (size_t j = i + 1; j < used; ++j)
        {
            if (entries[j].rssi > entries[strongest].rssi)
            {
                strongest = j;
            }
        }

        if (strongest != i)
        {
            ScanEntry temp = entries[i];
            entries[i] = entries[strongest];
            entries[strongest] = temp;
        }
    }

    String json;
    json.reserve(128 + used * 80);
    json = "{\"scanning\":false,\"networks\":[";

    for (size_t i = 0; i < used; ++i)
    {
        if (i > 0)
        {
            json += ',';
        }

        json += "{\"ssid\":\"";
        json += jsonEscape(entries[i].ssid);
        json += "\",\"rssi\":";
        json += String(entries[i].rssi);
        json += ",\"secure\":";
        json += entries[i].secure ? "true" : "false";
        json += '}';
    }

    json += "]}";
    return json;
}

void startMdns()
{
    if (mdnsStarted || !appState.network.domain.length())
    {
        return;
    }

    if (MDNS.begin(appState.network.domain.c_str()))
    {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
    }
    else
    {
        Serial.println("mDNS start failed.");
    }
}

void reconnectTimerCallback(TimerHandle_t)
{
    // Periodic retry is intentional. A one-shot retry can leave the node
    // offline forever when the access point is unavailable for longer than
    // the first retry window. WiFi.reconnect()/begin() are safe to repeat.
    WiFiManager::connect();
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    switch (event)
    {
    case SYSTEM_EVENT_STA_GOT_IP:
        appState.network.wifiConnected = true;
        stationConnectionStarted = true;
        if (wifiReconnectTimer != nullptr)
        {
            xTimerStop(wifiReconnectTimer, 0);
        }
        WiFiManager::refreshIp();

        if (appState.network.mqttConnected)
        {
            TelemetryService::queueDeviceInfo();
        }

        TimeManager::begin();
        startMdns();
        MqttManager::handleWifiConnected();

        DiagnosticsManager::recordWifiConnected();
        Serial.print("Wi-Fi connected: ");
        Serial.println(appState.network.ip);
        break;

    case SYSTEM_EVENT_STA_DISCONNECTED:
        appState.network.wifiConnected = false;
        appState.network.mqttConnected = false;
        DiagnosticsManager::recordWifiDisconnected(info.wifi_sta_disconnected.reason);

        if (mdnsStarted)
        {
            MDNS.end();
            mdnsStarted = false;
        }

        MqttManager::handleWifiDisconnected();

        if (wifiReconnectTimer != nullptr && settings.wifiSsid.length() > 0)
        {
            // Auto-reload timer keeps trying until SYSTEM_EVENT_STA_GOT_IP
            // stops it. xTimerReset also restarts an already-active timer.
            xTimerReset(wifiReconnectTimer, 0);
        }
        break;

    default:
        break;
    }
}
} // namespace

void WiFiManager::begin()
{
    WiFi.onEvent(onWiFiEvent);

    // Credentials live in NVS through SettingsManager, so Arduino Wi-Fi does
    // not need to write its own copy to flash. Auto reconnect complements the
    // periodic retry timer below.
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.mode(WIFI_STA);
    appState.network.mac = WiFi.macAddress();

    appState.network.domain = appState.network.mac;
    appState.network.domain.replace(":", "");
    appState.network.domain.toLowerCase();

    appState.network.localAddress =
        "http://" + appState.network.domain + ".local";

    wifiReconnectTimer = xTimerCreate(
        "wifiReconnect",
        pdMS_TO_TICKS(Config::WIFI_RECONNECT_INTERVAL_MS),
        pdTRUE,
        nullptr,
        reconnectTimerCallback);
}

void WiFiManager::connect()
{
    if (settings.wifiSsid.length() == 0)
    {
        return;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        appState.network.wifiConnected = true;
        if (wifiReconnectTimer != nullptr)
        {
            xTimerStop(wifiReconnectTimer, 0);
        }
        return;
    }

    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_NULL)
    {
        WiFi.mode(WIFI_STA);
    }
    else if (mode == WIFI_MODE_AP)
    {
        WiFi.mode(WIFI_AP_STA);
    }

    WiFi.setAutoReconnect(true);

    bool retryStarted = false;
    if (stationConnectionStarted)
    {
        // Reuse the already configured credentials first. This is the normal
        // path after a temporary AP/router outage.
        retryStarted = WiFi.reconnect();
    }

    if (!stationConnectionStarted || !retryStarted)
    {
        // Initial connection, or recovery when reconnect() has no usable STA
        // configuration. Wi-Fi credentials are sourced from Room Monitor NVS.
        if (settings.wifiPassword.length() == 0 || settings.wifiPassword == "myNull")
        {
            WiFi.begin(settings.wifiSsid.c_str(), nullptr);
        }
        else
        {
            WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPassword.c_str());
        }
        stationConnectionStarted = true;
    }

    if (wifiReconnectTimer != nullptr && xTimerIsTimerActive(wifiReconnectTimer) == pdFALSE)
    {
        // Keep retrying periodically until GOT_IP stops the timer.
        xTimerStart(wifiReconnectTimer, 0);
    }
}

void WiFiManager::refreshIp()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    const String newIp = WiFi.localIP().toString();
    if (newIp != appState.network.ip)
    {
        appState.network.ip = newIp;
        settings.persistedIp = newIp;
        SettingsManager::saveString(Config::NvsKey::LAST_IP, newIp);

        Serial.print("IP updated: ");
        Serial.println(newIp);
    }
}

String WiFiManager::scanNetworks()
{
    ensureScanCapableMode();
    const int count = WiFi.scanNetworks(false, true);
    String scannedSsids;

    for (int i = 0; i < count; ++i)
    {
        const String ssid = WiFi.SSID(i);
        if (ssid.length() == 0)
        {
            continue;
        }

        if (scannedSsids.length() > 0)
        {
            scannedSsids += '|';
        }
        scannedSsids += ssid;
    }

    WiFi.scanDelete();
    return scannedSsids;
}

void WiFiManager::requestNetworkScan()
{
    ensureScanCapableMode();

    const int existing = WiFi.scanComplete();
    if (existing == WIFI_SCAN_RUNNING)
    {
        scanRequested = true;
        return;
    }

    if (existing >= 0)
    {
        WiFi.scanDelete();
    }

    scanRequested = true;
    cachedScanJson = "{\"scanning\":true,\"networks\":[]}";

    const int result = WiFi.scanNetworks(true, true);
    if (result == WIFI_SCAN_FAILED)
    {
        scanRequested = false;
        cachedScanJson = "{\"scanning\":false,\"error\":\"Could not start Wi-Fi scan.\",\"networks\":[]}";
        Serial.println("Wi-Fi scan could not be started.");
    }
    else
    {
        Serial.println("Wi-Fi background scan started.");
    }
}

String WiFiManager::networkScanJson()
{
    const int result = WiFi.scanComplete();

    if (result == WIFI_SCAN_RUNNING)
    {
        return "{\"scanning\":true,\"networks\":[]}";
    }

    if (result == WIFI_SCAN_FAILED)
    {
        if (scanRequested)
        {
            scanRequested = false;
            cachedScanJson = "{\"scanning\":false,\"error\":\"Wi-Fi scan failed. Try Refresh scan.\",\"networks\":[]}";
        }
        return cachedScanJson;
    }

    if (result >= 0)
    {
        cachedScanJson = buildScanResultJson(result);
        scanRequested = false;
        WiFi.scanDelete();
        return cachedScanJson;
    }

    return cachedScanJson;
}

void WiFiManager::startConfigurationAccessPoint()
{
    // AP+STA allows the setup page to remain reachable at 192.168.4.1 while
    // the station interface scans surrounding Wi-Fi networks.
    WiFi.mode(WIFI_AP_STA);
    stationConnectionStarted = false;
    if (wifiReconnectTimer != nullptr)
    {
        xTimerStop(wifiReconnectTimer, 0);
    }
    WiFi.softAP(Config::WIFI_MANAGER_AP_NAME);

    appState.network.wifiConnected = false;
    appState.network.ip = WiFi.softAPIP().toString();

    Serial.print("Wi-Fi configuration AP started: ");
    Serial.print(Config::WIFI_MANAGER_AP_NAME);
    Serial.print(" at ");
    Serial.println(WiFi.softAPIP());
}

bool WiFiManager::isAccessPointMode()
{
    const wifi_mode_t mode = WiFi.getMode();
    return mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA;
}

String WiFiManager::activeIpAddress()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return WiFi.localIP().toString();
    }

    if (isAccessPointMode())
    {
        return WiFi.softAPIP().toString();
    }

    return appState.network.ip;
}

String WiFiManager::modeLabel()
{
    const wifi_mode_t mode = WiFi.getMode();

    if (mode == WIFI_MODE_APSTA)
    {
        return "Access point + station";
    }
    if (mode == WIFI_MODE_AP)
    {
        return "Access point";
    }
    if (mode == WIFI_MODE_STA)
    {
        return WiFi.status() == WL_CONNECTED ? "Station · connected" : "Station · connecting";
    }

    return "Wi-Fi off";
}
