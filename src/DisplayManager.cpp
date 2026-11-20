#include "DisplayManager.h"

#include <Arduino.h>
#include <MD_Parola.h>

#include "AppConfig.h"
#include "AppState.h"
#include "TimeManager.h"

namespace
{
MD_Parola display(
    MD_MAX72XX::FC16_HW,
    Config::DISPLAY_DATA_PIN,
    Config::DISPLAY_CLK_PIN,
    Config::DISPLAY_CS_PIN,
    Config::MAX_DISPLAY_DEVICES);

char displayBuffer[Config::DISPLAY_BUFFER_SIZE];
uint8_t displayIndex = 0;
uint32_t lastDisplayMs = 0;
uint32_t initializingCounter = 1;

void showDisplay(const char *label, const String &value)
{
    // MD_Parola keeps the supplied pointer while the animation is active.
    value.toCharArray(displayBuffer, Config::DISPLAY_BUFFER_SIZE);

    display.displayReset(0);
    display.displayReset(1);

    display.displayZoneText(
        1,
        label,
        PA_CENTER,
        Config::DISPLAY_SPEED,
        0,
        PA_PRINT,
        PA_NO_EFFECT);

    display.displayZoneText(
        0,
        displayBuffer,
        PA_CENTER,
        Config::DISPLAY_SPEED,
        0,
        PA_PRINT,
        PA_NO_EFFECT);
}

void updatePage()
{
    const SensorReadings &s = appState.sensors;

    if (appState.resetInProgress)
    {
        showDisplay("Reset", "Done");
        return;
    }

    if (!s.soundLightValid)
    {
        showDisplay("Initi.", String(initializingCounter++));
        return;
    }

    if (!appState.network.wifiConnected)
    {
        showDisplay("No", "WiFi");
        return;
    }

    if (!appState.network.mqttConnected)
    {
        showDisplay("No", "MQTT");
        return;
    }

    switch (displayIndex)
    {
    case 0:
        showDisplay("Temp.", String(s.temperature, 2));
        break;
    case 1:
        showDisplay("Hum.", String(s.humidity, 2));
        break;
    case 2:
        showDisplay("Pre.", String(s.pressure, 0));
        break;
    case 3:
        showDisplay("Air Qu.", String(s.iaq, 1));
        break;
    case 4:
        showDisplay("CO2.", String(s.co2, 1));
        break;
    case 5:
        showDisplay("VOC.", String(s.voc, 1));
        break;
    case 6:
        showDisplay("Sound", String(s.sound, 0));
        break;
    case 7:
        showDisplay("Light", String(s.light, 0));
        break;
    case 8:
        TimeManager::update();
        showDisplay("Time", TimeManager::timeText());
        break;
    case 9:
        TimeManager::update();
        showDisplay("Date", TimeManager::dateText());
        break;
    }

    displayIndex = (displayIndex + 1) % 10;
}
} // namespace

void DisplayManager::begin()
{
    display.begin(3);
    display.setInvert(false);
    display.setZone(1, 4, 7);
    display.setZone(0, 0, 3);
}

void DisplayManager::service()
{
    display.displayAnimate();

    const uint32_t now = millis();
    if (now - lastDisplayMs >= Config::DISPLAY_INTERVAL_MS)
    {
        lastDisplayMs = now;
        updatePage();
    }
}
