#include "FilesystemManager.h"

#include <Arduino.h>
#include <SPIFFS.h>

namespace
{
bool filesystemReady = false;
}

bool FilesystemManager::begin()
{
    // Do not use SPIFFS.begin(true) here. Auto-formatting on a transient mount
    // failure could erase all HTML/CSS assets. Re-upload the filesystem image
    // explicitly if mounting fails.
    filesystemReady = SPIFFS.begin(false);

    if (filesystemReady)
    {
        Serial.println("SPIFFS mounted (web assets).");
    }
    else
    {
        Serial.println("ERROR: SPIFFS mount failed. Web assets are unavailable.");
        Serial.println("Upload a SPIFFS filesystem image, then reboot the device.");
    }

    return filesystemReady;
}

void FilesystemManager::end()
{
    if (!filesystemReady)
    {
        return;
    }

    SPIFFS.end();
    filesystemReady = false;
    Serial.println("SPIFFS unmounted for filesystem update.");
}

bool FilesystemManager::isReady()
{
    return filesystemReady;
}
