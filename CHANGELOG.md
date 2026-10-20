# Changelog

## 0.1.7
- Smooth hand-offs: a plane's text scrolls in and pushes the time out, then scrolls on without stopping; when the
  plane has passed, the time scrolls back in. Button notices still switch instantly.
- Steady 40 frames a second, so scrolling glides evenly.

## 0.1.6
- The city shows straight away: a plane's route is looked up as soon as it's headed over the house (up to 10 minutes
  out), not when it's almost audible. If a lookup is still running, the sign waits up to 4 seconds for it rather than
  show the callsign first.
- The dashboard shows how long the clock has been running and why it last restarted.
- Today's planes and stats survive a restart (saved at most every 2 minutes, only when they change).

## 0.1.5
- Fixed: no flight data. adsb.lol now refuses HTTP/1.0 requests (426 Upgrade Required); the clock asks it over
  HTTP/1.1 and handles chunked replies.
- The dashboard shows the last problem in plain text, and the clock's free memory.

## 0.1.4
- Fixed: a short press on any button restarted the clock (saving the setting called itself until the
  stack ran out).

## 0.1.3
- Button holds act while the button is still held, so the display shows when to let go. The middle button opens the
  settings address and password after 3 seconds; the left button replays recent planes after 1.5 seconds.

## 0.1.2
- The password is shown four characters at a time, each block standing still with a progress mark; the left and right
  buttons page back and forward, the middle button closes. Same for the setup hotspot's Wi-Fi password.

## 0.1.1
- Installs and updates over Wi-Fi: password-protected *Update firmware* on the settings page, a one-click Windows
  installer (`installer/Install-Overhead.bat`) that also handles clocks still on Ulanzi's firmware, and two firmware
  slots so an update never leaves the clock without working firmware.
- The settings address and password scroll slowly, twice, with the password in groups of four.
- Tested on a real TC001: install, first boot, sign-in and an update over Wi-Fi.

## 0.1.0 (unreleased, not yet tested on hardware)
- First version of the Overhead firmware for the Ulanzi TC001.
- Shows the flight over the house only while it's clearly audible (loudness model with sound travel time).
- Display formats: city, codes, airline + codes, full. Landing planes show where they came from; departing planes
  where they're going.
- Clock + planes and planes-only modes; three-button control; brightness levels.
- Setup from a phone: password-protected hotspot, street address or coordinates, nearest airport, time zone.
- Settings page and dashboard: today's planes, usual busy hours, airport weather and runway flow.
- Optional extras: rare aircraft alerts (gold + sparkle, optional chime), inbound countdown, evening recap,
  indoor/outdoor temperature, date, sunrise/sunset.
- Request saving: wind-aware and learned-hours polling, route caching, backoff.
- Fail-safe clock: battery-backed time, background Wi-Fi reconnect, watchdog.
