// Platform-independent core: geometry, loudness model, tracking, route bookkeeping, nearest
// airport and the airport flight-board parser. No Arduino headers here, so it also builds on a PC for tests.
#pragma once
#include <stdint.h>
#include <time.h>

#include "compat.h"
#include <string>
#include <vector>

#include "config.h"
#include "extras.h"

namespace ov {

// Everything the owner can change from the settings page. Saved in flash (see settings.cpp).
struct Settings {
    std::string address;                      // as typed; empty = location not set yet
    double home_lat = 0, home_lon = 0;        // from the address lookup (or typed in)
    float home_elev = 900;                    // ft MSL, from the address lookup
    std::string airport_choice;               // IATA code chosen on the settings page; empty = nearest
    std::string airport_iata, airport_city;   // the airport in use (resolved from the choice or nearest)
    double airport_lat = 0, airport_lon = 0;
    std::string tz_name = "UTC";              // IANA name, e.g. America/Chicago
    std::string tz_posix = "UTC0";            // POSIX rule the ESP32 uses
    int brightness = 0;                       // 0 auto (light sensor), 1 low, 2 medium, 3 high
    int display_format = 0;                   // see format_flight(); cycled with the left button
    // optional extras (all off by default except rare-aircraft colors)
    bool rare_alerts = true;                  // gold text + sparkle for 747s, A380s, military, helicopters...
    bool rare_chime = false;                  // and a short chime on the buzzer
    bool inbound_countdown = false;           // "INBOUND 12" with a plane icon before a plane is audible
    bool recap = false;                       // evening recap of the day's planes
    int recap_hour = 20;
    int rotation = 0;                         // ROT_* bits: extra screens shown between clock minutes
    int rotate_every_s = 60;
    bool units_f = true;                      // Fahrenheit (else Celsius)
    float temp_offset_c = -4.0f;              // the TC001 sensor reads warm from its own electronics
    bool smart_polling = true;
    bool airport_board = false;               // opt-in: MSP's public flight pages for exact cities (unofficial)
    std::string ntp_server = "time.cloudflare.com";  // never the public pool or NIST: both forbid hard-coding                // poll less when the wind sends planes elsewhere / in quiet hours learned
    uint32_t clock_rgb = 0xFFAA6E;            // warm white
    uint32_t plane_rgb = 0xFF7800;            // used when airline colors are off, or airline unknown
    int bright_min = BRIGHT_MIN, bright_max = BRIGHT_MAX;
    float scroll_speed = SCROLL_PX_PER_S;
    int daily_budget = DAILY_BUDGET;
    float on_db = ON_DB, off_db = OFF_DB, cal_offset = CAL_OFFSET_DB;
    int quiet_start = QUIET_START_H, quiet_end = QUIET_END_H;
    int color_by_airline = COLOR_BY_AIRLINE;
    int mode = MODE, clock_24h = CLOCK_24H;  // mode: 1 clock + planes, 0 planes only
    // the look
    bool jet_banner = true;                   // a little jet tows the city onto the sign
    bool breathe = false;                     // optional, planes-only mode: the city brightens with loudness
    bool milestones = true;                   // FIRST PLANE TODAY, 100 PLANES TODAY, NEW RECORD ...
    bool warm_night = true;                   // the clock turns a warmer amber between sunset and sunrise
};
extern Settings S;
enum { ROT_INDOOR = 1, ROT_OUTDOOR = 2, ROT_DATE = 4, ROT_SUN = 8, ROT_FLOW = 16, ROT_TODAY = 32 };
inline bool location_set() { return S.home_lat != 0 || S.home_lon != 0; }

// ---------------------------------------------------------------- airports and time zones
struct NearbyAirport { std::string iata, city; double lat, lon, dist_nm; bool large; };
// Airports with scheduled service, nearest first (up to n, within max_nm).
std::vector<NearbyAirport> nearest_airports(double lat, double lon, int n, double max_nm);
// Sets S.airport_* from S.airport_choice (or the nearest airport). Returns false if none in range.
bool resolve_airport();
// IANA name -> POSIX TZ rule; nullptr if unknown.
const char* posix_for_zone(const std::string& iana);

// ---------------------------------------------------------------- geometry
void dist_bearing_ft(double lat1, double lon1, double lat2, double lon2, double& dist, double& brg);
void move(double lat, double lon, double brg_deg, double dist_ft, double& olat, double& olon);
float level_at(const char* category, bool departing, float horiz_ft, float height_ft);

// ---------------------------------------------------------------- routes
struct Route {
    std::string from_city, from_iata, to_city, to_iata, mkt;  // mkt = marketing airline code, e.g. "DL"
};
enum class RouteState { None, Pending, Found, NotFound };

struct RouteEntry {
    std::string cs;
    bool departing = false;
    RouteState state = RouteState::None;
    Route route;
    double t = 0;
};

class RouteCache {
public:
    // Returns the entry, creating a Pending one (and marking it for lookup) if absent/stale.
    RouteEntry* get(const std::string& cs, bool departing, double now);
    // Next entry waiting for the network task, or nullptr.
    RouteEntry* next_request();
    void complete(const std::string& cs, bool departing, bool found, const Route& r, double now);
private:
    std::vector<RouteEntry> entries_;
    std::vector<size_t> queue_;
};

std::string clean_city(const std::string& raw);
const char* marketing_codes(const std::string& op);   // "DAL" -> "DL", "SKW" -> "DL,UA,AA,AS"
bool split_callsign(const std::string& cs, std::string& op, std::string& num);

// Flight board HTML (mspairport.com) — streamed one character at a time, keeps only table rows.
class BoardParser {
public:
    void reset();
    void feed(char c);
    bool done() const { return done_; }
    std::vector<std::vector<std::string>> rows;
private:
    bool in_tbody_ = false, in_td_ = false, in_tag_ = false, done_ = false;
    std::string tag_, cell_;
    std::vector<std::string> row_;
};
// Choose the row for this flight closest to now. Returns true and fills r.
bool pick_board_row(const std::vector<std::vector<std::string>>& rows, const std::string& cs,
                    bool departing, const struct tm& now_local, Route& r);
// Route databases: list of (iata, city) legs -> the leg touching our airport.
bool pick_leg(const std::vector<std::pair<std::string, std::string>>& airports, bool departing, Route& r);

// ---------------------------------------------------------------- tracking
struct Fix {
    uint32_t hex = 0;
    char cs[9] = {0}, type[5] = {0}, cat[3] = {0};
    double lat = 0, lon = 0;
    float alt = 0, gs = 0, trk = 0, vrate = 0, seen_pos = 0;
    int db_flags = 0;             // readsb dbFlags: bit 0 = military
};

struct Track {
    bool used = false;
    Fix f;
    double t_fix = 0;
    float level = -99, horiz_ft = 0, agl_ft = 0;
    bool departing = false;
    bool in_pass = false;
    double pass_start = 0;
    float pass_peak = -99, pass_min_slant = 1e9f, pass_agl = 0;
    std::string rare;             // rare_label() of this aircraft, "" if ordinary
    void predict(double t, double& lat, double& lon, float& alt) const;
};

struct Pass {
    time_t start; char cs[9]; char type[5]; char city[28]; char other_iata[4]; char rare[12];
    float peak; int agl; float secs; bool dep;
};

struct Message {
    bool active = false;
    std::string text;
    uint8_t r = 255, g = 120, b = 0;
    float level = 0;
    uint32_t hex = 0;
    double since = 0;
    bool route_final = false;
    bool rare = false;            // show with the rare-aircraft treatment
};

const int MAX_TRACKS = 64;
const int MAX_PASSES = 12;

class Tracker {
public:
    Track tracks[MAX_TRACKS];
    Message msg;
    Pass passes[MAX_PASSES];
    int n_passes = 0;
    RouteCache routes;
    DayStats stats;
    float inbound_s = -1;         // seconds until the next plane becomes audible (<= 30), or -1
    std::string inbound_cs;

    void ingest(const std::vector<Fix>& fixes, double fetch_time);
    void tick(double now);
    double want_poll_at(double now);
    // Recompute inbound_s (call about once a second).
    void update_inbound(double now);
    void clear();
    int count() const;
private:
    bool is_departing(const Track& t) const;
    bool will_pass_over(const Track& t, double now) const;
    void heard_level(Track& t, double now, float& slant);
    void choose(double now);
    void fill_text(Message& m, Track& t, double now);
    void end_pass(Track& t, double now);
};

// What the sign says about a flight. Formats (cycled with the left button):
//   0 CITY     "DENVER"            origin if landing, destination if taking off
//   1 CODES    "DEN > MSP"
//   2 AIRLINE  "DELTA DEN > MSP"
//   3 FULL     "DELTA 1902  DENVER > MINNEAPOLIS  A321"
// Without a known route every format shows the flight number.
const int DISPLAY_FORMATS = 4;
std::string format_flight(int fmt, const Route* r, bool departing, const char* cs, const char* type);
const char* format_name(int fmt);
std::string airline_name(const std::string& cs, const std::string& mkt);

void airline_color(const std::string& mkt, const std::string& cs, uint8_t& r, uint8_t& g, uint8_t& b);
bool is_quiet(int hour);

}  // namespace ov
