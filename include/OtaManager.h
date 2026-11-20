#pragma once

class AsyncWebServer;

namespace OtaManager
{
// Registers the Room Monitor system/OTA web page and its upload endpoints.
// The page is compiled into firmware, so it remains available even if the
// SPIFFS web filesystem is missing or being replaced.
void begin(AsyncWebServer &server);
} // namespace OtaManager
