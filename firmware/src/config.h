// Overhead Clock for the Ulanzi TC001: compile-time defaults.
// Settings marked [web] are changed on the clock's settings page and kept in flash; the values
// here are only the defaults for a new clock. Location, airport and time zone come from the
// address entered during setup, so nothing in this file is specific to one place.
#pragma once

#define FW_VERSION      "0.1.8"
#define PROJECT_URL     "https://github.com/MishaTikh/overhead-clock"
#define HOSTNAME        "overhead"           // http://overhead.local
#define MAX_AIRPORT_NM  60.0                 // nearest-airport search radius

// ---------------------------------------------------------------- adsb.lol budget
#define FEED_RADIUS_NM      25
#define DAILY_BUDGET        450       // adsb.lol allows ~500/day per IP
#define BUCKET_CAPACITY     30
#define HOT_INTERVAL_S      15        // fastest polling, when a plane is about to pass
#define IDLE_INTERVAL_S     240       // when nothing is near
#define MAX_EXTRAPOLATE_S   150       // forget planes not refreshed in this long
#define QUIET_START_H       23        // [web] no plane alerts or data requests from 23:00...
#define QUIET_END_H         6         // [web] ...until 06:00 (set both equal to disable)

// ---------------------------------------------------------------- acoustics
// A-weighted level (dBA) at 1000 ft slant range, approach configuration, by ADS-B category.
#define SRC_A1  70.0f   // light piston
#define SRC_A2  78.0f   // small jets / turboprops
#define SRC_A3  82.0f   // A320 / 737 / E175 / CRJ900
#define SRC_A4  85.0f   // 757
#define SRC_A5  88.0f   // heavies
#define SRC_A7  82.0f   // helicopters
#define SRC_UNK 80.0f
#define DEPARTURE_BOOST_DB   8.0f
#define ABSORB_DB_PER_KFT    1.2f
#define GROUND_MAX_DB        10.0f
#define GROUND_FULL_DEG      5.0f
#define GROUND_ZERO_DEG      40.0f
#define SPEED_OF_SOUND_FPS   1125.0f
#define ON_DB                60.0f    // [web] "clearly audible" (quiet neighborhood ~45 dBA)
#define OFF_DB               57.0f    // [web] hysteresis
#define CAL_OFFSET_DB        0.0f     // [web] nudge after listening
#define MIN_SHOW_S           6.0f
#define ROUTE_WAIT_S         4.0       // wait this long for a route lookup before showing the callsign

// ---------------------------------------------------------------- display
#define MODE                 1        // [web] 1 clock (interrupted by planes), 0 planes only (dark otherwise)
#define CLOCK_24H            0        // [web] 0 "9:45", 1 "21:45"
#define SCROLL_PX_PER_S      20.0f    // [web]
#define SCROLL_HOLD_S        1.2f     // pause on the first letters before scrolling
#define FRAME_MS             25       // display frame period (40 fps)
#define COLOR_BY_AIRLINE     1        // [web] 1 airline colors, 0 one color for all planes
#define MAX_MILLIAMPS        900
#define BRIGHT_MIN           6        // [web] auto brightness in a dark room (0-255)
#define BRIGHT_MAX           90       // [web] and in daylight (255 is blinding)

// ---------------------------------------------------------------- TC001 hardware (AWTRIX 3 pin map)
#define PIN_MATRIX      32
#define PIN_BTN_LEFT    26
#define PIN_BTN_MID     27
#define PIN_BTN_RIGHT   14
#define HOLD_LEFT_MS    1500    // left hold: replay recent planes
#define HOLD_MID_MS     3000    // middle hold: settings address + password
#define PIN_BUZZER      15
#define PIN_LDR         35
#define MATRIX_W        32
#define MATRIX_H        8
// Pixel order. TC001: rows, zigzag, starting top-left. If the boot test looks
// mirrored or scrambled, try the other values (see README).
#define MATRIX_SERPENTINE 1
#define MATRIX_FLIP_X     0
#define MATRIX_FLIP_Y     0
