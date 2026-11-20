#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace DiagnosticsManager
{
void begin();

void registerTasks(TaskHandle_t sensorTask, TaskHandle_t serviceTask);
void recordWifiConnected();
void recordWifiDisconnected(uint8_t reason);
void recordMqttConnectAttempt();
void recordMqttConnected();
void recordMqttDisconnected();
void recordMqttError(const String &message);
void recordMqttPublish();
void recordMqttCommand();
void recordMqttTest(const String &outcome, const String &message);
void recordNtpSync();
void recordBsecSample(bool success, int bsecStatus, int bmeStatus, uint8_t accuracy);
void recordSoundSample(bool success);
void recordLightSample(bool success);
void recordLoginSuccess();
void recordLoginFailure();
void recordControlledRestart();

uint32_t bootCount();
uint32_t watchdogResetCount();
String resetReasonText();
String json();
} // namespace DiagnosticsManager
