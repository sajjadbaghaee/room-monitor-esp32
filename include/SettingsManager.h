#pragma once

#include <Arduino.h>

struct AppSettings;

namespace SettingsManager
{
// Opens the Preferences/NVS namespace, performs one-time migration from the
// legacy SPIFFS text files when necessary, and loads settings into `settings`.
bool begin();

// Reload all known values from NVS into the global AppSettings object.
void load();

// Persist the complete runtime/deployment configuration. Administrator
// credentials are managed separately by WebSecurity and are not overwritten.
bool saveAll(const AppSettings &source, bool applyRuntime = true);

// Persist one setting. The key must be one of Config::NvsKey::*.
bool saveString(const char *key, const String &value);
String getString(const char *key, const String &defaultValue = "");
void savePositiveUInt(const char *key, const String &value, uint32_t &target);
void saveFloat(const char *key, const String &value, float &target);
void saveNonNegativeFloat(const char *key, const String &value, float &target);
void saveInt(const char *key, const String &value, int &target);
bool saveUInt(const char *key, uint32_t value);
uint32_t getUInt(const char *key, uint32_t defaultValue = 0);

// Clears only this application's NVS namespace. Legacy configuration text
// files are also removed so a factory reset cannot accidentally re-import them.
// SPIFFS web assets (HTML/CSS) are intentionally preserved.
void factoryReset();
} // namespace SettingsManager
