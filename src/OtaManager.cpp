#include "OtaManager.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>

#include <new>

#include "AppConfig.h"
#include "AppState.h"
#include "DiagnosticsManager.h"
#include "FilesystemManager.h"
#include "SettingsManager.h"
#include "VersionInfo.h"
#include "SystemUpdatePage.h"
#include "WebSecurity.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

namespace
{
constexpr uint32_t OTA_RESTART_DELAY_MS = 4500;

enum class OtaTarget : uint8_t
{
    Firmware,
    Filesystem
};

struct UploadContext
{
    OtaTarget target;
    bool failed = false;
    bool ownsUpdater = false;
    bool filesystemWasMounted = false;
    const char *errorMessage = nullptr;
    size_t bytesWritten = 0;
    bool shaStarted = false;
    String expectedSha256;
    String actualSha256;
    mbedtls_sha256_context shaContext;

    explicit UploadContext(OtaTarget uploadTarget) : target(uploadTarget)
    {
        mbedtls_sha256_init(&shaContext);
    }

    ~UploadContext()
    {
        mbedtls_sha256_free(&shaContext);
    }
};

bool otaBusy = false;
TimerHandle_t restartTimer = nullptr;
uint32_t bootId = 0;

const char *targetLabel(OtaTarget target)
{
    return target == OtaTarget::Firmware ? "Firmware" : "Web interface";
}

int updateCommand(OtaTarget target)
{
    return target == OtaTarget::Firmware ? U_FLASH : U_SPIFFS;
}

void restartTimerCallback(TimerHandle_t)
{
    Serial.println("OTA: restarting device now.");
    ESP.restart();
}

void scheduleRestart()
{
    if (restartTimer != nullptr)
    {
        xTimerStop(restartTimer, 0);
        xTimerStart(restartTimer, 0);
        return;
    }

    // Extremely unlikely fallback if a FreeRTOS timer could not be created.
    // A short task still allows the HTTP success response to reach the browser
    // before the ESP32 restarts.
    xTaskCreate(
        [](void *)
        {
            vTaskDelay(pdMS_TO_TICKS(OTA_RESTART_DELAY_MS));
            ESP.restart();
            vTaskDelete(nullptr);
        },
        "OtaRestart",
        2048,
        nullptr,
        1,
        nullptr);
}

UploadContext *getContext(AsyncWebServerRequest *request)
{
    return static_cast<UploadContext *>(request->_tempObject);
}

void setFailure(UploadContext *context, const char *message)
{
    if (context == nullptr || context->failed)
    {
        return;
    }

    context->failed = true;
    context->errorMessage = message;

    // If an update had already started, release the Update object immediately
    // so a failed/interrupted upload cannot block the next OTA attempt.
    if (context->ownsUpdater && Update.isRunning())
    {
        Update.abort();
    }
}

String sha256Hex(const uint8_t *digest, size_t length)
{
    static const char HEX_DIGITS[] = "0123456789abcdef";
    String out;
    out.reserve(length * 2);
    for (size_t i = 0; i < length; ++i)
    {
        out += HEX_DIGITS[(digest[i] >> 4) & 0x0F];
        out += HEX_DIGITS[digest[i] & 0x0F];
    }
    return out;
}

bool validSha256Text(String value)
{
    value.trim();
    if (value.length() != 64) return false;
    for (size_t i = 0; i < value.length(); ++i)
    {
        const char c = value[i];
        const bool hex = (c >= '0' && c <= '9') ||
                         (c >= 'a' && c <= 'f') ||
                         (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

void beginUpload(
    AsyncWebServerRequest *request,
    const String &filename,
    OtaTarget target,
    bool acceptOptionalMd5,
    bool requireSha256)
{
    UploadContext *context = new (std::nothrow) UploadContext(target);

    request->_tempObject = context;

    if (context != nullptr)
    {
        // Clean up an interrupted upload as soon as its HTTP connection drops.
        // On a normal completed request finishUpload() clears _tempObject first,
        // so this callback becomes a no-op.
        request->onDisconnect(
            [request]()
            {
                UploadContext *disconnected = getContext(request);
                if (disconnected == nullptr)
                {
                    return;
                }

                if (disconnected->ownsUpdater)
                {
                    if (Update.isRunning())
                    {
                        Update.abort();
                    }
                    otaBusy = false;
                }

                // If a filesystem upload was interrupted before flash was
                // completely replaced, try to restore normal web access. A
                // partially written image may fail to mount; /system remains
                // available from firmware either way.
                if (disconnected->filesystemWasMounted)
                {
                    FilesystemManager::begin();
                }

                request->_tempObject = nullptr;
                delete disconnected;
                Serial.println("OTA: interrupted upload cleaned up.");
            });
    }

    if (context == nullptr)
    {
        Serial.println("OTA ERROR: unable to allocate upload context.");
        return;
    }

    if (otaBusy)
    {
        setFailure(context, "Another update is already in progress.");
        return;
    }

    otaBusy = true;
    context->ownsUpdater = true;

    Serial.printf(
        "OTA: starting %s upload (%s).\n",
        targetLabel(target),
        filename.c_str());

    // Do not rewrite a mounted filesystem partition. The OTA page itself is
    // stored in firmware, so unmounting SPIFFS does not interrupt this upload.
    if (target == OtaTarget::Filesystem && FilesystemManager::isReady())
    {
        context->filesystemWasMounted = true;
        FilesystemManager::end();
    }

    // The historical AsyncElegantOTA PlatformIO uploader sends an MD5 field.
    // It is optional for the new Room Monitor page, but we still honor it when
    // supplied so old upload scripts remain compatible with POST /update.
    if (acceptOptionalMd5 && request->hasParam("MD5", true))
    {
        const AsyncWebParameter *md5 = request->getParam("MD5", true);
        if (md5 == nullptr || !Update.setMD5(md5->value().c_str()))
        {
            setFailure(context, "The supplied MD5 checksum is invalid.");
            return;
        }
    }

    if (request->hasHeader("X-Image-SHA256"))
    {
        context->expectedSha256 = request->header("X-Image-SHA256");
        context->expectedSha256.trim();
        context->expectedSha256.toLowerCase();
        if (!validSha256Text(context->expectedSha256))
        {
            setFailure(context, "The supplied SHA-256 checksum is invalid.");
            return;
        }
    }
    else if (requireSha256)
    {
        setFailure(context, "SHA-256 verification is required for this update endpoint.");
        return;
    }

    if (mbedtls_sha256_starts_ret(&context->shaContext, 0) != 0)
    {
        setFailure(context, "Could not initialize SHA-256 verification.");
        return;
    }
    context->shaStarted = true;

    if (!Update.begin(UPDATE_SIZE_UNKNOWN, updateCommand(target)))
    {
        Update.printError(Serial);
        setFailure(context, "The ESP32 could not start the update.");
    }
}

void handleUploadChunk(
    AsyncWebServerRequest *request,
    const String &filename,
    size_t index,
    uint8_t *data,
    size_t len,
    bool final,
    OtaTarget fixedTarget,
    bool inferTargetFromFilename,
    bool acceptOptionalMd5,
    bool requireSha256)
{
    // Never write OTA data until administrator credentials have been verified.
    // This explicit check protects the upload body itself, not only the final
    // HTTP response.
    if (!WebSecurity::isAdminAuthenticated(request) ||
        (requireSha256 && !WebSecurity::isStateChangeRequestAllowed(request)))
    {
        return;
    }

    if (index == 0)
    {
        OtaTarget target = fixedTarget;

        // Compatibility with the historical /update payload format: a filesystem
        // upload uses the multipart filename "filesystem".
        if (inferTargetFromFilename)
        {
            target = (filename == "filesystem")
                         ? OtaTarget::Filesystem
                         : OtaTarget::Firmware;
        }

        beginUpload(request, filename, target, acceptOptionalMd5, requireSha256);
    }

    UploadContext *context = getContext(request);
    if (context == nullptr || context->failed || !context->ownsUpdater)
    {
        return;
    }

    if (len > 0)
    {
        if (index == 0)
        {
            const bool looksLikeFirmware = data[0] == 0xE9;
            if (context->target == OtaTarget::Firmware && !looksLikeFirmware)
            {
                setFailure(context, "This file does not look like an ESP32 firmware image (missing 0xE9 image magic).");
                return;
            }
            if (context->target == OtaTarget::Filesystem && looksLikeFirmware)
            {
                setFailure(context, "This file looks like firmware.bin. Do not upload firmware to the web-filesystem target.");
                return;
            }
        }

        if (context->shaStarted &&
            mbedtls_sha256_update_ret(&context->shaContext, data, len) != 0)
        {
            setFailure(context, "SHA-256 calculation failed during upload.");
            return;
        }

        const size_t written = Update.write(data, len);
        context->bytesWritten += written;

        if (written != len)
        {
            Update.printError(Serial);
            setFailure(context, "The ESP32 could not write the complete update image.");
            return;
        }
    }

    if (final)
    {
        if (context->shaStarted)
        {
            uint8_t digest[32];
            if (mbedtls_sha256_finish_ret(&context->shaContext, digest) != 0)
            {
                setFailure(context, "SHA-256 finalization failed.");
                return;
            }
            context->shaStarted = false;
            context->actualSha256 = sha256Hex(digest, sizeof(digest));
            if (context->expectedSha256.length() > 0 &&
                context->actualSha256 != context->expectedSha256)
            {
                setFailure(context, "SHA-256 verification failed: uploaded bytes do not match the selected file.");
                return;
            }
        }

        if (!Update.end(true))
        {
            Update.printError(Serial);
            setFailure(context, "The ESP32 could not finalize the update image.");
            return;
        }

        Serial.printf(
            "OTA: %s image written successfully (%u bytes, SHA-256 %s).\n",
            targetLabel(context->target),
            static_cast<unsigned int>(context->bytesWritten),
            context->actualSha256.c_str());
    }
}

void finishUpload(AsyncWebServerRequest *request, bool legacyResponse)
{
    UploadContext *context = getContext(request);

    if (context == nullptr)
    {
        request->send(
            500,
            legacyResponse ? "text/plain" : "application/json",
            legacyResponse
                ? "FAIL"
                : "{\"ok\":false,\"message\":\"No update file was received.\"}");
        return;
    }

    const bool success =
        context->ownsUpdater &&
        !context->failed &&
        !Update.hasError();

    const OtaTarget target = context->target;
    const bool filesystemWasMounted = context->filesystemWasMounted;
    const char *errorMessage = context->errorMessage;
    const String actualSha256 = context->actualSha256;

    if (context->ownsUpdater)
    {
        otaBusy = false;
    }

    request->_tempObject = nullptr;
    delete context;

    if (!success)
    {
        // A failed filesystem update should restore the previous filesystem
        // when it is still mountable. If the partition was partially written,
        // the mount may fail and the firmware-resident recovery UI takes over.
        if (filesystemWasMounted)
        {
            FilesystemManager::begin();
        }

        Serial.printf(
            "OTA ERROR: %s\n",
            errorMessage != nullptr ? errorMessage : "Update failed.");

        SettingsManager::saveString(Config::NvsKey::OTA_RESULT, "failed");
        SettingsManager::saveString(
            Config::NvsKey::OTA_TARGET,
            target == OtaTarget::Firmware ? "firmware" : "filesystem");
        SettingsManager::saveString(Config::NvsKey::OTA_SHA256, actualSha256);

        if (legacyResponse)
        {
            request->send(500, "text/plain", "FAIL");
        }
        else
        {
            String body = "{\"ok\":false,\"message\":\"";
            body += errorMessage != nullptr ? errorMessage : "Update failed.";
            body += "\"}";
            request->send(500, "application/json", body);
        }
        return;
    }

    SettingsManager::saveString(Config::NvsKey::OTA_RESULT, "success");
    SettingsManager::saveString(
        Config::NvsKey::OTA_TARGET,
        target == OtaTarget::Firmware ? "firmware" : "filesystem");
    SettingsManager::saveString(Config::NvsKey::OTA_SHA256, actualSha256);

    if (legacyResponse)
    {
        request->send(200, "text/plain", "OK");
    }
    else
    {
        String body = "{\"ok\":true,\"target\":\"";
        body += target == OtaTarget::Firmware ? "firmware" : "filesystem";
        body += "\",\"message\":\"";
        body += target == OtaTarget::Firmware
                    ? "Firmware uploaded successfully."
                    : "Web interface uploaded successfully.";
        body += "\",\"sha256\":\"";
        body += actualSha256;
        body += "\",\"currentFirmwareVersion\":\"";
        body += VersionInfo::FIRMWARE_VERSION;
        body += "\",\"restartInMs\":";
        body += String(OTA_RESTART_DELAY_MS);
        body += "}";

        request->send(200, "application/json", body);
    }

    DiagnosticsManager::recordControlledRestart();

    // Deliberately delay the reboot. The old OTA page restarted almost
    // immediately, which often left the browser displaying only "100%".
    // This gives the browser enough time to show a clear success message.
    scheduleRestart();
}

void addAdminResponseHeaders(AsyncWebServerResponse *response, bool html)
{
    if (response == nullptr) return;
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("X-Content-Type-Options", "nosniff");
    response->addHeader("X-Frame-Options", "DENY");
    response->addHeader("Referrer-Policy", "no-referrer");
    if (html)
    {
        response->addHeader(
            "Content-Security-Policy",
            "default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
    }
}

String systemInfoJson()
{
    const bool wifiConnected = WiFi.status() == WL_CONNECTED;
    String ip = appState.network.ip;

    if (wifiConnected)
    {
        ip = WiFi.localIP().toString();
    }
    else
    {
        const wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
        {
            ip = WiFi.softAPIP().toString();
        }
    }

    String json;
    json.reserve(560);

    json = "{\"firmwareVersion\":\"";
    json += VersionInfo::FIRMWARE_VERSION;
    json += "\",\"build\":\"";
    json += VersionInfo::buildTimestamp();
    json += "\",\"nodeId\":\"";
    json += settings.nodeId;
    json += "\",\"ip\":\"";
    json += ip;
    json += "\",\"mac\":\"";
    json += appState.network.mac;
    json += "\",\"wifi\":";
    json += wifiConnected ? "true" : "false";
    json += ",\"mqtt\":";
    json += appState.network.mqttConnected ? "true" : "false";
    json += ",\"mqttTls\":";
    json += settings.mqttTlsEnabled ? "true" : "false";
    json += ",\"filesystem\":";
    json += FilesystemManager::isReady() ? "true" : "false";
    json += ",\"freeHeapBytes\":";
    json += String(static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
    json += ",\"minFreeHeapBytes\":";
    json += String(static_cast<unsigned long>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)));
    json += ",\"largestFreeBlockBytes\":";
    json += String(static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    json += ",\"wifiRssiDbm\":";
    json += String(wifiConnected ? WiFi.RSSI() : 0);
    json += ",\"uptimeSeconds\":";
    json += String(millis() / 1000UL);
    json += ",\"bootId\":";
    json += String(bootId);
    json += "}";

    return json;
}

// The HTML/CSS/JavaScript for /system is kept in a separate firmware header
// so OTA transport logic remains readable.


void registerFixedUploadEndpoint(
    AsyncWebServer &server,
    const char *path,
    OtaTarget target)
{
    server.on(
        path,
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdmin(request)) return;
            if (!WebSecurity::requireStateChange(request)) return;
            finishUpload(request, false);
        },
        [target](
            AsyncWebServerRequest *request,
            String filename,
            size_t index,
            uint8_t *data,
            size_t len,
            bool final)
        {
            handleUploadChunk(
                request,
                filename,
                index,
                data,
                len,
                final,
                target,
                false,
                false,
                true);
        });
}

void registerLegacyUploadEndpoint(AsyncWebServer &server)
{
    // Keep the historical /update payload shape for controlled compatibility.
    // Authentication is now cookie-session based, so direct legacy uploader
    // scripts must first obtain an administrator session. Browser GET requests
    // are redirected to the Room Monitor System page.
    server.on(
        "/update",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdminPage(request, "/system")) return;
            request->redirect("/system");
        });

    server.on(
        "/update",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdmin(request)) return;
            finishUpload(request, true);
        },
        [](AsyncWebServerRequest *request,
           String filename,
           size_t index,
           uint8_t *data,
           size_t len,
           bool final)
        {
            handleUploadChunk(
                request,
                filename,
                index,
                data,
                len,
                final,
                OtaTarget::Firmware,
                true,
                true,
                false);
        });
}
} // namespace

void OtaManager::begin(AsyncWebServer &server)
{
    if (bootId == 0)
    {
        bootId = esp_random();
        if (bootId == 0)
        {
            bootId = 1;
        }
    }

    if (restartTimer == nullptr)
    {
        restartTimer = xTimerCreate(
            "otaRestart",
            pdMS_TO_TICKS(OTA_RESTART_DELAY_MS),
            pdFALSE,
            nullptr,
            restartTimerCallback);
    }

    // IMPORTANT: register the specific GET endpoint before /system. In
    // ESPAsyncWebServer a callback route can match child paths as a prefix;
    // registering /system first would cause GET /system/info to receive the
    // HTML page instead of JSON.
    server.on(
        "/system/info",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdmin(request)) return;
            AsyncWebServerResponse *response = request->beginResponse(
                200,
                "application/json",
                systemInfoJson());
            addAdminResponseHeaders(response, false);
            request->send(response);
        });

    server.on(
        "/system",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (!WebSecurity::requireAdminPage(request, "/system")) return;
            AsyncWebServerResponse *response = request->beginResponse(
                200,
                "text/html",
                OtaUi::SYSTEM_PAGE);
            addAdminResponseHeaders(response, true);
            request->send(response);
        });

    registerFixedUploadEndpoint(
        server,
        "/system/upload/firmware",
        OtaTarget::Firmware);

    registerFixedUploadEndpoint(
        server,
        "/system/upload/filesystem",
        OtaTarget::Filesystem);

    registerLegacyUploadEndpoint(server);

    Serial.println("OTA manager ready: /system");
}
