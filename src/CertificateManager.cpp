#include "CertificateManager.h"

#include <time.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>

#include "AppConfig.h"
#include "AppState.h"
#include "SettingsManager.h"

namespace
{
String jsonEscape(const String &value)
{
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i)
    {
        const char c = value[i];
        switch (c)
        {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<uint8_t>(c) >= 0x20) out += c;
            break;
        }
    }
    return out;
}

String formatX509Time(const mbedtls_x509_time &value)
{
    char buffer[24];
    snprintf(
        buffer,
        sizeof(buffer),
        "%04d-%02d-%02d %02d:%02d:%02d UTC",
        value.year,
        value.mon,
        value.day,
        value.hour,
        value.min,
        value.sec);
    return String(buffer);
}

uint64_t packedTime(
    int year,
    int month,
    int day,
    int hour,
    int minute,
    int second)
{
    uint64_t value = static_cast<uint64_t>(year);
    value = value * 100ULL + static_cast<uint64_t>(month);
    value = value * 100ULL + static_cast<uint64_t>(day);
    value = value * 100ULL + static_cast<uint64_t>(hour);
    value = value * 100ULL + static_cast<uint64_t>(minute);
    value = value * 100ULL + static_cast<uint64_t>(second);
    return value;
}

String hexFingerprint(const unsigned char *data, size_t length)
{
    unsigned char digest[32];
    if (mbedtls_sha256_ret(data, length, digest, 0) != 0)
    {
        return "";
    }

    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    String out;
    out.reserve(95);
    for (size_t i = 0; i < sizeof(digest); ++i)
    {
        if (i > 0) out += ':';
        out += HEX_DIGITS[(digest[i] >> 4) & 0x0F];
        out += HEX_DIGITS[digest[i] & 0x0F];
    }
    return out;
}
} // namespace

String CertificateManager::normalizePem(String pem)
{
    pem.replace("\r\n", "\n");
    pem.replace("\r", "\n");
    pem.trim();
    if (pem.length() > 0 && !pem.endsWith("\n")) pem += '\n';
    return pem;
}

bool CertificateManager::validatePem(const String &pem, String &error)
{
    if (pem.length() == 0)
    {
        error = "Secure MQTT requires a CA certificate.";
        return false;
    }

    if (pem.length() > Config::MQTT_CA_CERT_MAX_LENGTH)
    {
        error = "The MQTT CA certificate is too large for NVS storage.";
        return false;
    }

    if (!pem.startsWith("-----BEGIN CERTIFICATE-----") ||
        pem.indexOf("-----END CERTIFICATE-----") < 0)
    {
        error = "MQTT CA certificate must be PEM encoded (BEGIN/END CERTIFICATE).";
        return false;
    }

    mbedtls_x509_crt certificate;
    mbedtls_x509_crt_init(&certificate);
    const int result = mbedtls_x509_crt_parse(
        &certificate,
        reinterpret_cast<const unsigned char *>(pem.c_str()),
        pem.length() + 1);
    mbedtls_x509_crt_free(&certificate);

    if (result != 0)
    {
        error = "The MQTT CA certificate could not be parsed completely as X.509 PEM.";
        return false;
    }

    error = "";
    return true;
}

CertificateInfo CertificateManager::inspect(const String &pem)
{
    CertificateInfo info;
    info.present = pem.length() > 0;
    if (!info.present)
    {
        info.error = "No CA certificate stored.";
        return info;
    }

    String validationError;
    if (!validatePem(pem, validationError))
    {
        info.error = validationError;
        return info;
    }

    mbedtls_x509_crt certificate;
    mbedtls_x509_crt_init(&certificate);
    const int result = mbedtls_x509_crt_parse(
        &certificate,
        reinterpret_cast<const unsigned char *>(pem.c_str()),
        pem.length() + 1);

    if (result != 0)
    {
        info.error = "X.509 parsing did not consume the complete PEM certificate.";
        mbedtls_x509_crt_free(&certificate);
        return info;
    }

    info.validPem = true;

    char buffer[320];
    if (mbedtls_x509_dn_gets(buffer, sizeof(buffer), &certificate.subject) > 0)
        info.subject = buffer;
    if (mbedtls_x509_dn_gets(buffer, sizeof(buffer), &certificate.issuer) > 0)
        info.issuer = buffer;

    info.validFrom = formatX509Time(certificate.valid_from);
    info.validTo = formatX509Time(certificate.valid_to);
    info.sha256Fingerprint = hexFingerprint(certificate.raw.p, certificate.raw.len);

    const time_t now = time(nullptr);
    if (now >= 1609459200)
    {
        struct tm utcNow;
        gmtime_r(&now, &utcNow);
        const uint64_t currentPacked = packedTime(
            utcNow.tm_year + 1900,
            utcNow.tm_mon + 1,
            utcNow.tm_mday,
            utcNow.tm_hour,
            utcNow.tm_min,
            utcNow.tm_sec);
        const uint64_t fromPacked = packedTime(
            certificate.valid_from.year,
            certificate.valid_from.mon,
            certificate.valid_from.day,
            certificate.valid_from.hour,
            certificate.valid_from.min,
            certificate.valid_from.sec);
        const uint64_t toPacked = packedTime(
            certificate.valid_to.year,
            certificate.valid_to.mon,
            certificate.valid_to.day,
            certificate.valid_to.hour,
            certificate.valid_to.min,
            certificate.valid_to.sec);
        info.timeKnown = true;
        info.notYetValid = currentPacked < fromPacked;
        info.expired = currentPacked > toPacked;
    }

    mbedtls_x509_crt_free(&certificate);
    return info;
}

String CertificateManager::infoJson(const String &pem)
{
    const CertificateInfo info = inspect(pem);
    String json;
    json.reserve(900);
    json = "{\"present\":";
    json += info.present ? "true" : "false";
    json += ",\"validPem\":";
    json += info.validPem ? "true" : "false";
    json += ",\"timeKnown\":";
    json += info.timeKnown ? "true" : "false";
    json += ",\"notYetValid\":";
    json += info.notYetValid ? "true" : "false";
    json += ",\"expired\":";
    json += info.expired ? "true" : "false";
    json += ",\"length\":";
    json += String(pem.length());
    json += ",\"subject\":\"" + jsonEscape(info.subject) + "\"";
    json += ",\"issuer\":\"" + jsonEscape(info.issuer) + "\"";
    json += ",\"validFrom\":\"" + jsonEscape(info.validFrom) + "\"";
    json += ",\"validTo\":\"" + jsonEscape(info.validTo) + "\"";
    json += ",\"sha256\":\"" + jsonEscape(info.sha256Fingerprint) + "\"";
    json += ",\"error\":\"" + jsonEscape(info.error) + "\"}";
    return json;
}

bool CertificateManager::save(const String &pemInput, String &error)
{
    const String pem = normalizePem(pemInput);
    if (!validatePem(pem, error)) return false;
    if (!SettingsManager::saveString(Config::NvsKey::MQTT_CA, pem))
    {
        error = "The CA certificate could not be written to NVS.";
        return false;
    }
    settings.mqttCaCert = pem;
    error = "";
    return true;
}

bool CertificateManager::clear(String &error)
{
    if (!SettingsManager::saveString(Config::NvsKey::MQTT_CA, ""))
    {
        error = "The CA certificate could not be cleared from NVS.";
        return false;
    }
    settings.mqttCaCert = "";
    error = "";
    return true;
}
