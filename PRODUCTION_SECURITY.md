# Production security guidance

Room Monitor v7.3 hardens application-level administration and MQTT transport, but several ESP32 hardware-security features require a separate manufacturing/provisioning decision.

## Enabled in v7.3 application firmware

- MQTT TLS with CA-chain and broker hostname/SAN verification.
- MQTT username/password support without exposing the stored password through configuration GET APIs.
- Administrator session cookies with `HttpOnly` and `SameSite=Strict`.
- Login rate limiting and changeable administrator password.
- Custom-header CSRF barrier and same-Origin validation for current state-changing browser routes.
- Security response headers on local administration pages/APIs.
- SHA-256 verified OTA uploads plus firmware/filesystem target sanity checks.
- OTA result/target/hash audit retained in NVS.

## Not enabled automatically

### Secure Boot

Secure Boot can ensure that only firmware signed by an authorized key executes. Enabling it requires controlled signing-key management and eFuse provisioning. Do not enable/burn eFuses on field devices until the signing/recovery process has been tested on sacrificial hardware.

### Flash encryption

Flash encryption can protect firmware and data at rest from direct flash extraction. It also changes flashing/recovery workflows and normally belongs in the production provisioning process rather than ordinary PlatformIO development builds.

### NVS encryption

MQTT/Wi-Fi passwords must be recoverable by the firmware, so one-way hashing is not sufficient for those secrets. NVS encryption is the appropriate at-rest control when physical flash extraction is in scope, but its keys/provisioning must be designed together with flash encryption / secure boot policy.

## Recommended production rollout

1. Keep v7.3 application behavior stable on development units.
2. Create separate development and production build/provisioning profiles.
3. Test Secure Boot / flash encryption / NVS encryption on sacrificial ESP32s.
4. Establish signing-key backup, rotation and recovery procedures before burning eFuses.
5. Disable or tightly control physical serial flashing/JTAG according to deployment requirements.
6. Keep the HTTP management interface on a trusted LAN/VPN; do not expose ESP32 port 80 directly to the Internet.
7. Generate a unique local bootstrap credential for each build and change it during commissioning.

These hardware protections are intentionally documented rather than silently enabled in the default project because irreversible eFuse changes must be an explicit deployment decision.
