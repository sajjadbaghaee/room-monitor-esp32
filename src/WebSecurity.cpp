#include "WebSecurity.h"

#include <ESPAsyncWebServer.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>

#include "AppConfig.h"
#include "AppState.h"
#include "DiagnosticsManager.h"
#include "Secrets.h"
#include "SettingsManager.h"

// Fail closed: a published sample secret must not build into working firmware.
#if !defined(ROOM_MONITOR_SECRETS_CONFIGURED) || ROOM_MONITOR_SECRETS_CONFIGURED != 1
#error "Create include/Secrets.h with python tools/generate_secrets.py before building"
#endif
static_assert(sizeof(ROOM_MONITOR_ADMIN_PASSWORD) - 1 >= 16,
              "The bootstrap administrator password must be at least 16 characters");

namespace
{
constexpr char SESSION_COOKIE_NAME[] = "RMSESSION";
constexpr uint32_t SESSION_TTL_MS = 8UL * 60UL * 60UL * 1000UL;
constexpr uint32_t SESSION_MAX_AGE_SEC = SESSION_TTL_MS / 1000UL;
constexpr size_t SESSION_TOKEN_BYTES = 16;
constexpr size_t PASSWORD_SALT_BYTES = 16;
constexpr size_t MAX_SESSIONS = 4;
constexpr size_t MIN_PASSWORD_LENGTH = 10;
constexpr size_t MAX_PASSWORD_LENGTH = 64;
constexpr uint8_t MAX_FAILED_LOGINS = 5;
constexpr uint32_t LOGIN_LOCKOUT_MS = 30000UL;

struct AdminSession
{
    String token;
    uint32_t lastSeenMs = 0;
};

AdminSession sessions[MAX_SESSIONS];
uint8_t failedLoginAttempts = 0;
uint32_t loginLockoutUntilMs = 0;

String hexEncode(const uint8_t *bytes, size_t length)
{
    static const char HEX_DIGITS[] = "0123456789abcdef";
    String out;
    out.reserve(length * 2);

    for (size_t i = 0; i < length; ++i)
    {
        out += HEX_DIGITS[(bytes[i] >> 4) & 0x0F];
        out += HEX_DIGITS[bytes[i] & 0x0F];
    }

    return out;
}

String randomHex(size_t byteCount)
{
    String out;
    out.reserve(byteCount * 2);

    while (byteCount > 0)
    {
        const uint32_t value = esp_random();
        uint8_t bytes[4] = {
            static_cast<uint8_t>(value & 0xFF),
            static_cast<uint8_t>((value >> 8) & 0xFF),
            static_cast<uint8_t>((value >> 16) & 0xFF),
            static_cast<uint8_t>((value >> 24) & 0xFF)};

        const size_t take = byteCount > 4 ? 4 : byteCount;
        out += hexEncode(bytes, take);
        byteCount -= take;
    }

    return out;
}

String sha256Hex(const String &value)
{
    uint8_t digest[32];
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);

    if (mbedtls_sha256_starts_ret(&context, 0) != 0 ||
        mbedtls_sha256_update_ret(
            &context,
            reinterpret_cast<const unsigned char *>(value.c_str()),
            value.length()) != 0 ||
        mbedtls_sha256_finish_ret(&context, digest) != 0)
    {
        mbedtls_sha256_free(&context);
        return "";
    }

    mbedtls_sha256_free(&context);
    return hexEncode(digest, sizeof(digest));
}

bool constantTimeEquals(const String &left, const String &right)
{
    if (left.length() != right.length())
    {
        return false;
    }

    uint8_t difference = 0;
    for (size_t i = 0; i < left.length(); ++i)
    {
        difference |= static_cast<uint8_t>(left[i] ^ right[i]);
    }

    return difference == 0;
}

String storedCredential()
{
    return SettingsManager::getString(
        Config::NvsKey::ADMIN_CREDENTIAL,
        "");
}

bool parseCredential(
    const String &record,
    String &salt,
    String &hash)
{
    // v1:<32 hex salt>:<64 hex SHA-256>
    if (!record.startsWith("v1:"))
    {
        return false;
    }

    const int separator = record.indexOf(':', 3);
    if (separator < 0)
    {
        return false;
    }

    salt = record.substring(3, separator);
    hash = record.substring(separator + 1);

    return salt.length() == PASSWORD_SALT_BYTES * 2 &&
           hash.length() == 64;
}

bool verifyPassword(const String &password)
{
    const String record = storedCredential();
    if (record.length() == 0)
    {
        // Before the first dashboard password change, use the device's initial
        // password from Secrets.h. Factory reset intentionally returns here.
        return constantTimeEquals(password, ROOM_MONITOR_ADMIN_PASSWORD);
    }

    String salt;
    String expectedHash;
    if (!parseCredential(record, salt, expectedHash))
    {
        Serial.println("SECURITY ERROR: Invalid administrator credential record in NVS.");
        return false;
    }

    const String calculated = sha256Hex(salt + ":" + password);
    return calculated.length() == expectedHash.length() &&
           constantTimeEquals(calculated, expectedHash);
}

void invalidateExpiredSessions()
{
    const uint32_t now = millis();

    for (size_t i = 0; i < MAX_SESSIONS; ++i)
    {
        if (sessions[i].token.length() == 0)
        {
            continue;
        }

        if (static_cast<uint32_t>(now - sessions[i].lastSeenMs) > SESSION_TTL_MS)
        {
            sessions[i].token = "";
            sessions[i].lastSeenMs = 0;
        }
    }
}

void invalidateAllSessions()
{
    for (size_t i = 0; i < MAX_SESSIONS; ++i)
    {
        sessions[i].token = "";
        sessions[i].lastSeenMs = 0;
    }
}

String cookieValue(AsyncWebServerRequest *request, const char *name)
{
    if (request == nullptr || !request->hasHeader("Cookie"))
    {
        return "";
    }

    const String cookieHeader = request->header("Cookie");
    const String prefix = String(name) + "=";
    int start = 0;

    while (start < static_cast<int>(cookieHeader.length()))
    {
        while (start < static_cast<int>(cookieHeader.length()) &&
               (cookieHeader[start] == ' ' || cookieHeader[start] == ';'))
        {
            ++start;
        }

        int end = cookieHeader.indexOf(';', start);
        if (end < 0)
        {
            end = cookieHeader.length();
        }

        const String item = cookieHeader.substring(start, end);
        if (item.startsWith(prefix))
        {
            return item.substring(prefix.length());
        }

        start = end + 1;
    }

    return "";
}

int findSession(const String &token)
{
    if (token.length() == 0)
    {
        return -1;
    }

    invalidateExpiredSessions();

    for (size_t i = 0; i < MAX_SESSIONS; ++i)
    {
        if (sessions[i].token.length() > 0 &&
            constantTimeEquals(sessions[i].token, token))
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

String createSession()
{
    invalidateExpiredSessions();

    size_t slot = MAX_SESSIONS;
    uint32_t oldestAge = 0;
    const uint32_t now = millis();

    for (size_t i = 0; i < MAX_SESSIONS; ++i)
    {
        if (sessions[i].token.length() == 0)
        {
            slot = i;
            break;
        }

        const uint32_t age = static_cast<uint32_t>(now - sessions[i].lastSeenMs);
        if (slot == MAX_SESSIONS || age > oldestAge)
        {
            slot = i;
            oldestAge = age;
        }
    }

    if (slot == MAX_SESSIONS)
    {
        slot = 0;
    }

    sessions[slot].token = randomHex(SESSION_TOKEN_BYTES);
    sessions[slot].lastSeenMs = now;
    return sessions[slot].token;
}

void clearCookie(AsyncWebServerResponse *response)
{
    if (response != nullptr)
    {
        response->addHeader(
            "Set-Cookie",
            String(SESSION_COOKIE_NAME) +
                "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    }
}

String loginRedirect(const char *nextPath)
{
    String path = nextPath != nullptr ? String(nextPath) : String("/system");
    if (!path.startsWith("/") || path.startsWith("//"))
    {
        path = "/system";
    }

    // The known protected page paths contain only URL-safe characters.
    return String("/login?next=") + path;
}

void sendAuthenticationRequired(AsyncWebServerRequest *request)
{
    if (request == nullptr)
    {
        return;
    }

    AsyncWebServerResponse *response = request->beginResponse(
        401,
        "application/json",
        "{\"ok\":false,\"message\":\"Administrator login required.\"}");
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

void sendStateChangeRejected(AsyncWebServerRequest *request)
{
    if (request == nullptr) return;
    AsyncWebServerResponse *response = request->beginResponse(
        403,
        "application/json",
        "{\"ok\":false,\"message\":\"Request rejected by CSRF/origin protection. Refresh the Room Monitor page and try again.\"}");
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}
} // namespace

bool WebSecurity::isAdminAuthenticated(AsyncWebServerRequest *request)
{
    const String token = cookieValue(request, SESSION_COOKIE_NAME);
    const int sessionIndex = findSession(token);
    if (sessionIndex < 0)
    {
        return false;
    }

    sessions[sessionIndex].lastSeenMs = millis();
    return true;
}

bool WebSecurity::requireAdmin(AsyncWebServerRequest *request)
{
    if (isAdminAuthenticated(request))
    {
        return true;
    }

    sendAuthenticationRequired(request);
    return false;
}

bool WebSecurity::isStateChangeRequestAllowed(AsyncWebServerRequest *request)
{
    if (request == nullptr || !request->hasHeader("X-RoomMonitor-Request"))
    {
        return false;
    }

    if (request->header("X-RoomMonitor-Request") != "1")
    {
        return false;
    }

    // Modern browsers send Origin on POST/XHR requests. When present, require
    // it to match the Host header exactly. The custom header remains the
    // primary CSRF barrier for clients that omit Origin.
    if (request->hasHeader("Origin"))
    {
        const String origin = request->header("Origin");
        if (origin.length() > 0 && origin != "null")
        {
            const String expected = String("http://") + request->header("Host");
            if (origin != expected) return false;
        }
    }

    return true;
}

bool WebSecurity::requireStateChange(AsyncWebServerRequest *request)
{
    if (isStateChangeRequestAllowed(request)) return true;
    sendStateChangeRejected(request);
    return false;
}

bool WebSecurity::requireAdminPage(
    AsyncWebServerRequest *request,
    const char *nextPath)
{
    if (isAdminAuthenticated(request))
    {
        return true;
    }

    if (request != nullptr)
    {
        request->redirect(loginRedirect(nextPath));
    }
    return false;
}

bool WebSecurity::isProvisioningMode()
{
    return settings.wifiSsid.length() == 0;
}

bool WebSecurity::requireAdminUnlessProvisioning(AsyncWebServerRequest *request)
{
    return isProvisioningMode() || requireAdmin(request);
}

bool WebSecurity::requireAdminPageUnlessProvisioning(
    AsyncWebServerRequest *request,
    const char *nextPath)
{
    return isProvisioningMode() || requireAdminPage(request, nextPath);
}

const char *WebSecurity::adminUsername()
{
    return ROOM_MONITOR_ADMIN_USERNAME;
}

bool WebSecurity::login(
    const String &username,
    const String &password,
    String &sessionToken,
    String &message)
{
    const uint32_t now = millis();
    if (loginLockoutUntilMs != 0 &&
        static_cast<int32_t>(loginLockoutUntilMs - now) > 0)
    {
        const uint32_t secondsRemaining =
            (static_cast<uint32_t>(loginLockoutUntilMs - now) + 999UL) / 1000UL;
        sessionToken = "";
        message = "Too many failed sign-in attempts. Try again in ";
        message += String(secondsRemaining);
        message += " seconds.";
        return false;
    }

    loginLockoutUntilMs = 0;

    if (!constantTimeEquals(username, ROOM_MONITOR_ADMIN_USERNAME) ||
        !verifyPassword(password))
    {
        sessionToken = "";
        ++failedLoginAttempts;
        if (failedLoginAttempts >= MAX_FAILED_LOGINS)
        {
            failedLoginAttempts = 0;
            loginLockoutUntilMs = now + LOGIN_LOCKOUT_MS;
            message = "Too many failed sign-in attempts. Try again in 30 seconds.";
        }
        else
        {
            message = "Invalid username or password.";
        }
        DiagnosticsManager::recordLoginFailure();
        return false;
    }

    failedLoginAttempts = 0;
    loginLockoutUntilMs = 0;

    sessionToken = createSession();
    if (sessionToken.length() == 0)
    {
        message = "Could not create an administrator session.";
        return false;
    }

    DiagnosticsManager::recordLoginSuccess();
    message = "Signed in successfully.";
    return true;
}

void WebSecurity::addSessionCookie(
    AsyncWebServerResponse *response,
    const String &sessionToken)
{
    if (response == nullptr || sessionToken.length() == 0)
    {
        return;
    }

    String cookie = String(SESSION_COOKIE_NAME) + "=" + sessionToken;
    cookie += "; Path=/; HttpOnly; SameSite=Strict; Max-Age=";
    cookie += String(SESSION_MAX_AGE_SEC);
    response->addHeader("Set-Cookie", cookie);
}

void WebSecurity::logout(
    AsyncWebServerRequest *request,
    AsyncWebServerResponse *response)
{
    const String token = cookieValue(request, SESSION_COOKIE_NAME);
    const int sessionIndex = findSession(token);
    if (sessionIndex >= 0)
    {
        sessions[sessionIndex].token = "";
        sessions[sessionIndex].lastSeenMs = 0;
    }

    clearCookie(response);
}

bool WebSecurity::changeAdminPassword(
    const String &currentPassword,
    const String &newPassword,
    const String &confirmPassword,
    String &message)
{
    if (!verifyPassword(currentPassword))
    {
        message = "The current administrator password is incorrect.";
        return false;
    }

    if (newPassword.length() < MIN_PASSWORD_LENGTH)
    {
        message = "The new password must contain at least 10 characters.";
        return false;
    }

    if (newPassword.length() > MAX_PASSWORD_LENGTH)
    {
        message = "The new password must not exceed 64 characters.";
        return false;
    }

    if (newPassword != confirmPassword)
    {
        message = "The new password and confirmation do not match.";
        return false;
    }

    if (constantTimeEquals(currentPassword, newPassword))
    {
        message = "Choose a new password that is different from the current password.";
        return false;
    }

    const String salt = randomHex(PASSWORD_SALT_BYTES);
    const String hash = sha256Hex(salt + ":" + newPassword);
    if (salt.length() != PASSWORD_SALT_BYTES * 2 || hash.length() != 64)
    {
        message = "The device could not generate the new password credential.";
        return false;
    }

    const String credential = String("v1:") + salt + ":" + hash;
    if (!SettingsManager::saveString(Config::NvsKey::ADMIN_CREDENTIAL, credential))
    {
        message = "The new administrator password could not be saved to NVS.";
        return false;
    }

    invalidateAllSessions();
    message = "Password changed successfully. Sign in again with the new password.";
    return true;
}

bool WebSecurity::hasRuntimePassword()
{
    return storedCredential().length() > 0;
}

size_t WebSecurity::activeSessionCount()
{
    invalidateExpiredSessions();
    size_t count = 0;
    for (size_t i = 0; i < MAX_SESSIONS; ++i)
    {
        if (sessions[i].token.length() > 0) ++count;
    }
    return count;
}
