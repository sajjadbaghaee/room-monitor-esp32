# Third-Party Software and Licensing

## Overview

Room Monitor v7.3.2 is distributed under the Apache License, Version 2.0, for original project code that the copyright holder is authorized to license.

Third-party libraries and components retain their respective licenses. The project license does not replace or override those terms.

## Direct Dependencies

| Component | License | Upstream repository |
|---|---|---|
| MD_Parola | LGPL-2.1 | https://github.com/MajicDesigns/MD_Parola |
| ESPAsyncWebServer | LGPL-3.0 | https://github.com/me-no-dev/ESPAsyncWebServer |
| BH1750 | MIT | https://github.com/claws/BH1750 |
| Adafruit Unified Sensor | Apache-2.0 | https://github.com/adafruit/Adafruit_Sensor |
| Adafruit BME680 Library | BSD | https://github.com/adafruit/Adafruit_BME680 |
| NTPClient | MIT | https://github.com/taranais/NTPClient |
| Bosch BSEC Software Library | BSD-3-Clause wrapper; separately licensed proprietary core | https://github.com/boschsensortec/BSEC-Arduino-library |

## Additional Dependencies

| Component | License | Upstream repository |
|---|---|---|
| MD_MAX72XX | LGPL-2.1 | https://github.com/MajicDesigns/MD_MAX72XX |
| AsyncTCP | LGPL-3.0 | https://github.com/ESP32Async/AsyncTCP |
| Arduino-ESP32 Core | LGPL-2.1 | https://github.com/espressif/arduino-esp32 |

Additional transitive dependencies may be included by PlatformIO. Their licenses must also be respected.

## Bosch BSEC Licensing

Bosch BSEC includes proprietary binary components that are not covered by the Apache-2.0 license of this project.

The Arduino wrapper and sensor API are separately licensed from the BSEC core.

Users must review and comply with Bosch Sensortec's applicable software license agreement before using the proprietary BSEC library.

This repository references BSEC through PlatformIO rather than distributing a separate copy of Bosch's proprietary binary library.

Distribution rights for complete firmware images incorporating BSEC have not been independently confirmed for the selected BSEC 1.x version.

## LGPL Compliance

Several libraries used by Room Monitor are licensed under LGPL-2.1 or LGPL-3.0.

Their original license terms, copyright notices, and applicable source availability and modification/relinking rights must be respected when distributing software that incorporates them.

Static linking into an embedded firmware image can create additional distribution obligations.

## Firmware Distribution

This source repository does not provide precompiled firmware releases.

Before distributing firmware binaries, the publisher must verify:

- The applicable Bosch BSEC license and redistribution permissions.
- LGPL compliance for linked libraries and the Arduino framework.
- Required copyright notices, license texts, source availability, and applicable relinking provisions.
- Licensing requirements for all other bundled dependencies.

## Disclaimer

Third-party software is governed by its original licenses.

This document is provided for dependency attribution and licensing transparency. It does not replace upstream license agreements or establish additional redistribution rights.
