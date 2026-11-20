#pragma once

#include <Arduino.h>

struct CertificateInfo
{
    bool present = false;
    bool validPem = false;
    bool timeKnown = false;
    bool notYetValid = false;
    bool expired = false;
    String subject;
    String issuer;
    String validFrom;
    String validTo;
    String sha256Fingerprint;
    String error;
};

namespace CertificateManager
{
String normalizePem(String pem);
bool validatePem(const String &pem, String &error);
CertificateInfo inspect(const String &pem);
String infoJson(const String &pem);
bool save(const String &pem, String &error);
bool clear(String &error);
} // namespace CertificateManager
