# Overhead

**A little LED clock that tells you where the plane over your house is coming from (or going), only while you can
actually hear it.**

Overhead is open-source firmware for the [Ulanzi TC001](docs/hardware.md) pixel clock (an ESP32 with a 32x8 RGB LED
matrix). Live near an airport? When a plane roars over, the clock scrolls `DENVER` in Delta red, then goes back to
being a clock. No computer, server or account: it just needs Wi-Fi.

[![CI](https://github.com/MishaTikh/overhead-clock/actions/workflows/ci.yml/badge.svg)](https://github.com/MishaTikh/overhead-clock/actions/workflows/ci.yml)
![Status: early, untested on hardware](https://img.shields.io/badge/status-0.1%20early-orange)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

> **Status:** 0.1 builds and passes its tests, but hasn't run on a real TC001 yet. Hardware testers welcome.

**[Quick start guide (PDF)](docs/Overhead-Quick-Start.pdf)** · **[Teaser video](docs/media/overhead-teaser.mp4)**

## What it does
- **Only when it's audible.** Estimates how loud each nearby plane is at your house (distance, height, aircraft size,
  takeoff vs landing, even the seconds sound takes to reach you) and lights up only while it's clearly audible.
- **Arrivals and departures.** Landing planes show where they came from; departing planes, where they're going.
- **Four display styles,** cycled with a button: `DENVER` · `DEN > MSP` · `DELTA DEN > MSP` · `DELTA 1902 DENVER > MINNEAPOLIS A321`.
- **A real clock the rest of the time,** or dark between planes if you prefer.
- **Works anywhere.** Type your address once; the clock finds your nearest airport and time zone.
- **Airline colors,** and **rare aircraft** (747, A380, Dreamliner, military, helicopters, vintage) in sparkling gold.
- **Optional extras,** all off by default: inbound countdown, evening recap ("142 PLANES TODAY  FARTHEST TOKYO"),
  chime for rare planes, indoor/outdoor temperature, date, sunrise/sunset, and "landings over you today" from the wind.
- **A dashboard on your phone:** today's planes, the hours planes usually pass over you, airport weather.

## Get started
1. **Install the firmware** from Chrome or Edge with the clock on USB (about 5 minutes, details in the
   [quick start](docs/Overhead-Quick-Start.pdf)).
2. **Plug it in at home.** It scrolls the name and password of its setup Wi-Fi.
3. **Join that Wi-Fi on your phone,** pick your home network and type your address.
4. **Open the settings page** it shows (`http://overhead.local`, user `admin`) to pick colors and extras.

## Buttons
| Button | Press | Hold 1.5 s |
|---|---|---|
| Left | What to show for each plane: City, Codes, Airline, Full | Replay recent planes |
| Middle | Clock + planes, or planes only | Show the settings address and password |
| Right | Brightness: Auto, Low, Medium, High | |

Hold the left button while plugging the clock in to forget Wi-Fi and start setup again.

## How it works
- **Positions** come from the free [adsb.lol](https://adsb.lol) feed (about 500 requests a day per home). The clock
  predicts each plane's path between requests, polls quickly only when a plane is about to pass, slows down when the
  wind sends traffic elsewhere or in hours that are usually quiet, and goes silent during quiet hours.
- **Routes** come from community route databases (adsb.lol / VRS standing data, then adsbdb), checked against your
  airport so stale data isn't shown; unknown routes show the flight number. Near MSP you can opt in to reading the
  airport's public flight pages for exact cities (unofficial, off by default).
- **Time** comes from a configurable time server (Cloudflare's by default).
- **Loudness** uses a simple acoustic model you can tune on the settings page after listening for a few days.
- **Fail-safe:** if Wi-Fi, the internet or a service is down, it's just a clock (time kept by its battery-backed
  real-time clock), and it recovers on its own.

## Privacy and security
Outbound only: no cloud account, port forwarding, remote access or over-the-air updates. The setup hotspot and the
settings page are password protected, every connection is verified HTTPS, and your exact address never goes to the
flight-data service. Prefer not to send your address anywhere? Enter coordinates instead. Details in
[SECURITY.md](SECURITY.md).

## Acceptable use
Overhead is for personal, non-commercial use at home. Each clock uses free public services directly, within their
published limits (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). If you want to sell clocks with it, check
each provider's terms first: Open-Meteo's free tier, for one, is non-commercial. If a provider asks the project to stop
or change how it uses their service, it will.

## Building from source
See [CONTRIBUTING.md](CONTRIBUTING.md). Short version: `cd firmware && pio run -e tc001 -t upload`.

## Credits
Data: [adsb.lol](https://adsb.lol) (ODbL), VRS standing data (CC0), [adsbdb](https://www.adsbdb.com),
[OurAirports](https://ourairports.com), © [OpenStreetMap contributors](https://www.openstreetmap.org/copyright),
weather data by [Open-Meteo.com](https://open-meteo.com) (CC BY 4.0), NOAA [aviationweather.gov](https://aviationweather.gov).
This product uses the Census Bureau Data API but is not endorsed or certified by the Census Bureau.
Teaser video: footage by Joolsmagools and Sammy on [Pexels](https://www.pexels.com); music "Fluffing a Duck" by
Kevin MacLeod ([incompetech.com](https://incompetech.com)), licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
Libraries: FastLED, ArduinoJson, WiFiManager, Arduino-ESP32. Font from Adafruit GFX. TC001 pin map from AWTRIX 3.
Full list in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Not affiliated with Ulanzi, any airline or airport. For fun, not for navigation or safety.

## License
[MIT](LICENSE)
