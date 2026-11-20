# Room Monitor Architecture

## Flash/storage separation

```text
ESP32 flash
├── Application firmware
│   ├── sensors / MQTT / display / tasks
│   ├── /system OTA UI (firmware-resident)
│   └── /wifi network manager (firmware-resident)
├── Preferences / NVS
│   ├── Wi-Fi credentials
│   ├── last IP
│   ├── Node ID + MQTT broker/topics/credentials
│   ├── reporting intervals
│   ├── sensor offsets + deadbands
│   ├── backend measurement IDs
│   └── settings schema
└── SPIFFS
    ├── index.html
    ├── measurements.html
    ├── style.css
    └── wifimanager.html (compatibility redirect)
```

The two administrative pages are deliberately firmware-resident. `/system` can replace SPIFFS while SPIFFS is unmounted, and `/wifi` remains available after a factory reset even if the web filesystem is unavailable.

## Web route ordering

ESPAsyncWebServer callback handlers can match child paths by prefix. Therefore route ordering is intentional:

1. Specific APIs and Wi-Fi compatibility paths.
2. `GET /system/info`.
3. `GET /system`.
4. OTA POST endpoints.
5. Dashboard/measurement pages.
6. Static SPIFFS catch-all last.

This prevents the `/system` HTML handler from intercepting `/system/info` and prevents static files from shadowing administration APIs.

## Wi-Fi behavior

Normal operation uses station mode. After factory reset there is no saved SSID, so the firmware starts `RoomMonitoring-WIFI-MANAGER` in AP+STA mode. AP+STA keeps `192.168.4.1` reachable while the station interface performs asynchronous scans.

The Wi-Fi manager exposes a strongest-first list of unique detected SSIDs with RSSI and open/secured state, plus manual entry for hidden networks. Saving configuration writes to NVS and restarts the device.

## OTA behavior

`OtaManager` uses the ESP32 `Update` library directly; AsyncElegantOTA is not used. Firmware and filesystem targets have separate POST endpoints. A random boot ID is returned by `/system/info` so the browser can distinguish the pre-update process from the newly restarted device.

## Modules

```text
main.cpp               startup orchestration
FilesystemManager      SPIFFS mount/unmount
SettingsManager        NVS + legacy migration
SensorManager          BSEC/BME680, BH1750, SEN0232
WiFiManager            STA/AP, reconnect, mDNS, async network scan
MqttManager            ESP-IDF ESP-MQTT transport, optional TLS/CA verification, commands
TelemetryService       payloads/deadbands
WebManager             dashboard, runtime-config APIs, Wi-Fi APIs/pages, recovery routes
OtaManager             firmware/SPIFFS OTA
SystemUpdatePage.h     firmware-resident /system UI
WifiSettingsPage.h     firmware-resident /wifi UI
TimeManager            NTP/display time + SNTP system clock for TLS certificate validation
DisplayManager         MAX72xx/Parola
TaskManager            FreeRTOS scheduling + factory-reset check
```

## Shared UI and administration security

`SharedUiCss.h` provides the firmware-resident `/ui.css` design system used by all visible pages. `WebSecurity` centralizes administrator authentication. The System/OTA endpoints are always protected; Wi-Fi management is protected during normal operation and intentionally open only when the device is in factory-reset provisioning state with no saved SSID.

## Administrator session architecture (2026-09-14)

`WebSecurity` owns administrator authentication. Protected routes no longer depend on browser HTTP Basic credential caching. `/api/admin/login` validates the administrator username/password and issues an in-RAM random session token in an `HttpOnly`, `SameSite=Strict` cookie with an 8-hour maximum browser lifetime. `/api/admin/logout` invalidates the current token. ESP32 reboot invalidates all tokens because session state is never persisted.

The initial password comes from `include/Secrets.h`. Once changed from the Dashboard, `WebSecurity` writes one versioned salted credential record to NVS under `adm_cred`. `SettingsManager::factoryReset()` clears that record together with the rest of the Room Monitor namespace, so factory reset restores the bootstrap password.

SPIFFS is not involved in authentication state. The login, Wi-Fi, and System pages remain firmware-resident so an administrator can still authenticate and recover the device when SPIFFS is unavailable.

## Central runtime configuration (v5-v7)

The protected `/configuration` page is the primary deployment-configuration surface. `/api/config` returns non-secret current values and `/api/config/save` validates and persists a complete candidate `AppSettings`. Passwords are write-only. The configuration is deliberately applied after reboot rather than changing MQTT pointer-backed strings in place.

`AppSettings` now owns Node ID, MQTT host/port/topics/QoS/optional credentials/client ID/keep-alive/session/retain settings, reporting intervals, offsets, deadbands, and backend record IDs. `MqttManager`, `TelemetryService`, and `OtaManager` consume those values from runtime settings rather than compile-time identity/broker constants. Schema v1 is migrated through schema v2 to schema v3 without overwriting existing Wi-Fi/calibration/timing/security data. Schema v3 adds MQTT TLS enablement and the broker CA certificate.


## Secure MQTT architecture (v7)

`MqttManager` now uses the ESP-IDF `esp-mqtt` client exposed by Arduino-ESP32 rather than the former AsyncMqttClient dependency. Plain MQTT uses `mqtt://`; secure MQTT uses `mqtts://`. When secure mode is selected, the broker root CA stored in NVS is supplied through `esp_mqtt_client_config_t::cert_pem` and common-name/SAN checking remains enabled.

ESP-MQTT automatic broker reconnect remains enabled, while the Room Monitor service loop provides an explicit reconnect kick after Wi-Fi recovery. Publishing uses `esp_mqtt_client_enqueue()` so MQTT transmission occurs in the MQTT task rather than blocking the service task. QoS 0/1/2, username/password, client ID, clean-session, retain, publish topic and subscribe topic remain runtime configurable.

`TimeManager` starts both the existing NTPClient display clock and the ESP32 SNTP/system clock. TLS broker connection is held until `time(nullptr)` is plausible, preventing certificate-validity checks from running against the reset/epoch clock.

## v7.3 diagnostics and reliability modules

`DiagnosticsManager` owns operational counters and health snapshots without taking over normal sensing/communications responsibilities. `CertificateManager` owns X.509 CA parsing/inspection. `VersionInfo` is the single firmware version/build identity source. Deep engineering state is exposed through protected `/diagnostics`, while `/system` remains a concise OTA/health surface.

The architecture intentionally stops here rather than splitting every small responsibility into another class. MQTT wire compatibility and sensor/backend mappings remain owned by the existing modules.
