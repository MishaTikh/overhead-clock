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
| [VRS standing data](https://github.com/vradarserver/standing-data) via adsb.lol | Flight routes | CC0 |
| [adsbdb](https://www.adsbdb.com) | Flight routes (fallback) | Free API, low volume. Its route data may not be copied into other databases: the clock only uses it live and caches it in memory |
| [mspairport.com](https://www.mspairport.com) flight search | Exact origin/destination near MSP | **Opt-in, off by default, unofficial.** The site's terms allow viewing for personal use; one request per audible flight, at most 300 a day. Not affiliated with the Metropolitan Airports Commission |
| [US Census Geocoder](https://geocoding.geo.census.gov) | Address to coordinates (US addresses only) | US Government work. This product uses the Census Bureau Data API but is not endorsed or certified by the Census Bureau |
| [Nominatim](https://nominatim.org) / OpenStreetMap | Address to coordinates (worldwide) | Data © [OpenStreetMap contributors](https://www.openstreetmap.org/copyright), ODbL; [usage policy](https://operations.osmfoundation.org/policies/nominatim/): one request per lookup |
| [Open-Meteo](https://open-meteo.com) | Ground elevation and time zone | Weather data by Open-Meteo.com, [CC BY 4.0](https://open-meteo.com/en/license); free tier is for non-commercial use |
| [aviationweather.gov](https://aviationweather.gov) (NOAA) | Airport weather (METAR) | US Government work |
| [Cloudflare Time Services](https://www.cloudflare.com/time/) (default, changeable) | Time (NTP) | Free public service. The public NTP pool and time.nist.gov are deliberately not used: both ask products not to hard-code them |

## Teaser video (`docs/media/overhead-teaser.mp4`)
- Footage by Joolsmagools and Sammy on [Pexels](https://www.pexels.com), under the [Pexels License](https://www.pexels.com/license/).
- Music: "Fluffing a Duck" by Kevin MacLeod ([incompetech.com](https://incompetech.com)), licensed under
  [Creative Commons: By Attribution 4.0](https://creativecommons.org/licenses/by/4.0/).
- The stock files themselves aren't in this repository; `tools/teaser/make_teaser.py` downloads them.

## Trademarks

Ulanzi, AWTRIX and airline names are trademarks of their owners. Overhead is an independent project, not affiliated
with or endorsed by Ulanzi, any airline, airport or data provider. Airline names and simple colors are used only to
describe flights; no airline logos are included. Airlines visible in the teaser footage appear incidentally.

## Natural Earth

The coastline outline on the settings page's map is derived from Natural Earth (naturalearthdata.com),
110m coastline, simplified. Natural Earth is in the public domain; no attribution is required, and it is
credited here with thanks.
