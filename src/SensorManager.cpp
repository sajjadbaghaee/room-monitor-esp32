#include "SensorManager.h"

#include <Arduino.h>
#include <BH1750.h>
#include <Wire.h>
#include <bsec.h>
#include <cmath>

#include "AppConfig.h"
#include "AppState.h"
#include "DiagnosticsManager.h"

namespace
{
Bsec iaqSensor;
BH1750 lightMeter;

void checkIaqSensorStatus()
{
    if (iaqSensor.bsecStatus < BSEC_OK)
    {
        Serial.print("BSEC error: ");
        Serial.println(iaqSensor.bsecStatus);
    }
    else if (iaqSensor.bsecStatus > BSEC_OK)
    {
        Serial.print("BSEC warning: ");
        Serial.println(iaqSensor.bsecStatus);
    }

    if (iaqSensor.bme68xStatus < BME68X_OK)
    {
        Serial.print("BME68X error: ");
        Serial.println(iaqSensor.bme68xStatus);
    }
    else if (iaqSensor.bme68xStatus > BME68X_OK)
    {
        Serial.print("BME68X warning: ");
        Serial.println(iaqSensor.bme68xStatus);
    }
}

float readSoundDb()
{
    // DFRobot SEN0232: 0.6 V -> 30 dBA and 2.6 V -> 130 dBA.
    // Therefore dBA = output voltage * 50.
    uint32_t totalMilliVolts = 0;

    for (uint16_t i = 0; i < Config::SOUND_SAMPLE_COUNT; ++i)
    {
        totalMilliVolts += analogReadMilliVolts(Config::SOUND_PIN);
        delay(Config::SOUND_SAMPLE_DELAY_MS);
    }

    const float averageMilliVolts =
        static_cast<float>(totalMilliVolts) /
        static_cast<float>(Config::SOUND_SAMPLE_COUNT);

    if (averageMilliVolts < Config::SOUND_DISCONNECTED_MV)
    {
        return NAN;
    }

    const float voltage = averageMilliVolts / 1000.0f;
    return (voltage * 50.0f) + static_cast<float>(settings.soundOffset);
}
} // namespace

void SensorManager::begin()
{
    Wire.begin();

    // SEN0232 can output up to about 2.6 V.
    analogSetPinAttenuation(Config::SOUND_PIN, ADC_11db);

    lightMeter.begin();

    iaqSensor.begin(BME68X_I2C_ADDR_HIGH, Wire);
    checkIaqSensorStatus();

    bsec_virtual_sensor_t sensorList[] = {
        BSEC_OUTPUT_IAQ,
        BSEC_OUTPUT_STATIC_IAQ,
        BSEC_OUTPUT_CO2_EQUIVALENT,
        BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
        BSEC_OUTPUT_RAW_TEMPERATURE,
        BSEC_OUTPUT_RAW_PRESSURE,
        BSEC_OUTPUT_RAW_HUMIDITY,
        BSEC_OUTPUT_RAW_GAS,
        BSEC_OUTPUT_STABILIZATION_STATUS,
        BSEC_OUTPUT_RUN_IN_STATUS,
        BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
        BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
        BSEC_OUTPUT_GAS_PERCENTAGE};

    iaqSensor.updateSubscription(
        sensorList,
        sizeof(sensorList) / sizeof(sensorList[0]),
        BSEC_SAMPLE_RATE_LP);

    checkIaqSensorStatus();
}

void SensorManager::updateBsec()
{
    if (!iaqSensor.run())
    {
        checkIaqSensorStatus();
        if (iaqSensor.bsecStatus < BSEC_OK || iaqSensor.bme68xStatus < BME68X_OK)
        {
            DiagnosticsManager::recordBsecSample(
                false,
                iaqSensor.bsecStatus,
                iaqSensor.bme68xStatus,
                iaqSensor.iaqAccuracy);
        }
        return;
    }

    SensorReadings &s = appState.sensors;
    s.temperature = iaqSensor.temperature + settings.temperatureOffset;
    s.humidity = iaqSensor.humidity + settings.humidityOffset;
    s.iaq = iaqSensor.iaq + settings.iaqOffset;
    s.co2 = iaqSensor.co2Equivalent + settings.co2Offset;
    s.voc = iaqSensor.breathVocEquivalent + settings.vocOffset;

    // BSEC pressure is Pa. Preserve the legacy Ofap scale (0.1 hPa/unit).
    s.pressure = (iaqSensor.pressure / 100.0f) + (settings.pressureOffset / 10.0f);

    s.bsecValid =
        std::isfinite(s.temperature) &&
        std::isfinite(s.humidity) &&
        std::isfinite(s.iaq) &&
        std::isfinite(s.co2) &&
        std::isfinite(s.voc) &&
        std::isfinite(s.pressure);

    DiagnosticsManager::recordBsecSample(
        s.bsecValid,
        iaqSensor.bsecStatus,
        iaqSensor.bme68xStatus,
        iaqSensor.iaqAccuracy);
}

void SensorManager::updateSoundAndLight()
{
    SensorReadings &s = appState.sensors;

    s.sound = readSoundDb();

    const float rawLight = lightMeter.readLightLevel();
    const bool lightValid = std::isfinite(rawLight);
    if (lightValid)
    {
        s.light = rawLight + static_cast<float>(settings.lightOffset);
    }
    else
    {
        // Do not let an old finite reading make a failed BH1750 sample look
        // healthy. A failed read becomes explicitly invalid until the next
        // successful sample.
        s.light = NAN;
    }

    const bool soundValid = std::isfinite(s.sound);
    s.soundLightValid = soundValid && lightValid;
    DiagnosticsManager::recordSoundSample(soundValid);
    DiagnosticsManager::recordLightSample(lightValid);
}
