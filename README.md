# Room Monitor v7.3.2 — ESP32 environmental monitoring

ESP32 / PlatformIO firmware for local indoor-environment monitoring and MQTT telemetry. This repository is a **sanitized public source-code release**, not a ready-to-deploy device image. All broker, device and backend record IDs in the source are safe example values.


> [!WARNING]
> **Security Notice — Development and Trusted-LAN Use**
>
> Room Monitor v7.3.2 is intended for research, prototyping,
> and deployment on trusted local networks.
>
> - The web management interface uses HTTP, not HTTPS.
> - Wi-Fi provisioning is unauthenticated.
> - Do not expose the device directly to the Internet.
> - Use unique administrator and MQTT credentials.
> - Enable MQTT TLS with certificate verification.
> - Restrict device access to trusted networks.
>
> Review [SECURITY.md](SECURITY.md) and
> [PRODUCTION_SECURITY.md](PRODUCTION_SECURITY.md)
> before deploying this firmware.


## Features

- Indoor monitoring with BME680/BSEC, BH1750, analog sound input and MAX7219 LED matrix.
- Wi-Fi provisioning and automatic reconnect; MQTT publish/subscribe with optional TLS and CA hostname verification.
- Live Dashboard, browser-session measurement plots, protected Configuration and Diagnostics pages.
- MQTT/TLS connection test, credential management, non-secret configuration export/import, and verified firmware/SPIFFS OTA.
- Watchdog, sensor health, reset and Wi-Fi/MQTT diagnostics.

## Requirements

- Compatible ESP32 development board (`esp32doit-devkit-v1` target in `platformio.ini`).
- Python 3, [PlatformIO](https://platformio.org/), and USB access for initial flashing.
- Correctly wired sensors/display (review pin assignments in `include/AppConfig.h`).
- An MQTT broker and any backend record IDs required by your own ingestion service.

## Quick start

1. Clone or extract this repository and open the directory containing `platformio.ini`.
2. Generate a **private** initial administrator password with:

   ```bash
   python tools/generate_secrets.py
   ```

   This creates `include/Secrets.h` locally and prints **no password**. Open the file privately to record the random bootstrap password in a password manager. The file is ignored by Git. Never share it. For deliberate manual configuration, copy `include/Secrets.example.h` to `include/Secrets.h`, replace its placeholder with a unique 16+ character password, and set `ROOM_MONITOR_SECRETS_CONFIGURED` to `1`.
3. Validate and build:

   ```bash
   python tools/audit_public_release.py
   python tools/validate_project.py
   python tools/check_inline_js.py
   platformio run
   platformio run -t buildfs
   ```

4. On an initial USB installation, upload the firmware and then the web assets:

   ```bash
   platformio run -t upload
   platformio run -t uploadfs
   ```

5. Connect to the device's provisioning access point if Wi-Fi is not configured; open `http://192.168.4.1/` to set Wi-Fi. The provisioning AP has no password in this firmware: **commission only in a physically trusted environment**.
6. Sign in to `/login` with the username from your local secrets header (normally `admin`) and the generated bootstrap password. From `/configuration` set a unique node ID, your actual MQTT hostname/port, publish/subscribe topics, credentials, TLS trust anchor and **all backend measurement IDs**. Change the administrator password in the Dashboard after commissioning.
7. Verify Wi-Fi, sensors and MQTT through `/system` and `/diagnostics`. Test subscriptions with the Configuration page's **Test MQTT / TLS connection** action.

**Important:** `broker.example.invalid` cannot connect to a broker. The `CONFIGURE_*_ID` strings are intentional placeholders, not real backend IDs. A fresh installation will not deliver production data until you configure it. An existing deployed device's NVS settings are *not overwritten* by changing these source defaults, including through firmware OTA. Take a secure configuration backup before any upgrades.

## Web interface

| Page | URL | Authentication |
| --- | --- | --- |
| Dashboard | `/` | Read-only view available on the device LAN; admin controls require sign-in |
| Measurements | `/measurements.html` | Public on the device LAN |
| Administrator sign-in | `/login` | Credentials required |
| Wi-Fi | `/wifi` | Admin except factory-reset provisioning |
| Device & MQTT configuration | `/configuration` | Admin |
| Diagnostics | `/diagnostics` | Admin |
| OTA and device status | `/system` | Admin |

## Repository layout

- `include/`, `src/`: firmware, sensors, MQTT/TLS, HTTP administration, diagnostics, OTA.
- `data/`: SPIFFS web interface and styles.
- `tools/`: tests, public-release safety checks and local bootstrap secret generation.
- `.github/workflows/platformio.yml`: validation and build in CI with an ephemeral CI-only bootstrap credential.
- `HELP.md`: commissioning, troubleshooting and recovery guide.
- `SECURITY.md`, `PRODUCTION_SECURITY.md`: security model and limitations.
- `ARCHITECTURE.md`: firmware architecture and component overview.
- `CHANGELOG.md`: change history.

## Security and privacy

- **No live credentials or production broker/identity values are shipped in this release.** Never commit `include/Secrets.h`, Wi-Fi/MQTT passwords, CA private keys, diagnostic logs, device dumps, or configuration exports containing deployment details.
- The device's local management interface is **HTTP, not HTTPS**. Credentials and sessions are not encrypted on the LAN; use only trusted local networks or a properly configured VPN/TLS gateway. Do **not** port-forward it to the Internet.
- Provisioning mode is deliberately unauthenticated; secure the physical commissioning environment. MQTT plaintext is supported for compatibility but **enable TLS with CA/hostname validation for production**.
- TLS, secure boot, flash encryption and NVS encryption are separate design concerns; application-layer controls alone are not a full production security review.
- For an accidentally committed secret, removing it in a later commit is **not enough**: rotate the secret and clean the Git history if applicable.

## Tests and limitations

Run `python tools/validate_project.py`, `python tools/check_inline_js.py` (Node.js required), and `python tools/audit_public_release.py` before publication. Automated tests do not replace hardware validation or a security penetration test. The firmware depends on upstream libraries referenced in `platformio.ini`; verify dependency maintenance and licensing before deploying broadly.

For GitHub publication, verify ignored credentials with `git ls-files include/Secrets.h` (expected: no output), review staged files, then commit and push to a repository you control. Do not publish device-specific settings or credentials.

## License

Room Monitor v7.3.2 is licensed under the
Apache License, Version 2.0.

See [LICENSE](LICENSE) for the full license terms.

## Contributing

Contributions and bug reports are welcome.

Do not include passwords, API keys, certificates'
private keys, device credentials, or confidential
configuration details in issues or pull requests.

