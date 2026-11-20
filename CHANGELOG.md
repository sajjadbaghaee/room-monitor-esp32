# Room Monitor Changelog
## v7.3.2 - MQTT commissioning-test reliability

- Fixed the Configuration-page MQTT/TLS test so active saved settings reuse the already-connected production MQTT session rather than allocating a second TLS client.
- Track the production command-topic SUBACK and report it in the live commissioning result.
- Added a lightweight raw TCP precheck for unsaved broker settings so TCP reachability is reported independently from TLS.
- Added heap safety thresholds before starting an independent second ESP-MQTT/TLS session; low-memory cases now report PARTIAL / NOT RUN instead of false TCP/TLS failures.
- Added detailed independent-test diagnostics for ESP-TLS error, TLS stack error, certificate verify flags, socket errno, CONNACK code, free heap, and largest free block.
- Updated Diagnostics to preserve PASS / FAIL / PARTIAL test outcomes.

## v7.3.1 - Flash-size optimization and MQTT test reuse

- Moved the protected Diagnostics page from a firmware-resident raw HTML string to SPIFFS (`data/diagnostics.html`). The `/diagnostics` route remains authenticated and `no-store`, but the page no longer consumes application-partition flash.
- Reworked the Configuration-page MQTT connection test to use a temporary ESP-IDF ESP-MQTT client instead of linking an additional Arduino `WiFiClientSecure` commissioning path and hand-built MQTT packets. TLS CA and hostname verification remain enabled.
- Preserved the default ESP32 OTA partition layout so v7.2/v7.3 devices can continue updating with normal `firmware.bin` OTA; no USB-only partition-table migration is required.
- Bumped firmware/UI version to 7.3.1 and updated static validation for the SPIFFS-hosted Diagnostics page.


## v7.3.0 - Diagnostics, reliability and verified OTA

- Added protected firmware-resident Diagnostics page/API with firmware/build, boot/reset/watchdog, memory, filesystem, Wi-Fi, MQTT/TLS, certificate, task, sensor, session and OTA audit information.
- Added `VersionInfo`, `DiagnosticsManager`, and `CertificateManager` modules.
- Added X.509 CA inspection (subject, issuer, validity and SHA-256 fingerprint).
- Added non-persistent MQTT/TLS commissioning test covering DNS, TCP, time, CA, TLS, MQTT login and command-topic subscription.
- Added configuration JSON export/import without Wi-Fi password, MQTT password, or administrator secrets.
- Strengthened runtime configuration validation.
- Added task watchdog supervision and persistent watchdog-reset counter.
- Added BME68x/BSEC, sound and light freshness/failure diagnostics.
- Added CSRF/origin protection for current state-changing browser APIs plus additional security response headers.
- Added SHA-256 verified OTA, firmware image magic check, filesystem/firmware target sanity check, OTA audit metadata and version-aware reboot confirmation.
- Added static regression tests, inline JavaScript syntax checks, and GitHub Actions PlatformIO firmware/SPIFFS build workflow.
- Preserved v7.2 TLS, caching/performance, MQTT protocol, NVS settings, reconnect behavior and backend mappings.

## v7.2 - Web performance and TLS memory optimization

- Unified all page stylesheet URLs on `?v=7.2.0` and enabled one-year immutable browser caching for versioned `ui.css` and `style.css`.
- Kept HTML and API responses `no-store`, so live data and protected pages are never served stale.
- Moved the MQTT broker CA PEM out of the general `/api/config` JSON response; Configuration now fetches it lazily from protected `/api/config/mqtt-ca`.
- Unchanged stored CA PEM is no longer retransmitted by the browser on unrelated configuration saves.
- Reduced the ESP-MQTT receive buffer from 4096 to 1024 bytes while retaining the 4096-byte transmit buffer required for full telemetry payload headroom. Incoming command fragmentation remains supported.
- New/factory-reset configurations now default MQTT keep-alive to 60 seconds; existing NVS values are preserved.
- Added System diagnostics for free heap, minimum free heap, largest free block, Wi-Fi RSSI, and uptime.
- MQTT TLS, CA verification, hostname verification, username/password authentication, QoS behavior, NVS persistence, and backend mappings are unchanged.

# v7.1

- Fixed MQTT CA certificate UI persistence visibility after reboot.
- Added PEM certificate file browser/loader.
- Added TLS hostname/IP guidance and detailed TLS serial diagnostics.

# v6.1

- Fixed Dashboard humidity icon and value rendering by escaping literal percent characters in `data/index.html`.
- SPIFFS-only UI correction; no firmware logic change.


## v6 - Dedicated configuration page and resilient reconnect
- Moved runtime Device Configuration out of the Dashboard into protected `/configuration`.
- Added Configuration navigation tab across Dashboard, Measurements, Wi-Fi, System and recovery UI.
- Factory/default administrator credentials were added in an early release; public builds now require a locally generated unique bootstrap password.
- Wi-Fi reconnection now uses both ESP32 auto-reconnect and a periodic 10-second retry supervisor that continues until an IP address is reacquired.
- MQTT reconnection now uses a periodic 5-second retry supervisor while Wi-Fi is available and stops only after broker connection succeeds.
- Configuration APIs remain protected and NVS-backed; Wi-Fi settings remain on the dedicated Wi-Fi page.

# v5.0 — Central Dashboard runtime configuration

- Added authenticated Dashboard editing for Wi-Fi network/password, Node ID, MQTT broker host/IP, port, publish topic, subscribed topic, QoS, optional broker username/password, client ID, keep-alive, clean-session and publish-retain behavior, transmission intervals, all sensor offsets, all deadbands, and all backend measurement IDs.
- Added protected `GET /api/config` and `POST /api/config/save` endpoints. Secret Wi-Fi/MQTT password values are never returned to the browser.
- Added live nearby-Wi-Fi scanning directly inside Dashboard configuration while preserving the dedicated `/wifi` commissioning page.
- Moved Node ID, MQTT connection values, topics, QoS, and backend measurement IDs from compile-time-only constants to NVS-backed runtime settings.
- Bumped NVS schema from 1 to 2 with an in-place migration that preserves existing deployment settings and initializes only new keys with historical defaults.
- Updated MQTT and telemetry modules to consume NVS-backed runtime Node/MQTT/ID configuration.
- Configuration saves are persisted first and applied after a controlled restart, avoiding dangling MQTT string pointers or mixed old/new runtime state.
- Changed Dashboard sensor refresh from a full-page reload to `/api/measurements` polling so configuration forms are not lost while being edited.
- Added a global `[hidden]` style rule so Admin sign-in and Logout controls cannot appear simultaneously due to display-style overrides.
- Added `DASHBOARD_CONFIGURATION.md`.

# v4.2

- Fixed Arduino `HEX` macro collision in `WebSecurity.cpp` by renaming the local hexadecimal digit table to `HEX_DIGITS`.
- Kept AsyncMqttClient pinned to upstream `v0.9.0`.
- Shortened the distributable project folder name to `RoomMonitor_v4_2` to avoid the Windows 260-character path limit affecting nested AsyncMqttClient headers.
- Recommended Windows project location: `D:\\RoomMonitor\\RoomMonitor_v4_2` or another short path.


## Build fix - Arduino-ESP32 2.0.17

- Fixed an ambiguous `String(float, uint8_t)` constructor call in `WebManager.cpp` used by `/api/measurements`.
- `jsonNumber()` now accepts `unsigned int` for decimal places, matching the Arduino `String(float, unsigned int)` constructor and allowing PlatformIO builds with Arduino-ESP32 2.0.17 to compile successfully.

# Changelog

## System status + Wi-Fi manager update

- Fixed `/system/info` route ordering. ESPAsyncWebServer callback routes use prefix matching, so `/system` previously intercepted `GET /system/info` and returned HTML instead of JSON.
- Fixed the System page health strip remaining at `Loading...` / `Checking...`.
- Added a per-boot ID so the OTA page confirms that an actual reboot occurred before redirecting to the dashboard.
- Added cache-busting/no-store headers so a newly uploaded SPIFFS UI is shown immediately instead of a stale browser-cached stylesheet/page.
- Added a firmware-resident `/wifi` Wi-Fi manager that is available during normal operation and after factory reset.
- Added a clear **Wi-Fi settings** link to the `/system` page and Wi-Fi tabs to dashboard/measurements pages.
- Added asynchronous nearby-network scanning, signal strength, secured/open indication, strongest-first sorting, duplicate SSID removal, and manual hidden-network entry.
- Changed factory-reset AP mode to AP+STA so `RoomMonitoring-WIFI-MANAGER` stays reachable while the ESP32 scans nearby networks.
- Kept `/setssid`, `/wifimanager.html`, and `/system/wifi` compatibility paths.

## Room Monitor system-update UI

- Added `OtaManager` as a dedicated OTA module.
- Added a modern firmware-resident `/system` page.
- Added separate firmware and SPIFFS upload controls.
- Added client-side upload progress, explicit success/error messages, delayed reboot, and automatic reconnect.
- Added `/system/info` health/status information for node, IP, Wi-Fi, MQTT, and SPIFFS.
- Added interrupted-upload cleanup so a failed transfer does not permanently lock OTA until reboot.
- Kept `GET /update` as a compatibility redirect and `POST /update` compatibility for the historical PlatformIO uploader.
- Removed the AsyncElegantOTA dependency and its deprecated-library warnings.
- Added `SYSTEM_UPDATE.md`.

## NVS / OTA storage refactor

- Moved persistent device configuration from SPIFFS text files to ESP32 Preferences/NVS.
- Added one-time migration of legacy SPIFFS settings on first boot.
- Added settings schema versioning for future storage migrations.
- Added a mutex around runtime NVS writes from asynchronous callbacks/tasks.
- Added `FilesystemManager` so SPIFFS has one responsibility: web assets.
- Disabled automatic SPIFFS formatting on mount failure.
- Added an in-firmware recovery page that keeps the system updater reachable if SPIFFS is unavailable.
- Factory reset now clears operational configuration without erasing the dashboard filesystem.
- Updated Wi-Fi setup and MQTT command persistence to use NVS keys.
- Explicitly set `board_build.filesystem = spiffs` in `platformio.ini`.
- Preserved existing MQTT topics, command codes, sensor mappings, and device behavior.

## 2026-09-10 — Live measurement history plots

- Fixed the dashboard humidity card by replacing literal percent characters in template-processed HTML with HTML entities. Literal `%` characters could be consumed as ESPAsyncWebServer template delimiters and corrupt the humidity markup.
- Added `GET /api/measurements`, returning all eight sensor readings in one no-cache JSON response; unavailable values are emitted as JSON `null`.
- Rebuilt `measurements.html` as an API-driven live-history view with eight independent plots (temperature, humidity, CO₂ equivalent, IAQ, VOC equivalent, pressure, sound, and illuminance).
- The measurements page samples every 5 seconds, starts history when the page opens, and retains all samples for the lifetime of that browser tab.
- Added current/min/max/sample-count summaries and explicit notation that refreshing or closing the page clears browser-side plot history.
- Removed automatic full-page reloads from the Measurements view; the refresh control now requests a sample immediately without erasing history.

## Secure consistent UI update — 2026-09-10

- Added a firmware-resident shared stylesheet at `/ui.css` so Dashboard, Measurements, Wi-Fi, System, and recovery pages use the same typography, heading sizes, navigation, spacing, and visual language.
- Reworked the embedded Wi-Fi and System pages to use the same top bar, page shell, heading and four-tab navigation as Dashboard and Measurements.
- Added `WebSecurity` and HTTP Basic Authentication for System/OTA administration.
- Protected `/system`, `/system/info`, both OTA upload endpoints, and legacy `/update`.
- Protected Wi-Fi management and its APIs during normal operation.
- Preserved factory-reset recovery: Wi-Fi setup remains unauthenticated only while the device has no saved SSID.
- Added `include/Secrets.h` for device-local credentials and `Secrets.example.h` for repository use. `Secrets.h` is excluded by `.gitignore`.
- Added `SECURITY.md` and `UI_STYLE.md`.

## 2026-09-14 — Dashboard password management and real logout

- Replaced browser-managed HTTP Basic Authentication with Room Monitor session authentication.
- Added firmware-resident `/login` page.
- Added random `HttpOnly` / `SameSite=Strict` administrator session cookies with an 8-hour inactivity timeout.
- Added Dashboard **Administrator security** section with current/new/confirm password workflow.
- Added Dashboard **Logout** button and logout controls on protected System/Wi-Fi pages.
- Runtime administrator password is stored in NVS as a salted SHA-256 credential record; the bootstrap password remains in `Secrets.h` for first boot/factory reset.
- Password changes invalidate every administrator session and require a fresh login.
- Added 30-second login cooldown after five failed sign-in attempts.
- Factory reset now explicitly returns administrator authentication to the bootstrap password while preserving firmware/SPIFFS.

## v4.1 build dependency fix
- Pin AsyncMqttClient directly to the upstream GitHub `v0.9.0` tag.
- Avoids an incomplete PlatformIO Registry package observed on Windows where `ParsingInformation.hpp` is missing.


## v5.1
- Removed Wi-Fi SSID/password/network scanning controls from the Dashboard configuration form.
- Kept the dedicated Wi-Fi manager (`/wifi`) as the single interface for Wi-Fi changes.
- Dashboard configuration saves now preserve existing Wi-Fi settings and cannot overwrite them.


## v7 — Secure MQTT TLS + CA verification

- Added explicit **Secure MQTT (TLS + CA verification)** setting to `/configuration`.
- Added broker root-CA PEM entry, preserve-on-blank behavior, validation, clear control, and NVS persistence.
- Upgraded NVS schema to v3; existing v2 deployments keep their current MQTT values and default TLS to disabled until explicitly configured.
- Replaced AsyncMqttClient transport with the native ESP-IDF ESP-MQTT client supplied by Arduino-ESP32.
- Added `mqtts://` transport, CA validation, certificate hostname verification, and system-time gating before TLS handshake.
- Preserved runtime MQTT host/port/topics/QoS/username/password/client ID/keepalive/clean-session/retain settings.
- Preserved QoS 0/1/2 publishing; publishing uses the non-blocking ESP-MQTT outbox/enqueue API.
- Kept broker auto-reconnect enabled and retained the Room Monitor's Wi-Fi-recovery reconnect supervision.
- Added TLS state to `/system`: `Connected · TLS` / `Disconnected · TLS`.
- Removed the AsyncMqttClient dependency from `platformio.ini`.
