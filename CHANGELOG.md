# Changelog

## 0.2.2
- A map on the settings page: every city a plane over the house came from (or was going to), with an arc to your
  house. Orange is today, grey is all time (up to 250 cities, kept across restarts). Drawn by the phone from a tiny
  coastline outline built into the page: no internet tiles, nothing sent anywhere.

## 0.2.1
- Installer with a window: finds the clock, shows what it runs (version, uptime, last restart) and what the file
  contains (version, size, SHA-256), warns before installing the same or an older version, and confirms the clock
  came back on the new one.
- Loudness by aircraft type when the feed gives no size category (a 777 sounds like a heavy, a Cessna like a light
  piston), instead of one generic level.
- Barometric-only altitudes are corrected with the airport's altimeter setting from the METAR.
- Route lookups start earlier and wider (15 minutes out, 3 nm either side), and the sign waits at most 2.5 s for one.
- The dashboard's recent-planes table shows how long each plane waited for its route before it was shown.

## 0.2.0
A pleasure to watch:
- A little jet with contrails tows each plane's city onto the sign, pushing the time out.
- When the plane has passed, the name finishes scrolling (never cut off mid-word), then the time follows it back in.
- Optional, off by default: in planes-only mode the city glows brighter as the plane gets louder and fades as it
  leaves (More settings, The look).
- Milestones in gold: the first plane of the day, every hundredth, and a new farthest-ever city.
- A warmer amber clock between sunset and sunrise, and a colon that breathes instead of blinking.
- The clock's own screens (temperature, date, recap) glide in and out; button feedback stays instant.

Easy to use:
- The settings page shows a live copy of the sign: the clock's real pixels, five times a second.
- Changing the style, colors, jet, breathing or scroll speed flies a sample plane across the clock.
- Remember this phone for 30 days (optional), with sign-out for one or all phones. Firmware updates still ask
  for the password.
- The page opens with the everyday settings (mode, what to show, brightness, colors); everything else is under
  More settings.
- The sign-in replay check now lets requests sent together arrive in any order.

## 0.1.8
Security hardening, after a review of the settings page, the update path and the internet fetches
(see SECURITY.md). Updates from this version on need this version's settings page or installer.
- Firmware updates carry the image's SHA-256 inside the Digest-signed URI; the clock refuses an image that doesn't
  match it, so nothing on the network can swap the firmware in transit.
- A captured sign-in header can't be replayed: requests are numbered per challenge and repeats are refused.
- Request bodies are streamed with a 2 KB cap instead of buffered whole, so an unsigned-in client can't fill memory.
- The settings page's Content-Security-Policy allows only its own script, by hash.
- Every internet response is read with a size cap and a deadline.
- A wrong password on the update endpoint counts toward the lockout and the upload is dropped at once.
- Fixed: a multipart form POST to the update endpoint restarted the clock, without a password.

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
