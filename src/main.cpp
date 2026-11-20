#include <Arduino.h>

#include "AppState.h"
#include "DisplayManager.h"
#include "DiagnosticsManager.h"
#include "FilesystemManager.h"
#include "MqttManager.h"
#include "SensorManager.h"
#include "SettingsManager.h"
#include "TaskManager.h"
#include "WebManager.h"
#include "WiFiManager.h"

void setup()
{
    Serial.begin(115200);
    delay(500);

    // SPIFFS now contains web assets only. It is mounted before SettingsManager
    // so already-deployed devices can perform a one-time legacy settings
    // migration from SPIFFS text files into Preferences/NVS.
    FilesystemManager::begin();
    SettingsManager::begin();
    DiagnosticsManager::begin();

    SensorManager::begin();
    DisplayManager::begin();
    MqttManager::begin();
    WiFiManager::begin();
    WebManager::begin();

    if (settings.wifiSsid.length() > 0)
    {
        WiFiManager::connect();
    }

    TaskManager::begin();
}

void loop()
{
    // Runtime work is handled by FreeRTOS tasks and asynchronous callbacks.
    delay(1000);
}
