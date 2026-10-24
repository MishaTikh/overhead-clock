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
- **Outbound only.** No port forwarding, cloud account or remote access. Bluetooth is switched off and its memory
  released.
- **Firmware updates** go over the home network to `/api/update`, with the same protections as any other change:
  admin password, the custom header, the Host check and the per-device lockout. On top of that the sender puts the
  image's SHA-256 in the request's URI, which the Digest sign-in signs, and the clock hashes what it receives and
  refuses an image that doesn't match: someone intercepting traffic on the Wi-Fi can't swap the firmware in transit.
  The image must also be a valid ESP32 application whose checksum verifies, or the clock keeps its current firmware.
  Anyone who has the admin password can install any firmware, so treat it like the device's root password. (USB
  flashing of the TC001 doesn't work reliably from Windows, so updates over Wi-Fi are what make the clock
  maintainable.)
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
- **Framing and scripts.** Pages can't be framed, and a strict Content-Security-Policy allows only the settings page's
  own script, identified by its hash: an injected script could never run. Device data shown on the page (flight
  numbers, cities, network names) is escaped as well.
- **Replay.** A Digest header only works for the path it was made for, and only once: browsers number their requests
  per challenge, and each number is accepted once (a window of the last 64 lets requests sent together arrive in any
  order). So a captured header can't be replayed to restart the clock, forget its Wi-Fi or change a setting.
- **Remember this phone (optional, off until you turn it on).** A random token goes into a cookie that's HttpOnly,
  `SameSite=Strict` and lasts 30 days; the clock stores only its SHA-256, for up to four phones. Changes still need the
  custom header, so other websites can't use the cookie. The trade-off: the page is plain HTTP, so someone who can read
  your Wi-Fi traffic could copy the cookie and use the settings page until it expires. That's why **firmware updates
  always ask for the password**, even on a remembered phone. *Sign out all phones* and changing the password both
  cancel every remembered phone at once.
- **Big requests.** Request bodies are streamed with a 2 KB cap (the firmware image excepted); a client that hasn't
  signed in can't fill the clock's memory with a huge POST.
- **Slow clients.** The display runs in its own task, so a slow or hostile web client can delay the settings page but
  can't freeze the clock or trigger the watchdog.

### Limits of a plain-HTTP settings page
The settings page is plain HTTP on your local network (no certificates exist for `overhead.local`). Digest keeps the
password itself off the wire, but:
- A **new** password, typed into *Change password*, is sent to the clock in readable form.
- Someone who can intercept traffic on your Wi-Fi could alter pages in transit, or capture a Digest exchange and try
  to guess the password offline. A random 12-character password (the default) resists that; a short or common one
  doesn't.
- The web server library reads request headers into memory without a size limit, so a device on your network could
  make the clock run short of memory for a moment, or restart it, by sending an enormous request. It can't get in that
  way; the clock just comes back to the time.

So keep the clock on a network you trust (your home Wi-Fi, not a shared or public one), and if you change the admin
password, make it long.

### Outgoing connections
- **HTTPS only,** to a fixed list of services (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). Each is verified
  against pinned root certificates in `firmware/src/root_cas.h`. Redirects aren't followed.
- **Bounded responses.** Responses are parsed with filters and nesting limits, and read through a reader with a size
  cap (64 KB; 256 KB for the aircraft list) and a 15-second deadline, so a service that trickles or never stops can't
  hold the clock's network task. Timeouts are short (8 seconds) and back off when a service fails, capped at an hour
  even if the service asks for longer.
- **Certificate expiry.** Certificate dates aren't checked by the ESP32's TLS library; the pinned roots still decide
  who is trusted.
- **Time (NTP)** comes from one server you can change on the settings page (Cloudflare's by default) and is unauthenticated. A forged time could shift the clock until the next good sync, but it isn't saved
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
| Flight numbers | adsb.lol route data, adsbdb, and mspairport.com if you opt in | When a plane is about to be audible |
| Airport code | aviationweather.gov | Every 30 minutes |

  Requests identify the project (`overhead-clock` and its GitHub address), not the owner. Nothing is logged to the
  serial port.

### Fail-safe
If Wi-Fi, the internet or any service is down, the clock keeps telling time from its battery-backed real-time clock,
reconnects in the background, and retries services with growing delays. If the display ever stops updating, a
watchdog restarts the clock, which comes straight back to the time.
