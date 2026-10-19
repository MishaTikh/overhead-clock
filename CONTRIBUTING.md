# Contributing to Overhead

Thanks for helping. Bug reports, airport flight-board providers, fixes and ideas are all welcome.

## Getting set up
- **Firmware**: [PlatformIO](https://platformio.org). `cd firmware && pio run -e tc001`, then
  `pio run -e tc001 -t upload` with the clock on USB. The build embeds `ui/index.html` (the settings page).
  On Windows with a long folder path, set `PLATFORMIO_WORKSPACE_DIR` to a short path such as `C:\pio\overhead`.
- **Logic tests** run on a PC, no hardware needed:
  ```
  cd firmware
  g++ -std=c++17 -Isrc -o host_test test/host_test.cpp src/overhead.cpp src/render.cpp src/extras.cpp
  ./host_test test/fixtures/board_sample.html
  ```
- **Settings page**: open `firmware/ui/index.html` in a browser. Away from a clock it runs on sample data.
- **Generated data**: `python tools/gen_airports.py`, `python tools/gen_timezones.py` (needs `pip install tzdata`),
  `python tools/gen_root_cas.py firmware/src/root_cas.h` (needs `pip install certifi`).

## Code layout
| Path | What |
|---|---|
| `firmware/src/overhead.*` | Geometry, loudness model, tracking, routes, display formats (no Arduino code; host-tested) |
| `firmware/src/extras.*` | Rare aircraft, daily stats, runway flow, sunrise/sunset (host-tested) |
| `firmware/src/render.*` | Font, scrolling text, clock face (host-tested) |
| `firmware/src/main.cpp` | Wi-Fi, HTTPS requests, settings API, buttons, LEDs, sensors |
| `firmware/src/settings.*` | Saved settings and their JSON shape |
| `firmware/ui/index.html` | Settings page and dashboard |
| `prototype/` | The original Python desktop/Raspberry Pi prototype (MSP-specific simulator) |

## Adding a flight board for your airport
Route databases are often stale for reused flight numbers; an airport's own flight board is far more accurate.
See `lookup_board()` in `main.cpp` and `BoardParser` / `pick_board_row()` in `overhead.cpp`. A provider should make
one request per audible flight, read only what it needs, and add its host's root certificate if it isn't already in
`root_cas.h`. Add a fixture under `firmware/test/fixtures/` and a test.

## Ground rules
- Keep the clock outbound-only and least-privilege (read [SECURITY.md](SECURITY.md)). New network calls need a good
  reason, HTTPS with the pinned roots, size limits, and must respect the service's terms and rate limits.
- Anything new is optional and off by default unless it's core to showing planes.
- The clock must keep telling time when anything else fails.
- Never put personal data (addresses, coordinates, Wi-Fi names) in code, tests, issues or screenshots.
- Match the existing style; keep comments short and about why.
- By contributing you agree your work is released under the MIT license.
