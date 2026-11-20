#pragma once

#include <Arduino.h>

#ifndef ROOMMONITOR_GIT_SHA
#define ROOMMONITOR_GIT_SHA "local"
#endif

namespace VersionInfo
{
constexpr char FIRMWARE_NAME[] = "Room Monitor";
constexpr char FIRMWARE_VERSION[] = "7.3.2";
constexpr char UI_ASSET_VERSION[] = "7.3.2";
constexpr char BOARD_NAME[] = "DOIT ESP32 DEVKIT V1";
constexpr char GIT_SHA[] = ROOMMONITOR_GIT_SHA;

inline String buildTimestamp()
{
    return String(__DATE__) + " " + String(__TIME__);
}

inline String displayVersion()
{
    return String(FIRMWARE_NAME) + " " + String(FIRMWARE_VERSION);
}
} // namespace VersionInfo
