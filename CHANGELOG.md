# Changelog

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
