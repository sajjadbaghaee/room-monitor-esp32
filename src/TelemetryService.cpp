#include "TelemetryService.h"

#include <Arduino.h>
#include <cmath>

#include "AppConfig.h"
#include "AppState.h"
#include "MqttManager.h"
#include "WiFiManager.h"

namespace
{
String jsonEscape(const String &input)
{
    String output;
    output.reserve(input.length() + 8);

    for (size_t i = 0; i < input.length(); ++i)
    {
        const char c = input[i];
        switch (c)
        {
        case '\\':
            output += "\\\\";
            break;
        case '"':
            output += "\\\"";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            output += c;
            break;
        }
    }

    return output;
}

void appendRecord(
    String &json,
    bool &first,
    const char *sensorType,
    const char *sensorId,
    const String &value)
{
    if (!first)
    {
        json += ',';
    }
    first = false;

    json += "{\"nodeId\":\"";
    json += jsonEscape(settings.nodeId);
    json += "\",\"sensorType\":\"";
    json += sensorType;
    json += "\",\"sensorId\":\"";
    json += jsonEscape(String(sensorId));
    json += "\",\"value\":\"";
    json += jsonEscape(value);
    json += "\"}";
}

String makeSingleRecordPayload(
    const char *sensorType,
    const char *sensorId,
    const String &value)
{
    String payload;
    payload.reserve(240);
    payload = "{\"dataType\":\"SensorData\",\"data\":[";

    bool first = true;
    appendRecord(payload, first, sensorType, sensorId, value);
    payload += "]}";
    return payload;
}

void queueSensor(
    const char *sensorType,
    const char *sensorId,
    float value,
    uint8_t decimals)
{
    MqttManager::enqueue(
        makeSingleRecordPayload(
            sensorType,
            sensorId,
            String(value, static_cast<unsigned int>(decimals))));
}

void appendQuotedValue(String &payload, bool &first, const String &value)
{
    if (!first)
    {
        payload += ',';
    }
    first = false;
    payload += '"';
    payload += jsonEscape(value);
    payload += '"';
}

void syncDeadbandReferences()
{
    const SensorReadings &s = appState.sensors;
    LastSentReadings &last = appState.lastSent;

    if (std::isfinite(s.temperature)) last.temperature = s.temperature;
    if (std::isfinite(s.humidity)) last.humidity = s.humidity;
    if (std::isfinite(s.pressure)) last.pressure = s.pressure;
    if (std::isfinite(s.iaq)) last.iaq = s.iaq;
    if (std::isfinite(s.co2)) last.co2 = s.co2;
    if (std::isfinite(s.voc)) last.voc = s.voc;
    if (std::isfinite(s.sound)) last.sound = s.sound;
    if (std::isfinite(s.light)) last.light = s.light;
}

bool shouldSend(float value, float &lastSent, float deadband)
{
    if (!std::isfinite(value))
    {
        return false;
    }

    deadband = max(0.0f, deadband);

    if (std::isnan(lastSent) || std::fabs(value - lastSent) >= deadband)
    {
        lastSent = value;
        return true;
    }

    return false;
}

void sendIfChanged(
    const char *sensorType,
    const char *sensorId,
    float value,
    float &lastSent,
    float deadband,
    uint8_t decimals)
{
    if (!appState.network.wifiConnected)
    {
        return;
    }

    if (shouldSend(value, lastSent, deadband))
    {
        queueSensor(sensorType, sensorId, value, decimals);
    }
}
} // namespace

void TelemetryService::queuePing()
{
    MqttManager::enqueue(
        makeSingleRecordPayload("Ping", settings.idPing.c_str(), "LIVE"));
}

void TelemetryService::queueAlive()
{
    MqttManager::enqueue(
        makeSingleRecordPayload("Message", settings.idAlive.c_str(), "I am Alive"));
}

void TelemetryService::queueDeviceInfo()
{
    WiFiManager::refreshIp();

    String payload;
    payload.reserve(650);
    payload = "{\"dataType\":\"SensorData\",\"data\":[";

    bool first = true;
    appendRecord(payload, first, "Message", settings.idIp.c_str(), appState.network.ip);
    appendRecord(payload, first, "Message", settings.idMac.c_str(), appState.network.mac);
    appendRecord(payload, first, "Message", settings.idAlive.c_str(), "I am Alive");
    appendRecord(payload, first, "Message", settings.idLocalAddress.c_str(), appState.network.localAddress);

    payload += "]}";
    MqttManager::enqueue(payload);
}

void TelemetryService::queueFullData()
{
    const SensorReadings &s = appState.sensors;
    if (!s.bsecValid || !s.soundLightValid)
    {
        return;
    }

    WiFiManager::refreshIp();

    String payload;
    payload.reserve(1900);
    payload = "{\"dataType\":\"SensorData\",\"data\":[";

    bool first = true;
    appendRecord(payload, first, "Temperature", settings.idTemperature.c_str(), String(s.temperature, 2));
    appendRecord(payload, first, "Humidity", settings.idHumidity.c_str(), String(s.humidity, 2));
    appendRecord(payload, first, "CO2", settings.idCo2.c_str(), String(s.co2, 2));

    // Existing backend compatibility: AirQuality carries BSEC breath VOC equivalent.
    appendRecord(payload, first, "AirQuality", settings.idAirQuality.c_str(), String(s.voc, 2));

    appendRecord(payload, first, "Light", settings.idLight.c_str(), String(s.light, 0));

    // Existing backend compatibility: Gas carries BSEC IAQ.
    appendRecord(payload, first, "Gas", settings.idGas.c_str(), String(s.iaq, 2));

    appendRecord(payload, first, "Pressure", settings.idPressure.c_str(), String(s.pressure, 2));
    appendRecord(payload, first, "Sound", settings.idSound.c_str(), String(s.sound, 0));
    appendRecord(payload, first, "Message", settings.idIp.c_str(), appState.network.ip);
    appendRecord(payload, first, "Message", settings.idMac.c_str(), appState.network.mac);
    appendRecord(payload, first, "Message", settings.idAlive.c_str(), "I am Alive");
    appendRecord(payload, first, "Message", settings.idLocalAddress.c_str(), appState.network.localAddress);

    payload += "]}";

    syncDeadbandReferences();
    MqttManager::enqueue(payload);
}

void TelemetryService::queueConfiguration()
{
    String payload;
    payload.reserve(700);

    payload = "{\"dataType\":\"Configuration\",\"data\":[{\"nodeId\":\"";
    payload += jsonEscape(settings.nodeId);
    payload += "\",\"value\":[";

    bool first = true;
    appendQuotedValue(payload, first, "Rate" + String(settings.sendIntervalSec));
    appendQuotedValue(payload, first, "IamA" + String(settings.aliveIntervalSec));

    appendQuotedValue(payload, first, "Ofte" + String(settings.temperatureOffset));
    appendQuotedValue(payload, first, "Ofhu" + String(settings.humidityOffset));
    appendQuotedValue(payload, first, "Ofco" + String(settings.co2Offset));
    appendQuotedValue(payload, first, "Ofvo" + String(settings.vocOffset));
    appendQuotedValue(payload, first, "Ofaq" + String(settings.iaqOffset));
    appendQuotedValue(payload, first, "Ofap" + String(settings.pressureOffset));
    appendQuotedValue(payload, first, "Ofli" + String(settings.lightOffset));
    appendQuotedValue(payload, first, "Ofso" + String(settings.soundOffset));

    appendQuotedValue(payload, first, "Dfte" + String(settings.dbTemperature));
    appendQuotedValue(payload, first, "Dfhu" + String(settings.dbHumidity));
    appendQuotedValue(payload, first, "Dfco" + String(settings.dbCo2));
    appendQuotedValue(payload, first, "Dfvo" + String(settings.dbVoc));
    appendQuotedValue(payload, first, "Dfaq" + String(settings.dbIaq));
    appendQuotedValue(payload, first, "Dfap" + String(settings.dbPressure));
    appendQuotedValue(payload, first, "Dfli" + String(settings.dbLight));
    appendQuotedValue(payload, first, "Dfso" + String(settings.dbSound));

    payload += "]}]}";
    MqttManager::enqueue(payload);
}

void TelemetryService::processDeadbands()
{
    const SensorReadings &s = appState.sensors;
    LastSentReadings &last = appState.lastSent;

    if (s.bsecValid)
    {
        sendIfChanged("Temperature", settings.idTemperature.c_str(), s.temperature, last.temperature, settings.dbTemperature, 2);
        sendIfChanged("Humidity", settings.idHumidity.c_str(), s.humidity, last.humidity, settings.dbHumidity, 2);
        sendIfChanged("Pressure", settings.idPressure.c_str(), s.pressure, last.pressure, settings.dbPressure, 2);

        // Existing backend compatibility: AirQuality carries VOC.
        sendIfChanged("AirQuality", settings.idAirQuality.c_str(), s.voc, last.voc, settings.dbVoc, 2);

        // Existing backend compatibility: Gas carries BSEC IAQ.
        sendIfChanged("Gas", settings.idGas.c_str(), s.iaq, last.iaq, settings.dbIaq, 2);

        sendIfChanged("CO2", settings.idCo2.c_str(), s.co2, last.co2, settings.dbCo2, 2);
    }

    if (s.soundLightValid)
    {
        sendIfChanged("Sound", settings.idSound.c_str(), s.sound, last.sound, settings.dbSound, 0);
        sendIfChanged("Light", settings.idLight.c_str(), s.light, last.light, settings.dbLight, 0);
    }
}
