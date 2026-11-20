# Room Monitor web administration security

## Authentication model

Room Monitor uses its own administrator sign-in page and a short-lived session cookie. HTTP Basic Authentication is no longer used.

The sign-in page is firmware-resident:

```text
/login
```

The administrator username remains defined in `include/Secrets.h`. The password in `Secrets.h` is the **initial / factory-reset password**. After the deployed user changes the password from the Dashboard, the new credential is stored in NVS as a salted SHA-256 record instead of plaintext.

Administrator sessions:

- use a random 128-bit token generated on the ESP32;
- are carried in an `HttpOnly` + `SameSite=Strict` cookie;
- have a browser lifetime of up to 8 hours and are also expired server-side after 8 hours of inactivity;
- are stored only in RAM, so an ESP32 restart signs everyone out;
- are invalidated when the administrator password changes;
- can be explicitly invalidated with the Dashboard **Logout** button.

Five failed sign-in attempts trigger a 30-second login cooldown.

## Protected pages and endpoints

During normal operation, administrator authentication is required for:

- `/system`
- `/system/info`
- `/system/upload/firmware`
- `/system/upload/filesystem`
- legacy `/update`
- `/wifi`
- `/api/wifi/status`
- `/api/wifi/scan`
- `/api/wifi/networks`
- `/wifi/save`
- `/api/admin/password`
- `/api/config`
- `/api/config/save`

The Dashboard (`/`) and Measurements (`/measurements.html`) remain read-only and public on the local device. The Dashboard checks whether an administrator session is active and then exposes the password-change controls and Logout button.

## Changing the administrator password

Sign in, return to the Dashboard, and use **Administrator security**. The form requires:

- current password;
- new password (10–64 characters);
- confirmation of the new password.

After a successful change, all administrator sessions are invalidated and the user must sign in again with the new password.

The runtime password is stored in the Room Monitor NVS namespace under a dedicated credential key. A factory reset clears that NVS value and restores the initial password compiled from `include/Secrets.h`.

## Factory-reset exception

After a factory reset the device has no saved Wi-Fi SSID. In this provisioning state only the Wi-Fi setup page/API is intentionally available without administrator authentication so the owner can reconnect the Room Monitor to a network.

The System/OTA page remains protected. After Wi-Fi is saved and the ESP32 restarts, the Wi-Fi manager becomes protected again.

## Transport-security limitation

The ESP32 currently serves plain HTTP. Session authentication prevents casual unauthorized use, but HTTP does **not encrypt** passwords, session cookies, or page traffic on the LAN. Do not expose port 80 directly to the Internet or an untrusted network. For stronger deployment security, place the device behind a VPN/TLS reverse proxy or add an HTTPS-capable management layer.

Because the current password record uses salted SHA-256 rather than a memory-hard password KDF, use a long, unique administrator password. The salt prevents identical-password hash reuse, but physical NVS extraction still permits offline password guessing.

## Dashboard configuration secrets

The Dashboard configuration API never returns Wi-Fi or MQTT password contents. It returns only boolean `...PasswordSet` indicators. Password inputs are write-only: blank values retain the applicable existing credential, while explicit clear controls remove it. The administrator password continues to use the separate salted SHA-256 credential record.

MQTT and Wi-Fi connection secrets must be recoverable by the device in order to connect, so they are stored as NVS configuration values rather than one-way password hashes. Treat physical flash/NVS access as privileged.


## Factory/bootstrap credential
The initial and factory-reset administrator password is generated locally in `include/Secrets.h` and must never be committed. `tools/generate_secrets.py` creates a random secret for a development build. A password changed through the Dashboard is stored as a salted record in NVS; factory reset restores the compile-time bootstrap credential. Use a unique build credential for each deployment and rotate any previously shared defaults.

The protected Device Configuration page is `/configuration`; its API routes remain `/api/config` and `/api/config/save`.


## MQTT transport security (v7)

MQTT now supports optional TLS with broker CA verification from the protected Configuration page. When **Secure MQTT (TLS + CA verification)** is enabled, the firmware uses ESP-IDF ESP-MQTT over `mqtts://`, supplies the configured root CA to ESP-TLS, and leaves certificate hostname/common-name verification enabled. MQTT username/password authentication remains independently configurable and can be used together with TLS.

The CA certificate is public trust material and is stored in NVS. MQTT username/password are also stored in NVS because the device must recover them to authenticate after reboot; their contents are never returned by the configuration GET API. The current implementation validates the server only; it does not implement mutual TLS/client certificates/private keys.

Certificate verification depends on correct time. After Wi-Fi obtains an IP address the firmware starts SNTP/system-time synchronization and defers secure MQTT connection attempts until the clock is plausible. For TLS deployments use a broker hostname that appears in the certificate SAN/CN; connecting by raw IP will fail unless the certificate explicitly contains that IP address.

## Update authenticity limitation

OTA SHA-256 checks help detect transfer errors or mismatched images, but they **do not authenticate the author of the firmware**. The browser and device communicate over HTTP: a network attacker capable of controlling the management session or upload path may substitute both image and hash. Treat OTA as a trusted-LAN administrative function; signed updates and HTTPS/TLS administration require separate design work.

## v7.3 request hardening

State-changing browser APIs now require `X-RoomMonitor-Request: 1`. When the browser supplies an `Origin` header it must match the local Room Monitor host. This complements `SameSite=Strict` session cookies and blocks ordinary cross-site forms from changing configuration, Wi-Fi, password or OTA state. The login endpoint is intentionally exempt because no authenticated state exists yet.

Configuration export excludes Wi-Fi password, MQTT password and administrator credential/session data. The CA certificate may be exported because it is a public trust anchor.

Fixed System OTA endpoints require a browser-computed SHA-256 header; the ESP32 independently hashes received bytes before finalizing the update. The legacy `/update` endpoint remains authenticated for compatibility and is considered deprecated for new tooling.

See `PRODUCTION_SECURITY.md` for Secure Boot, flash encryption and NVS-encryption deployment guidance. Those irreversible/platform-level controls are not enabled automatically.
