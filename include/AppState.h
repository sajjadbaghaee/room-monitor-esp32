#pragma once

#include <Arduino.h>
#include <cmath>

#include "AppConfig.h"

// In-memory copy of persistent device configuration. SettingsManager loads
// this structure from Preferences/NVS during startup and updates it when a
// configuration command or authenticated Dashboard save is accepted.
struct AppSettings
{
    // Network provisioning
    String wifiSsid;
    String wifiPassword;
    String persistedIp = "NaV";

    // Deployment identity and MQTT transport. These used to be compile-time
    // constants; keeping them in NVS allows one firmware image to be deployed
    // across multiple rooms/nodes without recompilation.
    String nodeId = Config::DEFAULT_NODE_ID;
    String mqttHost = Config::DEFAULT_MQTT_HOST;
    uint16_t mqttPort = Config::DEFAULT_MQTT_PORT;
    String mqttPublishTopic = Config::DEFAULT_MQTT_PUBLISH_TOPIC;
    String mqttSubscribeTopic = Config::DEFAULT_MQTT_SUBSCRIBE_TOPIC;
    uint8_t mqttQos = Config::DEFAULT_MQTT_QOS;
    String mqttUsername;
    String mqttPassword;
    String mqttClientId; // Blank preserves the historical generated ESP32 client ID.
    uint16_t mqttKeepAliveSec = 60;
    bool mqttCleanSession = true;
    bool mqttPublishRetain = false;

    // Optional secure MQTT transport. When enabled the broker connection uses
    // mqtts:// and validates the server certificate against mqttCaCert.
    // The CA certificate is a public trust anchor, not a client secret.
    bool mqttTlsEnabled = false;
    String mqttCaCert;

    // Periodic transmission settings.
    uint32_t sendIntervalSec = Config::DEFAULT_SEND_INTERVAL_SEC;
    uint32_t aliveIntervalSec = Config::DEFAULT_ALIVE_INTERVAL_SEC;

    // Sensor calibration offsets.
    float temperatureOffset = 0.0f;
    float humidityOffset = 0.0f;
    float iaqOffset = 0.0f;
    float co2Offset = 0.0f;
    float vocOffset = 0.0f;
    float pressureOffset = 0.0f; // Legacy Ofap scale: 0.1 hPa per unit.
    int lightOffset = 0;
    int soundOffset = 0;

    // Change-trigger thresholds (deadbands).
    float dbTemperature = 0.50f;
    float dbHumidity = 2.00f;
    float dbPressure = 1.00f;
    float dbIaq = 5.00f;
    float dbCo2 = 25.00f;
    float dbVoc = 0.10f;
    float dbSound = 3.00f;
    float dbLight = 20.00f;

    // Backend measurement identifiers. They remain compatible with the
    // existing deployment by default but can be changed per node from the
    // authenticated Dashboard.
    String idTemperature = Config::ID_TEMPERATURE;
    String idHumidity = Config::ID_HUMIDITY;
    String idCo2 = Config::ID_CO2;
    String idAirQuality = Config::ID_AIR_QUALITY;
    String idLight = Config::ID_LIGHT;
    String idGas = Config::ID_GAS;
    String idPressure = Config::ID_PRESSURE;
    String idSound = Config::ID_SOUND;
    String idIp = Config::ID_IP;
    String idMac = Config::ID_MAC;
    String idAlive = Config::ID_ALIVE;
    String idLocalAddress = Config::ID_LOCAL_ADDRESS;
    String idPing = Config::ID_PING;
};

// Latest sensor values shared by acquisition, telemetry, display, and web UI.
struct SensorReadings
{
    float temperature = NAN;
    float humidity = NAN;
    float pressure = NAN;
    float iaq = NAN;
    float co2 = NAN;
    float voc = NAN;
    float sound = NAN;
    float light = NAN;

    bool bsecValid = false;
    bool soundLightValid = false;
};

struct LastSentReadings
{
    float temperature = NAN;
    float humidity = NAN;
    float pressure = NAN;
    float iaq = NAN;
    float co2 = NAN;
    float voc = NAN;
    float sound = NAN;
    float light = NAN;
};

struct NetworkState
{
    bool wifiConnected = false;
    bool mqttConnected = false;

    String ip = "NaV";
    String mac = "NaV";
    String domain = "NaV";
    String localAddress = "NaV";
};

struct AppState
{
    SensorReadings sensors;
    LastSentReadings lastSent;
    NetworkState network;
    bool resetInProgress = false;
};

extern AppSettings settings;
extern AppState appState;
