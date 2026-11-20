#pragma once

#include <Arduino.h>

class AsyncWebServerRequest;
class AsyncWebServerResponse;

namespace WebSecurity
{
// Session/cookie based administrator authentication. Sessions live only in
// RAM, so every ESP32 restart signs administrators out automatically.
bool isAdminAuthenticated(AsyncWebServerRequest *request);

// API/upload protection. On failure this sends HTTP 401 JSON.
bool requireAdmin(AsyncWebServerRequest *request);

// State-changing browser requests must carry the Room Monitor custom request
// header and, when supplied by the browser, a same-origin Origin value. This
// blocks cross-site form/CSRF requests while preserving the HttpOnly session.
bool isStateChangeRequestAllowed(AsyncWebServerRequest *request);
bool requireStateChange(AsyncWebServerRequest *request);

// Browser-page protection. On failure this redirects to the firmware-resident
// login page and remembers the requested destination.
bool requireAdminPage(AsyncWebServerRequest *request, const char *nextPath);

// Wi-Fi provisioning must remain recoverable after a factory reset. Wi-Fi
// management is therefore open only while no SSID is configured.
bool requireAdminUnlessProvisioning(AsyncWebServerRequest *request);
bool requireAdminPageUnlessProvisioning(
    AsyncWebServerRequest *request,
    const char *nextPath);

bool isProvisioningMode();
const char *adminUsername();

// Login/logout helpers used by WebManager routes.
bool login(
    const String &username,
    const String &password,
    String &sessionToken,
    String &message);
void addSessionCookie(AsyncWebServerResponse *response, const String &sessionToken);
void logout(AsyncWebServerRequest *request, AsyncWebServerResponse *response);

// Changes the administrator password stored in NVS. The current password is
// required even for an authenticated session. Successful changes invalidate
// every active administrator session.
bool changeAdminPassword(
    const String &currentPassword,
    const String &newPassword,
    const String &confirmPassword,
    String &message);

// Useful for status APIs and UI decisions.
bool hasRuntimePassword();
size_t activeSessionCount();
} // namespace WebSecurity
