# Security policy

## Reporting a vulnerability

Please report security problems privately through GitHub: **Security tab > Report a vulnerability** on this
repository. Don't open a public issue. You'll get a reply within a week; please allow time for a fix before
disclosing publicly.

Only the latest release gets fixes.

## Security model

The clock sits on a home network and talks to the internet, so it's built to need as little trust as possible.
This section says what it does and, just as important, what it doesn't protect against.

### Network exposure
- **Outbound only.** No port forwarding, cloud account, remote access or over-the-air updates. Changing the firmware
  needs a USB cable at the clock. Bluetooth is switched off and its memory released.
- **On the home network** it answers on port 80 (the settings page and its API, behind the admin password) and to its
  `overhead.local` name. It advertises no services.
- **Setup hotspot.** WPA2, with a random 12-character password per device that is shown only on the clock's own
  display. Its pages are limited to choosing Wi-Fi and entering an address: WiFiManager's firmware-upload, erase, info
  and exit routes are replaced with "not found", not just hidden. The hotspot shuts down once the clock is connected,
  and a clock that has been set up never reopens it on its own.
- **Wi-Fi.** Refuses open, WEP and WPA1 networks.

### Settings page
- **Password.** HTTP Digest authentication, user `admin`. The password starts as the setup password; change it on the
  page. Holding the middle button shows it on the display: physical access to the clock is treated as ownership.
- **Guessing.** After 5 wrong passwords from one device on the network, that device waits 30 seconds, doubling up to
  10 minutes. Other devices are unaffected, so nobody can lock the owner out.
- **Cross-site requests.** Changes must be JSON with a custom `X-Overhead` header, which browsers won't send from
  another website without a CORS preflight the clock never approves.
- **DNS rebinding.** Requests must be addressed to the clock by its IP address or `overhead.local`.
- **Framing and scripts.** Pages can't be framed, and a strict Content-Security-Policy is set. Device data shown on the
  page (flight numbers, cities, network names) is escaped.
- **Replay.** A Digest header only works for the path it was made for.
- **Slow clients.** The display runs in its own task, so a slow or hostile web client can delay the settings page but
  can't freeze the clock or trigger the watchdog.

### Limits of a plain-HTTP settings page
The settings page is plain HTTP on your local network (no certificates exist for `overhead.local`). Digest keeps the
password itself off the wire, but:
- A **new** password, typed into *Change password*, is sent to the clock in readable form.
- Someone who can intercept traffic on your Wi-Fi could alter pages in transit, or capture a Digest exchange and try
  to guess the password offline. A random 12-character password (the default) resists that; a short or common one
  doesn't.

So keep the clock on a network you trust (your home Wi-Fi, not a shared or public one), and if you change the admin
password, make it long.

### Outgoing connections
- **HTTPS only,** to a fixed list of services (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). Each is verified
  against pinned root certificates in `firmware/src/root_cas.h`. Redirects aren't followed.
- **Bounded responses.** Responses are parsed with filters and size, nesting and time limits. Timeouts are short
  (8 seconds) and back off when a service fails, capped at an hour even if the service asks for longer.
- **Certificate expiry.** Certificate dates aren't checked by the ESP32's TLS library; the pinned roots still decide
  who is trusted.
- **Time (NTP)** is unauthenticated. A forged time could shift the clock until the next good sync, but it isn't saved
  into the battery-backed clock unless the two roughly agree.

### Data
- **On the clock** (ESP32 flash, not encrypted): Wi-Fi password, home address and coordinates, settings and admin
  password. There's no secure boot, so anyone who takes the clock and plugs it into a computer could read them or
  reflash it. Before giving a clock away, use *Forget Wi-Fi* and reflash it.
- **Leaving the house:**

| Data | Goes to | When |
|---|---|---|
| Street address | US Census geocoder (US addresses) or OpenStreetMap Nominatim | Once, when you look it up. Nothing, if you enter coordinates instead |
| Location rounded to about 1 km | adsb.lol | With each request for nearby planes |
| Location rounded to about 1 km | Open-Meteo | Once, for elevation and time zone |
| Flight numbers | Airport flight board, adsb.lol route data, adsbdb | When a plane is about to be audible |
| Airport code | aviationweather.gov | Every 30 minutes |

  Requests identify the project (`overhead-clock` and its GitHub address), not the owner. Nothing is logged to the
  serial port.

### Fail-safe
If Wi-Fi, the internet or any service is down, the clock keeps telling time from its battery-backed real-time clock,
reconnects in the background, and retries services with growing delays. If the display ever stops updating, a
watchdog restarts the clock, which comes straight back to the time.
