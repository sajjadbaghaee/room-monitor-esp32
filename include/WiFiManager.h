#pragma once

#include <Arduino.h>

namespace WiFiManager
{
void begin();
void connect();
void refreshIp();

// Legacy synchronous scan helper retained for compatibility.
String scanNetworks();

// Non-blocking scan API used by the web-based Wi-Fi manager.
void requestNetworkScan();
String networkScanJson();

void startConfigurationAccessPoint();
bool isAccessPointMode();
String activeIpAddress();
String modeLabel();
} // namespace WiFiManager
