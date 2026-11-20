#include "TaskManager.h"

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

#include "AppConfig.h"
#include "AppState.h"
#include "DisplayManager.h"
#include "DiagnosticsManager.h"
#include "MqttManager.h"
#include "SensorManager.h"
#include "SettingsManager.h"
#include "TelemetryService.h"

namespace
{
TaskHandle_t sensorTaskHandle = nullptr;
TaskHandle_t serviceTaskHandle = nullptr;

void checkFactoryReset()
{
    if (analogRead(Config::FACTORY_RESET_PIN) <= Config::FACTORY_RESET_THRESHOLD)
    {
        return;
    }

    delay(Config::FACTORY_RESET_CONFIRM_MS);

    if (analogRead(Config::FACTORY_RESET_PIN) <= Config::FACTORY_RESET_THRESHOLD)
    {
        return;
    }

    appState.resetInProgress = true;

    // Reset operational configuration only. SettingsManager clears NVS and
    // legacy configuration files but deliberately preserves SPIFFS web assets.
    SettingsManager::factoryReset();

    Serial.println("Factory reset completed (configuration cleared, web assets preserved).");
    delay(1500);
    ESP.restart();
}

void sensorTask(void *)
{
    esp_task_wdt_add(nullptr);
    uint32_t lastFullSendMs = 0;
    uint32_t lastAliveSendMs = 0;
    uint32_t lastSoundLightReadMs = 0;

    for (;;)
    {
        checkFactoryReset();
        SensorManager::updateBsec();

        const uint32_t now = millis();

        if (now - lastSoundLightReadMs >= Config::SENSOR_INTERVAL_MS)
        {
            lastSoundLightReadMs = now;
            SensorManager::updateSoundAndLight();
        }

        if (appState.network.wifiConnected &&
            appState.sensors.bsecValid &&
            appState.sensors.soundLightValid &&
            now - lastFullSendMs >= settings.sendIntervalSec * 1000UL)
        {
            lastFullSendMs = now;
            lastAliveSendMs = now;
            TelemetryService::queueFullData();
        }

        if (appState.network.wifiConnected &&
            now - lastAliveSendMs >= settings.aliveIntervalSec * 1000UL)
        {
            lastAliveSendMs = now;
            TelemetryService::queueAlive();
        }

        TelemetryService::processDeadbands();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(Config::TASK_DELAY_MS));
    }
}

void serviceTask(void *)
{
    esp_task_wdt_add(nullptr);
    for (;;)
    {
        MqttManager::service();
        DisplayManager::service();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(Config::TASK_DELAY_MS));
    }
}
} // namespace

void TaskManager::begin()
{
    pinMode(Config::FACTORY_RESET_PIN, INPUT);

    const esp_err_t wdtResult = esp_task_wdt_init(Config::TASK_WATCHDOG_TIMEOUT_SEC, true);
    if (wdtResult != ESP_OK && wdtResult != ESP_ERR_INVALID_STATE)
    {
        Serial.printf("WARNING: task watchdog initialization failed: %d\n", static_cast<int>(wdtResult));
    }

    const BaseType_t sensorTaskResult = xTaskCreatePinnedToCore(
        sensorTask,
        "SensorTask",
        Config::TASK_STACK_SIZE,
        nullptr,
        Config::TASK_PRIORITY,
        &sensorTaskHandle,
        Config::SENSOR_TASK_CORE);

    const BaseType_t serviceTaskResult = xTaskCreatePinnedToCore(
        serviceTask,
        "ServiceTask",
        Config::TASK_STACK_SIZE,
        nullptr,
        Config::TASK_PRIORITY,
        &serviceTaskHandle,
        Config::SERVICE_TASK_CORE);

    if (sensorTaskResult != pdPASS)
    {
        sensorTaskHandle = nullptr;
        Serial.println("ERROR: SensorTask could not be created.");
    }
    if (serviceTaskResult != pdPASS)
    {
        serviceTaskHandle = nullptr;
        Serial.println("ERROR: ServiceTask could not be created.");
    }

    DiagnosticsManager::registerTasks(sensorTaskHandle, serviceTaskHandle);
}
