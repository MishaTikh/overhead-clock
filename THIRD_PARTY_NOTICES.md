# Third-party notices and data attribution

Overhead is MIT licensed (see [LICENSE](LICENSE)). It builds on the following work.

## Libraries compiled into the firmware

| Component | License | Use |
|---|---|---|
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) (with ESP-IDF, mbedTLS, lwIP) | LGPL-2.1 (core), Apache-2.0 (ESP-IDF, mbedTLS) | Platform, Wi-Fi, TLS |
| [FastLED](https://github.com/FastLED/FastLED) 3.7.8 | MIT | LED matrix driver |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) 7.2.1 | MIT | JSON parsing |
| [WiFiManager](https://github.com/tzapu/WiFiManager) 2.0.17 | MIT | First-boot Wi-Fi setup hotspot |

The Arduino core is LGPL-2.1. The firmware links it unmodified; to rebuild against a modified core, use the
PlatformIO project in `firmware/` (all sources and build files are here).

## Embedded data and code

| Item | Source | License |
|---|---|---|
| 5x7 font (`firmware/src/render.cpp`) | Adafruit GFX Library `glcdfont.c` | BSD license, Copyright (c) 2012 Adafruit Industries |
| Airport and runway table (`firmware/src/airports_data.h`) | [OurAirports](https://ourairports.com/data/) | Public domain |
| Time zone rules (`firmware/src/timezones_data.h`) | [tz database](https://www.iana.org/time-zones) via the `tzdata` Python package | Public domain |
| Root certificates (`firmware/src/root_cas.h`) | Mozilla CA Certificate Program via [certifi](https://github.com/certifi/python-certifi) | MPL-2.0 |
| TC001 pin map (`firmware/src/config.h`) | Documented by the [AWTRIX 3](https://github.com/Blueforcer/awtrix3) project | Reference only (facts, no code copied) |

## Online services the clock uses

Each clock talks to these services directly from its owner's home network. Each has its own terms; please respect them.

| Service | What for | Terms / attribution |
|---|---|---|
| [adsb.lol](https://adsb.lol) API | Aircraft positions near the house | Data under [ODbL 1.0](https://opendatacommons.org/licenses/odbl/); fair-use rate limits |
| adsb.lol VRS standing data | Flight routes | Community data via adsb.lol |
| [adsbdb](https://www.adsbdb.com) | Flight routes (fallback) | Free API, fair use |
| Airport flight boards (currently [mspairport.com](https://www.mspairport.com)) | Accurate origin/destination at that airport | One request per audible flight; public flight information |
| [US Census Geocoder](https://geocoding.geo.census.gov) | Address to coordinates (US) | US Government work |
| [Nominatim](https://nominatim.org) / OpenStreetMap | Address to coordinates (worldwide) | Data © [OpenStreetMap contributors](https://www.openstreetmap.org/copyright), ODbL; [usage policy](https://operations.osmfoundation.org/policies/nominatim/): one request per lookup |
| [Open-Meteo](https://open-meteo.com) | Ground elevation and time zone | [CC BY 4.0](https://open-meteo.com/en/license); free for non-commercial use |
| [aviationweather.gov](https://aviationweather.gov) (NOAA) | Airport weather (METAR) | US Government work |
| NTP (pool.ntp.org, time.nist.gov) | Time | Public time service |

## Trademarks

Ulanzi, AWTRIX and airline names are trademarks of their owners. Overhead is an independent project, not affiliated
with or endorsed by Ulanzi, any airline, airport or data provider. Airline names and colors are used only to describe
flights.
