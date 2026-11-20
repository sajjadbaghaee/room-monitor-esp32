# Room Monitor — Help and Troubleshooting

This guide covers the sanitized v7.3.2 ESP32 PlatformIO firmware. For a full overview, see [README.md](README.md).

## First-time setup

1. Install Python 3, PlatformIO and, for JS verification, Node.js.
2. In the repository root, run `python tools/generate_secrets.py`. Your bootstrap password is stored *only* in the ignored `include/Secrets.h` file; keep it private and do not copy it into GitHub issues.
3. Connect the ESP32 with USB. Run `platformio run` followed by `platformio run -t upload`. Run `platformio run -t buildfs` followed by `platformio run -t uploadfs` to install the web files.
4. If there is no saved Wi-Fi network, join the device AP `RoomMonitoring-WIFI-MANAGER` and open `http://192.168.4.1/`. The provisioning AP is unauthenticated; do this only in a controlled location.
5. Sign in at `/login` with the local bootstrap credentials. Configure node ID, MQTT host, topics, any MQTT password, TLS/CA settings and backend sensor IDs on `/configuration`.
6. On the Dashboard, change the administrator password; test telemetry from Configuration. Inspect `/diagnostics` if anything fails.

## Common problems

| Symptom | Likely cause | Suggested action |
| --- | --- | --- |
| `Secrets.h: No such file or directory` or configuration `#error` | Private bootstrap header has not been generated | Run `python tools/generate_secrets.py`. Never commit the output. |
| The MQTT connection never comes up | Public sample hostname ends with `.invalid` | Set your own actual broker hostname, port, username/password and topics in Configuration. |
| TLS handshake or certificate verification fails | Missing/mismatched CA, broker hostname mismatch, or unsynchronized clock | Use the certificate's DNS hostname, import the correct CA PEM, confirm time sync and re-run **Test MQTT / TLS connection**. |
| MQTT test shows `PARTIAL` / `NOT RUN` | Not enough safe heap to create a second TLS test session | With saved settings, test the already-connected production client; check heap and event details in Diagnostics. |
| Wi-Fi will not connect | Wrong SSID/password, weak signal, or incorrect configuration | Reopen `/wifi`, scan networks, save correct credentials and check the serial monitor at 115200 baud. |
| Configuration saves but backend receives no records | Backend sensor IDs are still `CONFIGURE_*` examples, wrong node/topic, or broker not reachable | Set all required backend IDs and topics to valid values for your own backend. |
| Dashboard styles/pages missing after firmware update | SPIFFS image missing or incompatible | Build the filesystem image with `platformio run -t buildfs`, then upload the compatible filesystem image using the protected `/system` page after firmware OTA. |
| Login doesn't accept the generated password after upgrade | A changed administrator password is already stored in NVS | Use the active password; local compile-time bootstrap credentials apply only on first boot or after factory reset. |

## Diagnostics checklist

Check Wi-Fi SSID and IP, time synchronization, MQTT host and port, TLS mode/CA chain, heap free/largest block, MQTT connection state, command-topic SUBACK, sensor freshness, and recent reset reason in `/diagnostics`. Treat exported config and logs as potentially sensitive even if the export excludes passwords.

## Updates and recovery

- For existing installations, **preserve device NVS** (including local network and MQTT settings). Back up configuration securely, then upgrade firmware before uploading the compatible SPIFFS asset image. Verify services and configuration after upgrading.
- Do not assume a firmware flash removes old broker credentials: NVS is separate from application code.
- Factory reset removes device settings and changed administrator credentials, returning the device to the locally compiled bootstrap password. Make a secure record of necessary settings first.
- OTA is for trusted, authenticated local administration. Never make the device's HTTP port publicly accessible.
