#pragma once

#include <Arduino.h>

struct AppSettings;

namespace MqttManager
{
void begin();
void connect();
void handleWifiConnected();
void handleWifiDisconnected();

void enqueue(const String &message);
void service();

// Commissioning test for the supplied settings. If they match the active
// configuration, the already-established production connection is inspected
// instead of creating a second TLS client. Unsaved settings use lightweight
// DNS/TCP/time/CA prechecks and, when heap margin permits, an independent
// ESP-MQTT session. NVS is never modified.
String testConnection(const AppSettings &testSettings);
} // namespace MqttManager
