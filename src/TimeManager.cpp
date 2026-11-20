#include "TimeManager.h"

#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

#include "AppState.h"
#include "DiagnosticsManager.h"

namespace
{
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 10800, 60000);
String formattedDate;
String currentDateText = "NaN";
String currentTimeText = "NaN";
bool started = false;

// Certificate validation needs a plausible wall clock. 2021 is only a sanity
// threshold; the actual certificate validity window is checked by ESP-TLS.
constexpr time_t MIN_VALID_SYSTEM_TIME = 1609459200; // 2021-01-01 UTC
} // namespace

void TimeManager::begin()
{
    if (!started)
    {
        // NTPClient is retained for the existing local display formatting.
        timeClient.begin();

        // Also start the ESP32 SNTP/system clock. ESP-TLS uses the system time
        // when validating X.509 certificate validity dates, so secure MQTT is
        // intentionally held until this clock becomes plausible.
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        started = true;
    }
}

void TimeManager::update()
{
    if (!appState.network.wifiConnected || !started || !timeClient.update())
    {
        return;
    }

    formattedDate = timeClient.getFormattedDate();
    const int separator = formattedDate.indexOf('T');
    if (separator < 0)
    {
        return;
    }

    currentDateText = formattedDate.substring(5, 10);
    currentTimeText = formattedDate.substring(separator + 1, separator + 6);
    if (systemTimeValid())
    {
        DiagnosticsManager::recordNtpSync();
    }
}

bool TimeManager::systemTimeValid()
{
    if (!started)
    {
        return false;
    }

    const time_t now = time(nullptr);
    return now >= MIN_VALID_SYSTEM_TIME;
}

const String &TimeManager::dateText()
{
    return currentDateText;
}

const String &TimeManager::timeText()
{
    return currentTimeText;
}
