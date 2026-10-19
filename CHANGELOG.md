# Changelog

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
