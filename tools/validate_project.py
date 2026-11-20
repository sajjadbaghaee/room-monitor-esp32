#!/usr/bin/env python3
"""Static regression checks for Room Monitor.

These tests deliberately avoid ESP32 hardware dependencies so they can run on
Windows/Linux and in CI before the PlatformIO firmware build.
"""
from __future__ import annotations

import re
import sys
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "7.3.2"

errors: list[str] = []


def fail(message: str) -> None:
    errors.append(message)


def text(path: str) -> str:
    p = ROOT / path
    if not p.exists():
        fail(f"missing required file: {path}")
        return ""
    return p.read_text(encoding="utf-8", errors="replace")


required = [
    "include/VersionInfo.h",
    "include/DiagnosticsManager.h",
    "src/DiagnosticsManager.cpp",
    "include/CertificateManager.h",
    "src/CertificateManager.cpp",
    "data/diagnostics.html",
    "data/configuration.html",
    "src/SystemUpdatePage.h",
    "PRODUCTION_SECURITY.md",
]
for item in required:
    text(item)

version_h = text("include/VersionInfo.h")
if f'FIRMWARE_VERSION[] = "{VERSION}"' not in version_h:
    fail(f"VersionInfo.h does not declare firmware version {VERSION}")

# NVS string keys on ESP32 Preferences are limited to 15 characters.
app_config = text("include/AppConfig.h")
key_block_match = re.search(r"namespace\s+NvsKey\s*\{(.*?)\}\s*//\s*namespace\s+NvsKey", app_config, re.S)
if not key_block_match:
    fail("could not find Config::NvsKey block")
else:
    for name, value in re.findall(r'constexpr\s+char\s+(\w+)\[\]\s*=\s*"([^"]+)"', key_block_match.group(1)):
        if len(value) > 15:
            fail(f"NVS key {name}='{value}' is {len(value)} chars (max 15)")

# Navigation / asset version consistency.
ui_files = [
    "data/index.html",
    "data/measurements.html",
    "data/configuration.html",
    "data/wifimanager.html",
    "src/WifiSettingsPage.h",
    "src/SystemUpdatePage.h",
    "data/diagnostics.html",
    "src/AdminLoginPage.h",
]
for path in ui_files:
    s = text(path)
    if "v=7.2.0" in s:
        fail(f"stale v7.2.0 asset token remains in {path}")
    if path != "src/AdminLoginPage.h" and path != "data/wifimanager.html" and "/diagnostics" not in s:
        fail(f"Diagnostics navigation missing from {path}")

# Duplicate HTML ids frequently break browser-side status updates.
class IdParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.ids: list[str] = []
    def handle_starttag(self, tag, attrs):
        for key, value in attrs:
            if key == "id" and value:
                self.ids.append(value)


def check_ids(path: str, html: str) -> None:
    parser = IdParser()
    try:
        parser.feed(html)
    except Exception as exc:
        fail(f"HTML parse failure in {path}: {exc}")
        return
    seen: set[str] = set()
    for item in parser.ids:
        if item in seen:
            fail(f"duplicate id '{item}' in {path}")
        seen.add(item)

for path in ["data/index.html", "data/measurements.html", "data/configuration.html", "data/wifimanager.html"]:
    check_ids(path, text(path))
for path in ["data/diagnostics.html"]:
    check_ids(path, text(path))
for path, delimiter in [("src/WifiSettingsPage.h", "WIFIHTML"), ("src/SystemUpdatePage.h", "SYSHTML"), ("src/AdminLoginPage.h", "LOGINHTML")]:
    s = text(path)
    m = re.search(rf'R"{delimiter}\((.*?)\){delimiter}"', s, re.S)
    if m:
        check_ids(path, m.group(1))

# Export must never leak recoverable passwords or administrator credential data.
web = text("src/WebManager.cpp")
m = re.search(r"String\s+configurationExportJson\(\)\s*\{(.*?)\n\}", web, re.S)
if not m:
    fail("configurationExportJson() not found")
else:
    export_body = m.group(1)
    for forbidden in ["mqttPassword", "wifiPassword", "ADMIN_CREDENTIAL", "adminCredential"]:
        if forbidden in export_body:
            fail(f"configuration export references secret field: {forbidden}")

# Preserve legacy backend command compatibility.
mqtt = text("src/MqttManager.cpp")
for command in ["Ping", "Cfrr", "Rate", "IamA", "Ofte", "Ofhu", "Ofco", "Ofvo", "Ofaq", "Ofap", "Ofli", "Ofso", "Dfte", "Dfhu", "Dfco", "Dfvo", "Dfaq", "Dfap", "Dfli", "Dfso"]:
    if f'command == "{command}"' not in mqtt:
        fail(f"legacy MQTT command missing: {command}")

telemetry = text("src/TelemetryService.cpp")
if "settings.idAirQuality" not in telemetry or "settings.idGas" not in telemetry:
    fail("legacy AirQuality/Gas backend ID mappings appear to be missing")


# Flash-size regression guards retained from v7.3.1.
if (ROOT / "src/DiagnosticsPage.h").exists():
    fail("Diagnostics page must remain in SPIFFS, not firmware-resident PROGMEM")
if '#include <WiFiClientSecure.h>' in mqtt:
    fail("MQTT commissioning test must reuse ESP-MQTT instead of WiFiClientSecure")
if 'data/diagnostics.html' not in [str(Path("data/diagnostics.html"))]:
    fail("internal validator error")

# v7.3.2 MQTT commissioning test must reuse the live production session for
# active settings and guard an independent second client under low heap.
for needle in [
    "mqttConnectionSettingsMatch",
    'String mode = activeConfiguration ? "live" : "independent"',
    "mqttSubscriptionAcknowledged",
    "MQTT_TEST_MIN_FREE_HEAP_BYTES",
    "WiFiClient tcpProbe",
    'outcome = "partial"',
]:
    if needle not in mqtt and needle not in app_config:
        fail(f"expected v7.3.2 MQTT test behavior missing: {needle}")
config_html = text("data/configuration.html")
for needle in ["Existing live connection", "NOT RUN", "Test details"]:
    if needle not in config_html:
        fail(f"v7.3.2 commissioning UI missing: {needle}")

# Sensor health must be based on the current BH1750 read, not a stale finite
# value retained from the previous successful sample.
sensor = text("src/SensorManager.cpp")
if "const bool lightValid = std::isfinite(rawLight);" not in sensor or "s.light = NAN;" not in sensor:
    fail("BH1750 diagnostics do not explicitly invalidate a failed current sample")

# The Dashboard is template-processed by ESPAsyncWebServer. Literal percent
# signs must stay encoded so only known %...% placeholders reach the parser.
index_html = text("data/index.html")
percent_tokens = re.findall(r"%([^%]+)%", index_html)
known_tokens = {"Temp_web", "Hum_web", "CO2_web", "AirQuality_web", "VOC_web", "Pressure_web", "Sound_web", "Light_web"}
for token in percent_tokens:
    if token not in known_tokens:
        fail(f"unexpected Dashboard template token: %{token}%")

# Security/reliability features expected in v7.3.
for needle, path in [
    ("X-RoomMonitor-Request", "src/WebSecurity.cpp"),
    ("/api/config/test-mqtt", "src/WebManager.cpp"),
    ("/api/config/export", "src/WebManager.cpp"),
    ("X-Image-SHA256", "src/OtaManager.cpp"),
    ("data[0] == 0xE9", "src/OtaManager.cpp"),
    ("esp_task_wdt", "src/TaskManager.cpp"),
    ("/api/diagnostics", "src/WebManager.cpp"),
]:
    if needle not in text(path):
        fail(f"expected v7.3 feature '{needle}' missing from {path}")

if errors:
    print("Room Monitor static validation FAILED:")
    for e in errors:
        print(" -", e)
    sys.exit(1)

print("Room Monitor static validation passed.")
