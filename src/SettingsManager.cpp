#include "SettingsManager.h"

#include <Preferences.h>
#include <SPIFFS.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "AppConfig.h"
#include "AppState.h"
#include "FilesystemManager.h"

namespace
{
Preferences preferences;
SemaphoreHandle_t settingsMutex = nullptr;
bool preferencesReady = false;

const char *const LEGACY_SETTING_FILES[] = {
    Config::LegacyPath::SSID,
    Config::LegacyPath::PASSWORD,
    Config::LegacyPath::IP,
    Config::LegacyPath::SEND_INTERVAL,
    Config::LegacyPath::ALIVE_INTERVAL,
    Config::LegacyPath::OFFSET_TEMPERATURE,
    Config::LegacyPath::OFFSET_HUMIDITY,
    Config::LegacyPath::OFFSET_IAQ,
    Config::LegacyPath::OFFSET_CO2,
    Config::LegacyPath::OFFSET_VOC,
    Config::LegacyPath::OFFSET_PRESSURE,
    Config::LegacyPath::OFFSET_LIGHT,
    Config::LegacyPath::OFFSET_SOUND,
    Config::LegacyPath::DB_TEMPERATURE,
    Config::LegacyPath::DB_HUMIDITY,
    Config::LegacyPath::DB_PRESSURE,
    Config::LegacyPath::DB_IAQ,
    Config::LegacyPath::DB_CO2,
    Config::LegacyPath::DB_VOC,
    Config::LegacyPath::DB_SOUND,
    Config::LegacyPath::DB_LIGHT,
};

bool lockSettings()
{
    return settingsMutex != nullptr &&
           xSemaphoreTake(settingsMutex, pdMS_TO_TICKS(250)) == pdTRUE;
}

void unlockSettings()
{
    if (settingsMutex != nullptr)
    {
        xSemaphoreGive(settingsMutex);
    }
}

String readLegacyString(const char *path)
{
    if (!FilesystemManager::isReady())
    {
        return "";
    }

    File file = SPIFFS.open(path, FILE_READ);
    if (!file || file.isDirectory())
    {
        return "";
    }

    String value = file.readStringUntil('\n');
    file.close();
    value.trim();
    return value;
}

float legacyFloat(const char *path, float defaultValue)
{
    const String value = readLegacyString(path);
    return value.length() ? value.toFloat() : defaultValue;
}

int legacyInt(const char *path, int defaultValue)
{
    const String value = readLegacyString(path);
    return value.length() ? value.toInt() : defaultValue;
}

uint32_t legacyPositiveUInt(const char *path, uint32_t defaultValue)
{
    const String value = readLegacyString(path);
    const long parsed = value.toInt();
    return (value.length() && parsed > 0)
               ? static_cast<uint32_t>(parsed)
               : defaultValue;
}

bool legacySettingsExist()
{
    if (!FilesystemManager::isReady())
    {
        return false;
    }

    for (const char *path : LEGACY_SETTING_FILES)
    {
        if (SPIFFS.exists(path))
        {
            return true;
        }
    }

    return false;
}

void loadLegacySettings(AppSettings &legacy)
{
    legacy.wifiSsid = readLegacyString(Config::LegacyPath::SSID);
    legacy.wifiPassword = readLegacyString(Config::LegacyPath::PASSWORD);

    const String oldIp = readLegacyString(Config::LegacyPath::IP);
    legacy.persistedIp = oldIp.length() ? oldIp : "NaV";

    legacy.sendIntervalSec = legacyPositiveUInt(
        Config::LegacyPath::SEND_INTERVAL,
        Config::DEFAULT_SEND_INTERVAL_SEC);
    legacy.aliveIntervalSec = legacyPositiveUInt(
        Config::LegacyPath::ALIVE_INTERVAL,
        Config::DEFAULT_ALIVE_INTERVAL_SEC);

    legacy.temperatureOffset = legacyFloat(Config::LegacyPath::OFFSET_TEMPERATURE, 0.0f);
    legacy.humidityOffset = legacyFloat(Config::LegacyPath::OFFSET_HUMIDITY, 0.0f);
    legacy.iaqOffset = legacyFloat(Config::LegacyPath::OFFSET_IAQ, 0.0f);
    legacy.co2Offset = legacyFloat(Config::LegacyPath::OFFSET_CO2, 0.0f);
    legacy.vocOffset = legacyFloat(Config::LegacyPath::OFFSET_VOC, 0.0f);
    legacy.pressureOffset = legacyFloat(Config::LegacyPath::OFFSET_PRESSURE, 0.0f);
    legacy.lightOffset = legacyInt(Config::LegacyPath::OFFSET_LIGHT, 0);
    legacy.soundOffset = legacyInt(Config::LegacyPath::OFFSET_SOUND, 0);

    legacy.dbTemperature = max(0.0f, legacyFloat(Config::LegacyPath::DB_TEMPERATURE, 0.50f));
    legacy.dbHumidity = max(0.0f, legacyFloat(Config::LegacyPath::DB_HUMIDITY, 2.00f));
    legacy.dbPressure = max(0.0f, legacyFloat(Config::LegacyPath::DB_PRESSURE, 1.00f));
    legacy.dbIaq = max(0.0f, legacyFloat(Config::LegacyPath::DB_IAQ, 5.00f));
    legacy.dbCo2 = max(0.0f, legacyFloat(Config::LegacyPath::DB_CO2, 25.00f));
    legacy.dbVoc = max(0.0f, legacyFloat(Config::LegacyPath::DB_VOC, 0.10f));
    legacy.dbSound = max(0.0f, legacyFloat(Config::LegacyPath::DB_SOUND, 3.00f));
    legacy.dbLight = max(0.0f, legacyFloat(Config::LegacyPath::DB_LIGHT, 20.00f));
}

bool putStringChecked(const char *key, const String &value)
{
    // Preferences::putString() reports 0 bytes for a valid empty string, so
    // verify string writes by reading the key back instead of trusting only
    // the byte-count return value.
    preferences.putString(key, value);
    return preferences.isKey(key) &&
           preferences.getString(key, "") == value;
}

bool writeAllSettingsToNvs(const AppSettings &source)
{
    bool ok = true;

    // Provisioning/network state.
    ok = putStringChecked(Config::NvsKey::WIFI_SSID, source.wifiSsid) && ok;
    ok = putStringChecked(Config::NvsKey::WIFI_PASSWORD, source.wifiPassword) && ok;
    ok = putStringChecked(Config::NvsKey::LAST_IP, source.persistedIp) && ok;

    // Deployment identity and MQTT settings.
    ok = putStringChecked(Config::NvsKey::NODE_ID, source.nodeId) && ok;
    ok = putStringChecked(Config::NvsKey::MQTT_HOST, source.mqttHost) && ok;
    ok = (preferences.putUInt(Config::NvsKey::MQTT_PORT, source.mqttPort) > 0) && ok;
    ok = putStringChecked(Config::NvsKey::MQTT_PUBLISH, source.mqttPublishTopic) && ok;
    ok = putStringChecked(Config::NvsKey::MQTT_SUBSCRIBE, source.mqttSubscribeTopic) && ok;
    ok = (preferences.putUInt(Config::NvsKey::MQTT_QOS, source.mqttQos) > 0) && ok;
    ok = putStringChecked(Config::NvsKey::MQTT_USERNAME, source.mqttUsername) && ok;
    ok = putStringChecked(Config::NvsKey::MQTT_PASSWORD, source.mqttPassword) && ok;
    ok = putStringChecked(Config::NvsKey::MQTT_CLIENT_ID, source.mqttClientId) && ok;
    ok = (preferences.putUInt(Config::NvsKey::MQTT_KEEPALIVE, source.mqttKeepAliveSec) > 0) && ok;
    ok = (preferences.putBool(Config::NvsKey::MQTT_CLEAN, source.mqttCleanSession) > 0) && ok;
    ok = (preferences.putBool(Config::NvsKey::MQTT_RETAIN, source.mqttPublishRetain) > 0) && ok;
    ok = (preferences.putBool(Config::NvsKey::MQTT_TLS, source.mqttTlsEnabled) > 0) && ok;
    ok = putStringChecked(Config::NvsKey::MQTT_CA, source.mqttCaCert) && ok;

    // Transmission cadence.
    ok = (preferences.putUInt(Config::NvsKey::SEND_INTERVAL, source.sendIntervalSec) > 0) && ok;
    ok = (preferences.putUInt(Config::NvsKey::ALIVE_INTERVAL, source.aliveIntervalSec) > 0) && ok;

    // Calibration offsets.
    ok = (preferences.putFloat(Config::NvsKey::OFFSET_TEMPERATURE, source.temperatureOffset) > 0) && ok;
    ok = (preferences.putFloat(Config::NvsKey::OFFSET_HUMIDITY, source.humidityOffset) > 0) && ok;
    ok = (preferences.putFloat(Config::NvsKey::OFFSET_IAQ, source.iaqOffset) > 0) && ok;
    ok = (preferences.putFloat(Config::NvsKey::OFFSET_CO2, source.co2Offset) > 0) && ok;
    ok = (preferences.putFloat(Config::NvsKey::OFFSET_VOC, source.vocOffset) > 0) && ok;
    ok = (preferences.putFloat(Config::NvsKey::OFFSET_PRESSURE, source.pressureOffset) > 0) && ok;
    ok = (preferences.putInt(Config::NvsKey::OFFSET_LIGHT, source.lightOffset) > 0) && ok;
    ok = (preferences.putInt(Config::NvsKey::OFFSET_SOUND, source.soundOffset) > 0) && ok;

    // Change-trigger deadbands.
    ok = (preferences.putFloat(Config::NvsKey::DB_TEMPERATURE, source.dbTemperature) > 0) && ok;
    ok = (preferences.putFloat(Config::NvsKey::DB_HUMIDITY, source.dbHumidity) > 0) && ok;
    ok = (preferences.putFloat(Config::NvsKey::DB_PRESSURE, source.dbPressure) > 0) && ok;
    ok = (preferences.putFloat(Config::NvsKey::DB_IAQ, source.dbIaq) > 0) && ok;
    ok = (preferences.putFloat(Config::NvsKey::DB_CO2, source.dbCo2) > 0) && ok;
    ok = (preferences.putFloat(Config::NvsKey::DB_VOC, source.dbVoc) > 0) && ok;
    ok = (preferences.putFloat(Config::NvsKey::DB_SOUND, source.dbSound) > 0) && ok;
    ok = (preferences.putFloat(Config::NvsKey::DB_LIGHT, source.dbLight) > 0) && ok;

    // Backend identifiers.
    ok = putStringChecked(Config::NvsKey::ID_TEMPERATURE, source.idTemperature) && ok;
    ok = putStringChecked(Config::NvsKey::ID_HUMIDITY, source.idHumidity) && ok;
    ok = putStringChecked(Config::NvsKey::ID_CO2, source.idCo2) && ok;
    ok = putStringChecked(Config::NvsKey::ID_AIR_QUALITY, source.idAirQuality) && ok;
    ok = putStringChecked(Config::NvsKey::ID_LIGHT, source.idLight) && ok;
    ok = putStringChecked(Config::NvsKey::ID_GAS, source.idGas) && ok;
    ok = putStringChecked(Config::NvsKey::ID_PRESSURE, source.idPressure) && ok;
    ok = putStringChecked(Config::NvsKey::ID_SOUND, source.idSound) && ok;
    ok = putStringChecked(Config::NvsKey::ID_IP, source.idIp) && ok;
    ok = putStringChecked(Config::NvsKey::ID_MAC, source.idMac) && ok;
    ok = putStringChecked(Config::NvsKey::ID_ALIVE, source.idAlive) && ok;
    ok = putStringChecked(Config::NvsKey::ID_LOCAL_ADDRESS, source.idLocalAddress) && ok;
    ok = putStringChecked(Config::NvsKey::ID_PING, source.idPing) && ok;

    // The schema marker is deliberately written last. If a power interruption
    // occurs while migrating, the next boot can retry rather than accepting a
    // partially migrated configuration as complete.
    if (ok)
    {
        ok = preferences.putUShort(
                 Config::NvsKey::SCHEMA,
                 Config::SETTINGS_SCHEMA_VERSION) > 0;
    }

    return ok;
}

void writeNewV2DefaultsIfMissing()
{
    const AppSettings defaults;

    if (!preferences.isKey(Config::NvsKey::NODE_ID)) putStringChecked(Config::NvsKey::NODE_ID, defaults.nodeId);
    if (!preferences.isKey(Config::NvsKey::MQTT_HOST)) putStringChecked(Config::NvsKey::MQTT_HOST, defaults.mqttHost);
    if (!preferences.isKey(Config::NvsKey::MQTT_PORT)) preferences.putUInt(Config::NvsKey::MQTT_PORT, defaults.mqttPort);
    if (!preferences.isKey(Config::NvsKey::MQTT_PUBLISH)) putStringChecked(Config::NvsKey::MQTT_PUBLISH, defaults.mqttPublishTopic);
    if (!preferences.isKey(Config::NvsKey::MQTT_SUBSCRIBE)) putStringChecked(Config::NvsKey::MQTT_SUBSCRIBE, defaults.mqttSubscribeTopic);
    if (!preferences.isKey(Config::NvsKey::MQTT_QOS)) preferences.putUInt(Config::NvsKey::MQTT_QOS, defaults.mqttQos);
    if (!preferences.isKey(Config::NvsKey::MQTT_USERNAME)) putStringChecked(Config::NvsKey::MQTT_USERNAME, defaults.mqttUsername);
    if (!preferences.isKey(Config::NvsKey::MQTT_PASSWORD)) putStringChecked(Config::NvsKey::MQTT_PASSWORD, defaults.mqttPassword);
    if (!preferences.isKey(Config::NvsKey::MQTT_CLIENT_ID)) putStringChecked(Config::NvsKey::MQTT_CLIENT_ID, defaults.mqttClientId);
    if (!preferences.isKey(Config::NvsKey::MQTT_KEEPALIVE)) preferences.putUInt(Config::NvsKey::MQTT_KEEPALIVE, defaults.mqttKeepAliveSec);
    if (!preferences.isKey(Config::NvsKey::MQTT_CLEAN)) preferences.putBool(Config::NvsKey::MQTT_CLEAN, defaults.mqttCleanSession);
    if (!preferences.isKey(Config::NvsKey::MQTT_RETAIN)) preferences.putBool(Config::NvsKey::MQTT_RETAIN, defaults.mqttPublishRetain);

    if (!preferences.isKey(Config::NvsKey::ID_TEMPERATURE)) putStringChecked(Config::NvsKey::ID_TEMPERATURE, defaults.idTemperature);
    if (!preferences.isKey(Config::NvsKey::ID_HUMIDITY)) putStringChecked(Config::NvsKey::ID_HUMIDITY, defaults.idHumidity);
    if (!preferences.isKey(Config::NvsKey::ID_CO2)) putStringChecked(Config::NvsKey::ID_CO2, defaults.idCo2);
    if (!preferences.isKey(Config::NvsKey::ID_AIR_QUALITY)) putStringChecked(Config::NvsKey::ID_AIR_QUALITY, defaults.idAirQuality);
    if (!preferences.isKey(Config::NvsKey::ID_LIGHT)) putStringChecked(Config::NvsKey::ID_LIGHT, defaults.idLight);
    if (!preferences.isKey(Config::NvsKey::ID_GAS)) putStringChecked(Config::NvsKey::ID_GAS, defaults.idGas);
    if (!preferences.isKey(Config::NvsKey::ID_PRESSURE)) putStringChecked(Config::NvsKey::ID_PRESSURE, defaults.idPressure);
    if (!preferences.isKey(Config::NvsKey::ID_SOUND)) putStringChecked(Config::NvsKey::ID_SOUND, defaults.idSound);
    if (!preferences.isKey(Config::NvsKey::ID_IP)) putStringChecked(Config::NvsKey::ID_IP, defaults.idIp);
    if (!preferences.isKey(Config::NvsKey::ID_MAC)) putStringChecked(Config::NvsKey::ID_MAC, defaults.idMac);
    if (!preferences.isKey(Config::NvsKey::ID_ALIVE)) putStringChecked(Config::NvsKey::ID_ALIVE, defaults.idAlive);
    if (!preferences.isKey(Config::NvsKey::ID_LOCAL_ADDRESS)) putStringChecked(Config::NvsKey::ID_LOCAL_ADDRESS, defaults.idLocalAddress);
    if (!preferences.isKey(Config::NvsKey::ID_PING)) putStringChecked(Config::NvsKey::ID_PING, defaults.idPing);
}

void writeNewV3DefaultsIfMissing()
{
    const AppSettings defaults;
    if (!preferences.isKey(Config::NvsKey::MQTT_TLS))
    {
        preferences.putBool(Config::NvsKey::MQTT_TLS, defaults.mqttTlsEnabled);
    }
    if (!preferences.isKey(Config::NvsKey::MQTT_CA))
    {
        putStringChecked(Config::NvsKey::MQTT_CA, defaults.mqttCaCert);
    }
}

void writeNewV4DefaultsIfMissing()
{
    // Schema 4 adds persistent reliability counters and OTA audit metadata.
    // These are intentionally separate from AppSettings so normal configuration
    // saves cannot reset operational diagnostics.
    if (!preferences.isKey(Config::NvsKey::BOOT_COUNT)) preferences.putUInt(Config::NvsKey::BOOT_COUNT, 0);
    if (!preferences.isKey(Config::NvsKey::WDT_COUNT)) preferences.putUInt(Config::NvsKey::WDT_COUNT, 0);
    if (!preferences.isKey(Config::NvsKey::OTA_RESULT)) putStringChecked(Config::NvsKey::OTA_RESULT, "none");
    if (!preferences.isKey(Config::NvsKey::OTA_SHA256)) putStringChecked(Config::NvsKey::OTA_SHA256, "");
    if (!preferences.isKey(Config::NvsKey::OTA_TARGET)) putStringChecked(Config::NvsKey::OTA_TARGET, "");
}

void migrateLegacySpiffsSettings()
{
    if (!legacySettingsExist())
    {
        return;
    }

    AppSettings legacy;
    loadLegacySettings(legacy);
    if (!writeAllSettingsToNvs(legacy))
    {
        Serial.println("ERROR: Legacy SPIFFS settings migration to NVS was incomplete.");
        return;
    }

    // Keep the old files until the next filesystem image replaces them. This
    // provides a rollback path to older firmware. The schema marker prevents
    // these files from being imported again by this firmware.
    Serial.println("Settings migration: legacy SPIFFS configuration copied to NVS.");
}

void initializeSchemaIfNeeded()
{
    const uint16_t schema = preferences.getUShort(Config::NvsKey::SCHEMA, 0);

    if (schema == Config::SETTINGS_SCHEMA_VERSION)
    {
        return;
    }

    if (schema == 0)
    {
        if (legacySettingsExist())
        {
            migrateLegacySpiffsSettings();
        }
        else if (FilesystemManager::isReady())
        {
            // Clean install: persist defaults so all runtime-editable settings
            // have explicit NVS values from the first boot onward.
            const AppSettings defaults;
            if (!writeAllSettingsToNvs(defaults))
            {
                Serial.println("ERROR: Could not initialize default NVS settings.");
            }
        }
        else
        {
            // Do not mark initialization complete while SPIFFS is unavailable.
            // A deployed device may still need legacy migration after the
            // filesystem is restored on a later boot.
            Serial.println("NVS schema not initialized: SPIFFS unavailable for migration check.");
        }
        return;
    }

    if (schema > Config::SETTINGS_SCHEMA_VERSION)
    {
        Serial.println("WARNING: NVS settings schema is newer than this firmware.");
        return;
    }

    // Upgrade incrementally so an older deployed node reaches the current
    // schema in a single boot without overwriting any existing values.
    uint16_t upgradedSchema = schema;

    if (upgradedSchema == 1)
    {
        writeNewV2DefaultsIfMissing();
        upgradedSchema = 2;
        Serial.println("Settings schema upgraded from v1 to v2.");
    }

    // Schema 3 adds secure MQTT/TLS settings and the broker CA certificate.
    if (upgradedSchema == 2)
    {
        writeNewV3DefaultsIfMissing();
        upgradedSchema = 3;
        Serial.println("Settings schema upgraded from v2 to v3 (MQTT TLS support).");
    }

    // Schema 4 adds diagnostics counters and OTA audit metadata. No existing
    // deployment configuration is overwritten during this upgrade.
    if (upgradedSchema == 3)
    {
        writeNewV4DefaultsIfMissing();
        upgradedSchema = 4;
        Serial.println("Settings schema upgraded from v3 to v4 (diagnostics/reliability metadata).");
    }

    if (upgradedSchema == Config::SETTINGS_SCHEMA_VERSION)
    {
        preferences.putUShort(Config::NvsKey::SCHEMA, upgradedSchema);
        return;
    }

    Serial.println("WARNING: NVS settings schema upgrade path is not defined.");
}

void removeLegacySettingsFiles()
{
    if (!FilesystemManager::isReady())
    {
        return;
    }

    for (const char *path : LEGACY_SETTING_FILES)
    {
        if (SPIFFS.exists(path))
        {
            SPIFFS.remove(path);
        }
    }
}
} // namespace

bool SettingsManager::begin()
{
    if (settingsMutex == nullptr)
    {
        settingsMutex = xSemaphoreCreateMutex();
    }

    if (settingsMutex == nullptr)
    {
        Serial.println("ERROR: Failed to create settings mutex.");
        return false;
    }

    if (!preferences.begin(Config::NVS_NAMESPACE, false))
    {
        Serial.println("ERROR: Could not open Preferences/NVS namespace.");
        return false;
    }

    preferencesReady = true;
    initializeSchemaIfNeeded();
    load();

    Serial.println("Settings loaded from Preferences/NVS.");
    return true;
}

void SettingsManager::load()
{
    if (!preferencesReady || !lockSettings())
    {
        return;
    }

    settings.wifiSsid = preferences.getString(Config::NvsKey::WIFI_SSID, "");
    settings.wifiPassword = preferences.getString(Config::NvsKey::WIFI_PASSWORD, "");
    settings.persistedIp = preferences.getString(Config::NvsKey::LAST_IP, "NaV");

    settings.nodeId = preferences.getString(Config::NvsKey::NODE_ID, Config::DEFAULT_NODE_ID);
    settings.mqttHost = preferences.getString(Config::NvsKey::MQTT_HOST, Config::DEFAULT_MQTT_HOST);
    settings.mqttPort = static_cast<uint16_t>(preferences.getUInt(Config::NvsKey::MQTT_PORT, Config::DEFAULT_MQTT_PORT));
    settings.mqttPublishTopic = preferences.getString(Config::NvsKey::MQTT_PUBLISH, Config::DEFAULT_MQTT_PUBLISH_TOPIC);
    settings.mqttSubscribeTopic = preferences.getString(Config::NvsKey::MQTT_SUBSCRIBE, Config::DEFAULT_MQTT_SUBSCRIBE_TOPIC);
    const uint32_t storedQos = preferences.getUInt(Config::NvsKey::MQTT_QOS, Config::DEFAULT_MQTT_QOS);
    settings.mqttQos = static_cast<uint8_t>(storedQos > 2 ? 2 : storedQos);
    settings.mqttUsername = preferences.getString(Config::NvsKey::MQTT_USERNAME, "");
    settings.mqttPassword = preferences.getString(Config::NvsKey::MQTT_PASSWORD, "");
    settings.mqttClientId = preferences.getString(Config::NvsKey::MQTT_CLIENT_ID, "");
    const uint32_t storedKeepAlive = preferences.getUInt(Config::NvsKey::MQTT_KEEPALIVE, 60);
    settings.mqttKeepAliveSec = static_cast<uint16_t>(storedKeepAlive < 5 ? 5 : (storedKeepAlive > 3600 ? 3600 : storedKeepAlive));
    settings.mqttCleanSession = preferences.getBool(Config::NvsKey::MQTT_CLEAN, true);
    settings.mqttPublishRetain = preferences.getBool(Config::NvsKey::MQTT_RETAIN, false);
    settings.mqttTlsEnabled = preferences.getBool(Config::NvsKey::MQTT_TLS, false);
    settings.mqttCaCert = preferences.getString(Config::NvsKey::MQTT_CA, "");

    settings.sendIntervalSec = preferences.getUInt(Config::NvsKey::SEND_INTERVAL, Config::DEFAULT_SEND_INTERVAL_SEC);
    settings.aliveIntervalSec = preferences.getUInt(Config::NvsKey::ALIVE_INTERVAL, Config::DEFAULT_ALIVE_INTERVAL_SEC);

    settings.temperatureOffset = preferences.getFloat(Config::NvsKey::OFFSET_TEMPERATURE, 0.0f);
    settings.humidityOffset = preferences.getFloat(Config::NvsKey::OFFSET_HUMIDITY, 0.0f);
    settings.iaqOffset = preferences.getFloat(Config::NvsKey::OFFSET_IAQ, 0.0f);
    settings.co2Offset = preferences.getFloat(Config::NvsKey::OFFSET_CO2, 0.0f);
    settings.vocOffset = preferences.getFloat(Config::NvsKey::OFFSET_VOC, 0.0f);
    settings.pressureOffset = preferences.getFloat(Config::NvsKey::OFFSET_PRESSURE, 0.0f);
    settings.lightOffset = preferences.getInt(Config::NvsKey::OFFSET_LIGHT, 0);
    settings.soundOffset = preferences.getInt(Config::NvsKey::OFFSET_SOUND, 0);

    settings.dbTemperature = max(0.0f, preferences.getFloat(Config::NvsKey::DB_TEMPERATURE, 0.50f));
    settings.dbHumidity = max(0.0f, preferences.getFloat(Config::NvsKey::DB_HUMIDITY, 2.00f));
    settings.dbPressure = max(0.0f, preferences.getFloat(Config::NvsKey::DB_PRESSURE, 1.00f));
    settings.dbIaq = max(0.0f, preferences.getFloat(Config::NvsKey::DB_IAQ, 5.00f));
    settings.dbCo2 = max(0.0f, preferences.getFloat(Config::NvsKey::DB_CO2, 25.00f));
    settings.dbVoc = max(0.0f, preferences.getFloat(Config::NvsKey::DB_VOC, 0.10f));
    settings.dbSound = max(0.0f, preferences.getFloat(Config::NvsKey::DB_SOUND, 3.00f));
    settings.dbLight = max(0.0f, preferences.getFloat(Config::NvsKey::DB_LIGHT, 20.00f));

    settings.idTemperature = preferences.getString(Config::NvsKey::ID_TEMPERATURE, Config::ID_TEMPERATURE);
    settings.idHumidity = preferences.getString(Config::NvsKey::ID_HUMIDITY, Config::ID_HUMIDITY);
    settings.idCo2 = preferences.getString(Config::NvsKey::ID_CO2, Config::ID_CO2);
    settings.idAirQuality = preferences.getString(Config::NvsKey::ID_AIR_QUALITY, Config::ID_AIR_QUALITY);
    settings.idLight = preferences.getString(Config::NvsKey::ID_LIGHT, Config::ID_LIGHT);
    settings.idGas = preferences.getString(Config::NvsKey::ID_GAS, Config::ID_GAS);
    settings.idPressure = preferences.getString(Config::NvsKey::ID_PRESSURE, Config::ID_PRESSURE);
    settings.idSound = preferences.getString(Config::NvsKey::ID_SOUND, Config::ID_SOUND);
    settings.idIp = preferences.getString(Config::NvsKey::ID_IP, Config::ID_IP);
    settings.idMac = preferences.getString(Config::NvsKey::ID_MAC, Config::ID_MAC);
    settings.idAlive = preferences.getString(Config::NvsKey::ID_ALIVE, Config::ID_ALIVE);
    settings.idLocalAddress = preferences.getString(Config::NvsKey::ID_LOCAL_ADDRESS, Config::ID_LOCAL_ADDRESS);
    settings.idPing = preferences.getString(Config::NvsKey::ID_PING, Config::ID_PING);

    appState.network.ip = settings.persistedIp;
    unlockSettings();
}

bool SettingsManager::saveAll(const AppSettings &source, bool applyRuntime)
{
    if (!preferencesReady || !lockSettings())
    {
        return false;
    }

    const bool ok = writeAllSettingsToNvs(source);
    if (ok && applyRuntime)
    {
        settings = source;
        appState.network.ip = settings.persistedIp;
    }

    unlockSettings();
    return ok;
}

bool SettingsManager::saveString(const char *key, const String &value)
{
    if (!preferencesReady || !lockSettings())
    {
        return false;
    }

    const bool ok = putStringChecked(key, value);
    unlockSettings();
    return ok;
}

String SettingsManager::getString(const char *key, const String &defaultValue)
{
    if (!preferencesReady || !lockSettings())
    {
        return defaultValue;
    }

    const String value = preferences.getString(key, defaultValue);
    unlockSettings();
    return value;
}

void SettingsManager::savePositiveUInt(const char *key, const String &value, uint32_t &target)
{
    const long parsed = value.toInt();
    if (parsed <= 0 || !preferencesReady || !lockSettings())
    {
        return;
    }

    const uint32_t candidate = static_cast<uint32_t>(parsed);
    if (preferences.putUInt(key, candidate) > 0)
    {
        target = candidate;
    }

    unlockSettings();
}

void SettingsManager::saveFloat(const char *key, const String &value, float &target)
{
    if (!preferencesReady || !lockSettings())
    {
        return;
    }

    const float candidate = value.toFloat();
    if (preferences.putFloat(key, candidate) > 0)
    {
        target = candidate;
    }

    unlockSettings();
}

void SettingsManager::saveNonNegativeFloat(const char *key, const String &value, float &target)
{
    if (!preferencesReady || !lockSettings())
    {
        return;
    }

    const float candidate = max(0.0f, value.toFloat());
    if (preferences.putFloat(key, candidate) > 0)
    {
        target = candidate;
    }

    unlockSettings();
}

void SettingsManager::saveInt(const char *key, const String &value, int &target)
{
    if (!preferencesReady || !lockSettings())
    {
        return;
    }

    const int candidate = value.toInt();
    if (preferences.putInt(key, candidate) > 0)
    {
        target = candidate;
    }

    unlockSettings();
}

bool SettingsManager::saveUInt(const char *key, uint32_t value)
{
    if (!preferencesReady || !lockSettings())
    {
        return false;
    }

    preferences.putUInt(key, value);
    const bool ok = preferences.isKey(key) && preferences.getUInt(key, value + 1U) == value;
    unlockSettings();
    return ok;
}

uint32_t SettingsManager::getUInt(const char *key, uint32_t defaultValue)
{
    if (!preferencesReady || !lockSettings())
    {
        return defaultValue;
    }

    const uint32_t value = preferences.getUInt(key, defaultValue);
    unlockSettings();
    return value;
}

void SettingsManager::factoryReset()
{
    if (preferencesReady && lockSettings())
    {
        preferences.clear();
        const AppSettings defaults;
        if (!writeAllSettingsToNvs(defaults))
        {
            Serial.println("WARNING: Default settings could not be fully restored during factory reset.");
        }
        unlockSettings();
    }

    // Remove only old configuration files. Never format SPIFFS here because
    // the filesystem contains application web assets only.
    removeLegacySettingsFiles();

    settings = AppSettings{};
    appState.network.ip = settings.persistedIp;
}
