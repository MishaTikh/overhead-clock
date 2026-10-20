// Overhead Clock — firmware for the Ulanzi TC001 (ESP32 + 32x8 RGB LED matrix).
// Shows where the plane over your house came from (or is going), only while you can clearly
// hear it, and in clock mode the time the rest of the day.
//
// Core 0: network task (flight positions on a daily request budget, route and address lookups).
// Core 1: Arduino loop (tracking at 30 fps, LED matrix, buttons, light sensor, settings page).
//
// Security model (see SECURITY.md)
//  - Setup hotspot is WPA2 with a random per-device password that only the clock's own display shows.
//    The setup portal offers Wi-Fi + address only; its firmware-upload, info and erase pages are disabled.
//  - Settings page and API need the admin password (HTTP Digest, never sent in readable form),
//    with a lockout after repeated wrong passwords. The admin password starts as the setup password.
//  - Changes require a JSON POST with a custom header, which browsers won't send cross-site.
//  - Every outgoing connection is HTTPS to a fixed list of services, verified against pinned root
//    certificates (root_cas.h). Home coordinates are rounded to ~1 km for flight-data requests.
//  - Outbound only: no port forwarding, cloud account or remote access. Firmware updates are accepted
//    only from the home network, behind the admin password and the same request checks as settings,
//    and must be a valid, checksummed ESP32 image. Bluetooth is switched off and its memory released.
//
// Fail-safe: whatever fails (Wi-Fi, internet, any service), the clock keeps telling the time.
//  - A clock that has been set up never blocks on the setup hotspot; it shows the time and reconnects.
//  - Time comes from the TC001's battery-backed real-time clock, re-synced from the internet when possible.
//  - Services that fail are retried with growing delays; a watchdog restarts a hung clock.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <esp_bt.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <Preferences.h>
#include <sys/time.h>

#include "config.h"
#include "extras.h"
#include "overhead.h"
#include "render.h"
#include "root_cas.h"
#include "settings.h"
#include "ui_html.h"

using namespace ov;

static CRGB leds[MATRIX_W * MATRIX_H];
static Tracker tracker;
static Marquee marquee;
static SemaphoreHandle_t lock;  // recursive: guards tracker, settings S, and every String shared across tasks
#define LOCK() xSemaphoreTakeRecursive(lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGiveRecursive(lock)
static WebServer web(80);
static String admin_pw;

// status shown on the settings page
static volatile float tokens = BUCKET_CAPACITY;
static volatile int calls_today = 0;
static volatile double last_poll = 0, next_poll = 0;
static String last_error;
static volatile bool quiet_now = false;

// airport weather and the learned busy hours
static Weather weather;
static FlowInfo flow;
static HourModel hour_model;
static double feed_backoff_until = 0, feed_backoff_s = 0;  // after errors from the flight-data service

// sensors
static bool have_rtc = false, have_sht = false;
static float indoor_c = NAN, indoor_rh = NAN;

// address lookup, done by the network task
enum class GeoState { Idle, Pending, Ok, Failed };
static volatile GeoState geo_state = GeoState::Idle;
static String geo_request, geo_message;
static double geo_lat = 0, geo_lon = 0;  // used instead of an address when coordinates are typed in

// a temporary override of the sign (buttons, preview, boot messages)
static std::string override_text;
static uint32_t override_rgb = 0xFF7800;
static double override_until = 0;
static float override_speed = 0;  // 0 = the scroll-speed setting

static void display_task(void*);

static double now_s() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1e6;
}
static bool time_valid() { return time(nullptr) > 1700000000; }
static CRGB rgb(uint32_t c) { return CRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

static void show_override(const std::string& text, double secs, uint32_t color = 0xFF7800, float speed = 0) {
    LOCK();
    override_speed = speed;
    override_text = text;
    override_rgb = color;
    override_until = now_s() + secs;
    UNLOCK();
}

// Credentials are shown slowly and in still blocks: easy to read off the sign.
static const float CREDENTIAL_SPEED = 10.0f;  // px/s, about half the usual scroll speed

// The address scrolls slowly, then the password is shown four characters at a time, each block
// standing still for a few seconds with a progress mark on the bottom row. Left/right step through.
static SignSequence credential_seq;
static uint32_t credential_rgb = 0x00B4FF;

static std::vector<SignStep> credential_steps(const std::string& header, const String& pw) {
    std::vector<SignStep> steps;
    Marquee m;
    m.set_text(header, 0, CREDENTIAL_SPEED);
    steps.push_back({header, m.seconds_for(1), false, CREDENTIAL_SPEED});
    m.set_text("PASSWORD", 0, CREDENTIAL_SPEED);
    steps.push_back({"PASSWORD", m.seconds_for(1), false, CREDENTIAL_SPEED});
    int pages = (pw.length() + 3) / 4;
    for (int p = 0; p < pages; p++)
        steps.push_back({pw.substring(p * 4, p * 4 + 4).c_str(), 3.0, true, 0, p + 1, pages});
    return steps;
}

static void show_credentials() {
    LOCK();
    std::string header = ("SETTINGS " + WiFi.localIP().toString() + "   USER ADMIN").c_str();
    String pw = admin_pw;
    override_until = 0;
    credential_seq.start(credential_steps(header, pw), now_s(), 2, 2);  // password pages shown 3 times
    UNLOCK();
}

static void apply_timezone() {
    LOCK();
    setenv("TZ", S.tz_posix.c_str(), 1);
    tzset();
    UNLOCK();
}

// One configurable time server. Not the public NTP pool (products must use their own vendor zone)
// and not time.nist.gov (NIST asks devices not to hard-code it).
static String ntp_host;  // must outlive the SNTP client, which keeps the pointer
static void start_time_sync() {
    LOCK();
    ntp_host = S.ntp_server.c_str();
    String tz = S.tz_posix.c_str();
    UNLOCK();
    configTzTime(tz.c_str(), ntp_host.c_str());
}

static void set_error(const String& e) {
    LOCK();
    last_error = e;
    UNLOCK();
}

static void save_settings_locked() {
    LOCK();
    settings_save();
    UNLOCK();
}

// ================================================================ LED output
static uint16_t xy(int x, int y) {
    if (MATRIX_FLIP_X) x = MATRIX_W - 1 - x;
    if (MATRIX_FLIP_Y) y = MATRIX_H - 1 - y;
    if (MATRIX_SERPENTINE && (y & 1)) return y * MATRIX_W + (MATRIX_W - 1 - x);
    return y * MATRIX_W + x;
}

static void draw_columns(const uint8_t* cols, CRGB color) {
    fill_solid(leds, MATRIX_W * MATRIX_H, CRGB::Black);
    for (int x = 0; x < MATRIX_W; x++)
        for (int y = 0; y < MATRIX_H; y++)
            if (cols[x] & (1 << y)) leds[xy(x, y)] = color;
}

static void draw_static(const char* text, CRGB color) {
    std::vector<uint8_t> c = text_columns(text);
    uint8_t cols[MATRIX_W] = {0};
    int pad = (int)c.size() < MATRIX_W ? (MATRIX_W - (int)c.size()) / 2 : 0;
    for (size_t i = 0; i < c.size() && pad + (int)i < MATRIX_W; i++) cols[pad + i] = c[i];
    draw_columns(cols, color);
    FastLED.setBrightness(30);
    FastLED.show();
}

// ================================================================ network
// Identifies the project and version to every service, as Nominatim and aviationweather.gov ask.
static const char* UA = "overhead-clock/" FW_VERSION " (+" PROJECT_URL ")";

// HTTPS GET with certificate verification and no redirects. Caller reads the stream, then http.end().
// Timeouts stay well under the 15 s watchdog: stream reads busy-wait on core 0.
// http11: for services that refuse HTTP/1.0 (adsb.lol answers 426); read the body through BodyReader.
static int https_get(HTTPClient& http, WiFiClientSecure& client, const String& url, uint32_t timeout_ms = 8000,
                     bool http11 = false) {
    client.setCACert(ROOT_CAS);
    client.setHandshakeTimeout(8);
    http.useHTTP10(!http11);  // HTTP/1.0: no chunked encoding, so the stream can be parsed directly
    http.setTimeout(timeout_ms);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(client, url)) return -1;
    http.setUserAgent(UA);
    return http.GET();
}

// Reads an HTTP/1.1 body for ArduinoJson, undoing chunked transfer encoding when the server uses it.
class BodyReader {
public:
    BodyReader(Stream& s, bool chunked) : s_(s), chunked_(chunked) {}
    int read() {
        if (!chunked_) return raw();
        if (done_) return -1;
        if (left_ == 0) {  // chunk header: hex size, optional ";extension", CRLF
            long size = 0;
            bool digits = true;
            for (;;) {
                int c = raw();
                if (c < 0) { done_ = true; return -1; }
                if (c == '\n') break;
                if (c == ';' || c == '\r') digits = false;
                if (!digits) continue;
                int v = isdigit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
                if (v < 0 || size > 0x7FFFFF) { done_ = true; return -1; }
                size = size * 16 + v;
            }
            if (size == 0) { done_ = true; return -1; }
            left_ = size;
        }
        int c = raw();
        if (c < 0) { done_ = true; return -1; }
        if (--left_ == 0) { raw(); raw(); }  // CRLF after the chunk
        return c;
    }
    size_t readBytes(char* buf, size_t n) {
        size_t i = 0;
        for (; i < n; i++) {
            int c = read();
            if (c < 0) break;
            buf[i] = (char)c;
        }
        return i;
    }
private:
    int raw() { char c; return s_.readBytes(&c, 1) == 1 ? (uint8_t)c : -1; }  // waits up to the stream timeout
    Stream& s_;
    bool chunked_, done_ = false;
    long left_ = 0;
};

static bool fetch_aircraft(double home_lat, double home_lon, std::vector<Fix>& out) {
    WiFiClientSecure client;
    HTTPClient http;
    char url[96];
    // centre rounded to 0.01 deg (~1 km): plenty for a 25 nm search, and keeps the exact house private
    snprintf(url, sizeof(url), "https://api.adsb.lol/v2/point/%.2f/%.2f/%d", home_lat, home_lon, FEED_RADIUS_NM);
    static const char* keep[] = {"Retry-After", "Transfer-Encoding"};
    http.collectHeaders(keep, 2);
    int code = https_get(http, client, url, 8000, true);
    if (code != 200) {
        set_error(code < 0 ? "flight data: connection or certificate error" : String("flight data: HTTP ") + code);
        // back off: honour Retry-After, otherwise double the wait (1 min .. 30 min)
        int retry = http.header("Retry-After").toInt();
        feed_backoff_s = retry > 0 ? fmin(retry, 3600.0) : fmin(1800.0, fmax(60.0, feed_backoff_s * 2));
        feed_backoff_until = now_s() + feed_backoff_s;
        http.end();
        return false;
    }
    feed_backoff_s = 0;
    JsonDocument filter;
    for (const char* k : {"hex", "flight", "t", "category", "lat", "lon", "alt_baro", "alt_geom",
                          "gs", "track", "baro_rate", "geom_rate", "seen_pos", "dbFlags"})
        filter["ac"][0][k] = true;
    JsonDocument doc;
    BodyReader body(http.getStream(), http.header("Transfer-Encoding").equalsIgnoreCase("chunked"));
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter),
                                               DeserializationOption::NestingLimit(6));
    http.end();
    if (err) {
        set_error(String("flight data: ") + err.c_str());
        return false;
    }
    for (JsonObject a : doc["ac"].as<JsonArray>()) {
        if (!a["lat"].is<float>()) continue;
        float alt;
        if (a["alt_geom"].is<float>()) alt = a["alt_geom"];
        else if (a["alt_baro"].is<float>()) alt = a["alt_baro"];
        else continue;  // "ground" or missing
        Fix f;
        const char* hex = a["hex"] | "";
        f.hex = hex[0] == '~' ? (0x1000000u | strtoul(hex + 1, nullptr, 16)) : strtoul(hex, nullptr, 16);
        const char* cs = a["flight"] | "";
        int n = 0;
        for (const char* p = cs; *p && n < 8; p++)
            if (isalnum((unsigned char)*p)) f.cs[n++] = *p;
        strncpy(f.type, a["t"] | "", 4);
        strncpy(f.cat, a["category"] | "", 2);
        f.lat = a["lat"];
        f.lon = a["lon"];
        f.alt = alt;
        f.gs = a["gs"] | 0.0f;
        f.trk = a["track"] | 0.0f;
        f.vrate = a["geom_rate"].is<float>() ? a["geom_rate"].as<float>() : (a["baro_rate"] | 0.0f);
        f.seen_pos = a["seen_pos"] | 0.0f;
        f.db_flags = a["dbFlags"] | 0;
        out.push_back(f);
    }
    set_error("");
    return true;
}

// Airport flight-board providers: the most reliable route, from the airport's own schedule.
// Only MSP so far (mspairport.com); contributions for other airports go here.
// Opt-in and unofficial: MSP's site terms allow viewing for personal use and don't address automated
// access, so this is off by default, limited to one request per audible flight and 300 a day.
static int board_calls_today = 0;
static bool lookup_board(bool enabled, const std::string& airport, const std::string& cs, bool departing, Route& r) {
    std::string op, num;
    if (!enabled || airport != "MSP" || board_calls_today >= 300 || !split_callsign(cs, op, num) || !marketing_codes(op))
        return false;
    board_calls_today++;
    WiFiClientSecure client;
    HTTPClient http;
    String url = String("https://www.mspairport.com/flights-and-airlines/flights?flight_type=") +
                 (departing ? "departure" : "arrival") + "&text=" + num.c_str();
    if (https_get(http, client, url, 8000) != 200) { http.end(); return false; }
    BoardParser parser;
    parser.reset();
    WiFiClient* s = http.getStreamPtr();
    uint8_t buf[512];
    uint32_t t0 = millis(), total = 0;
    while (!parser.done() && millis() - t0 < 10000 && total < 600000 && (s->connected() || s->available())) {
        int n = s->read(buf, sizeof(buf));
        if (n <= 0) { delay(5); continue; }
        total += n;
        for (int i = 0; i < n; i++) parser.feed((char)buf[i]);
    }
    http.end();
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    LOCK();
    bool ok = pick_board_row(parser.rows, cs, departing, lt, r);
    UNLOCK();
    return ok;
}

// Community route databases (any airport). Often stale for reused flight numbers, so a
// route only counts if one of its legs touches our airport.
static bool lookup_vrs(const std::string& cs, bool departing, Route& r) {
    WiFiClientSecure client;
    HTTPClient http;
    String url = String("https://vrs-standing-data.adsb.lol/routes/") + cs.substr(0, 2).c_str() + "/" + cs.c_str() + ".json";
    if (https_get(http, client, url, 8000) != 200) { http.end(); return false; }
    JsonDocument filter;
    filter["airline_code"] = true;
    filter["_airports"][0]["iata"] = true;
    filter["_airports"][0]["location"] = true;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) return false;
    std::vector<std::pair<std::string, std::string>> ap;
    for (JsonObject a : doc["_airports"].as<JsonArray>()) ap.push_back({a["iata"] | "", a["location"] | ""});
    LOCK();
    bool ok = pick_leg(ap, departing, r);
    UNLOCK();
    return ok;
}

static bool lookup_adsbdb(const std::string& cs, bool departing, Route& r) {
    WiFiClientSecure client;
    HTTPClient http;
    String url = String("https://api.adsbdb.com/v0/callsign/") + cs.c_str();
    if (https_get(http, client, url, 8000) != 200) { http.end(); return false; }
    JsonDocument filter;
    for (const char* k : {"origin", "destination"}) {
        filter["response"]["flightroute"][k]["iata_code"] = true;
        filter["response"]["flightroute"][k]["municipality"] = true;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) return false;
    JsonObject fr = doc["response"]["flightroute"];
    std::vector<std::pair<std::string, std::string>> ap = {
        {fr["origin"]["iata_code"] | "", fr["origin"]["municipality"] | ""},
        {fr["destination"]["iata_code"] | "", fr["destination"]["municipality"] | ""}};
    LOCK();
    bool ok = pick_leg(ap, departing, r);
    UNLOCK();
    return ok;
}

static bool valid_callsign(const std::string& cs) {
    if (cs.size() < 3 || cs.size() > 8) return false;
    for (char c : cs)
        if (!isalnum((unsigned char)c)) return false;
    return true;
}

static String url_encode(const String& s) {
    String o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '.' || c == '_') o += (char)c;
        else if (c == ' ') o += '+';
        else { char b[4]; snprintf(b, sizeof(b), "%%%02X", c); o += b; }
    }
    return o;
}

// Address -> coordinates: US Census geocoder first (precise for US street addresses),
// then OpenStreetMap Nominatim (worldwide; one request per lookup, within its usage policy).
// Looks like a US address (ZIP code, "USA" or "United States")? Only those go to the US Census geocoder.
static bool looks_us(const String& a) {
    String u = a;
    u.toUpperCase();
    if (u.indexOf("USA") >= 0 || u.indexOf("UNITED STATES") >= 0) return true;
    for (int i = 0; i + 5 <= (int)u.length(); i++) {
        bool five = true;
        for (int k = 0; k < 5; k++) five &= isdigit((unsigned char)u[i + k]) != 0;
        bool edge_l = i == 0 || !isdigit((unsigned char)u[i - 1]);
        bool edge_r = i + 5 == (int)u.length() || !isdigit((unsigned char)u[i + 5]);
        if (five && edge_l && edge_r && i > 8) return true;  // a ZIP after the street part
    }
    return false;
}

static bool geocode_address(const String& address, double& lat, double& lon, String& matched) {
    if (looks_us(address)) {
        WiFiClientSecure client;
        HTTPClient http;
        String url = "https://geocoding.geo.census.gov/geocoder/locations/onelineaddress?benchmark=Public_AR_Current&format=json&address=" +
                     url_encode(address);
        int code = https_get(http, client, url, 8000);
        if (code == 200) {
            JsonDocument filter;
            filter["result"]["addressMatches"][0]["matchedAddress"] = true;
            filter["result"]["addressMatches"][0]["coordinates"] = true;
            JsonDocument doc;
            bool ok = !deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
            http.end();
            JsonObject m = doc["result"]["addressMatches"][0];
            if (ok && !m.isNull()) {
                lat = m["coordinates"]["y"];
                lon = m["coordinates"]["x"];
                matched = m["matchedAddress"] | "";
                return true;
            }
        } else {
            http.end();
        }
    }
    WiFiClientSecure client;
    HTTPClient http;
    String url = "https://nominatim.openstreetmap.org/search?format=jsonv2&limit=1&q=" + url_encode(address);
    if (https_get(http, client, url, 8000) != 200) { http.end(); return false; }
    JsonDocument filter;
    filter[0]["lat"] = true;
    filter[0]["lon"] = true;
    filter[0]["display_name"] = true;
    JsonDocument doc;
    bool ok = !deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (!ok || !doc[0]["lat"].is<const char*>()) return false;
    lat = atof(doc[0]["lat"]);
    lon = atof(doc[0]["lon"]);
    matched = doc[0]["display_name"] | "";
    return true;
}

// Coordinates -> ground elevation and time zone (Open-Meteo, worldwide, no key).
static void lookup_elevation_tz(double lat, double lon, float& elev_ft, String& tz) {
    WiFiClientSecure client;
    HTTPClient http;
    char url[128];
    snprintf(url, sizeof(url), "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f&timezone=auto", lat, lon);  // ~1 km
    if (https_get(http, client, url, 8000) == 200) {
        JsonDocument filter;
        filter["elevation"] = true;
        filter["timezone"] = true;
        JsonDocument doc;
        if (!deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))) {
            if (doc["elevation"].is<float>()) elev_ft = doc["elevation"].as<float>() * 3.28084f;
            tz = doc["timezone"] | "";
        }
    }
    http.end();
}

// Airport weather (METAR) from aviationweather.gov: wind for the runway flow, temperature for the clock.
static void fetch_weather(const std::string& airport, double home_lat, double home_lon) {
    AirportInfo ap;
    if (!airport_by_iata(airport, ap) || ap.icao.empty()) return;
    WiFiClientSecure client;
    HTTPClient http;
    String url = "https://aviationweather.gov/api/data/metar?format=json&ids=" + String(ap.icao.c_str());
    if (https_get(http, client, url, 8000) != 200) { http.end(); return; }
    JsonDocument filter;
    for (const char* k : {"obsTime", "temp", "dewp", "wdir", "wspd", "wgst", "cover", "rawOb"}) filter[0][k] = true;
    JsonDocument doc;
    bool ok = !deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    JsonObject m = doc[0];
    if (!ok || m.isNull()) return;
    Weather w;
    w.ok = true;
    w.obs = (time_t)(m["obsTime"] | 0L);
    w.temp_c = m["temp"] | NAN;
    w.dewp_c = m["dewp"] | NAN;
    w.wind_dir = m["wdir"].is<int>() ? m["wdir"].as<int>() : -1;  // "VRB" = variable
    w.wind_kt = m["wspd"] | 0;
    w.gust_kt = m["wgst"] | 0;
    w.cover = m["cover"] | "";
    w.raw = m["rawOb"] | "";
    FlowInfo fl = flow_for(airport, home_lat, home_lon, w);
    LOCK();
    weather = w;
    flow = fl;
    UNLOCK();
}

static void load_hour_model() {
    Preferences p;
    p.begin("model", true);
    if (p.getBytesLength("avg") == sizeof(hour_model.avg)) p.getBytes("avg", hour_model.avg, sizeof(hour_model.avg));
    hour_model.days = p.getInt("days", 0);
    p.end();
}

static void save_hour_model() {
    Preferences p;
    p.begin("model", false);
    p.putBytes("avg", hour_model.avg, sizeof(hour_model.avg));
    p.putInt("days", hour_model.days);
    p.end();
}

// Today's planes and stats, kept across restarts. Saved at most every 2 minutes, only when they changed.
static void save_today() {
    JsonDocument d;
    LOCK();
    const DayStats& st = tracker.stats;
    d["saved"] = (uint32_t)time(nullptr);
    d["yday"] = st.yday;
    d["planes"] = st.planes;
    d["arr"] = st.arrivals;
    d["dep"] = st.departures;
    d["fc"] = st.farthest_city.c_str();
    d["fnm"] = st.farthest_nm;
    d["lt"] = st.loudest_type.c_str();
    d["lcs"] = st.loudest_cs.c_str();
    d["ldb"] = st.loudest_db;
    for (auto& r : st.rare) d["rare"].add(r.c_str());
    for (int h = 0; h < 24; h++) d["hours"].add(st.by_hour[h]);
    for (int i = 0; i < tracker.n_passes; i++) {
        const Pass& p = tracker.passes[i];
        JsonObject o = d["passes"].add<JsonObject>();
        o["t"] = (uint32_t)p.start; o["cs"] = p.cs; o["ty"] = p.type; o["c"] = p.city; o["o"] = p.other_iata;
        o["r"] = p.rare; o["pk"] = p.peak; o["agl"] = p.agl; o["s"] = p.secs; o["d"] = p.dep;
    }
    UNLOCK();
    String out;
    serializeJson(d, out);
    Preferences p;
    p.begin("today", false);
    p.putString("j", out);
    p.end();
}

static void load_today(int yday) {
    Preferences p;
    p.begin("today", true);
    String in = p.getString("j", "");
    p.end();
    JsonDocument d;
    if (in.isEmpty() || deserializeJson(d, in)) return;
    // same day of the year, and saved within the last day (not the same date a year ago)
    if ((d["yday"] | -2) != yday || time(nullptr) - (time_t)(d["saved"] | 0u) > 86400) return;
    LOCK();
    DayStats& st = tracker.stats;
    st.reset(yday);
    st.planes = d["planes"] | 0;
    st.arrivals = d["arr"] | 0;
    st.departures = d["dep"] | 0;
    st.farthest_city = (const char*)(d["fc"] | "");
    st.farthest_nm = d["fnm"] | 0.0;
    st.loudest_type = (const char*)(d["lt"] | "");
    st.loudest_cs = (const char*)(d["lcs"] | "");
    st.loudest_db = d["ldb"] | 0.0f;
    for (JsonVariant r : d["rare"].as<JsonArray>()) st.rare.push_back((const char*)(r | ""));
    int h = 0;
    for (JsonVariant v : d["hours"].as<JsonArray>()) if (h < 24) st.by_hour[h++] = v | 0;
    tracker.n_passes = 0;
    for (JsonObject o : d["passes"].as<JsonArray>()) {
        if (tracker.n_passes >= MAX_PASSES) break;
        Pass& ps = tracker.passes[tracker.n_passes++];
        memset(&ps, 0, sizeof(ps));
        ps.start = (time_t)(o["t"] | 0u);
        strncpy(ps.cs, o["cs"] | "", sizeof(ps.cs) - 1);
        strncpy(ps.type, o["ty"] | "", sizeof(ps.type) - 1);
        strncpy(ps.city, o["c"] | "", sizeof(ps.city) - 1);
        strncpy(ps.other_iata, o["o"] | "", sizeof(ps.other_iata) - 1);
        strncpy(ps.rare, o["r"] | "", sizeof(ps.rare) - 1);
        ps.peak = o["pk"] | 0.0f;
        ps.agl = o["agl"] | 0;
        ps.secs = o["s"] | 0.0f;
        ps.dep = o["d"] | false;
    }
    UNLOCK();
}

static void finish_geocode(GeoState st, const String& msg) {
    LOCK();
    geo_message = msg;
    geo_state = st;
    UNLOCK();
}

static void do_geocode() {
    LOCK();
    String request = geo_request;
    double lat = geo_lat, lon = geo_lon;
    UNLOCK();
    String matched;
    bool by_address = request.length() > 0;
    if (by_address && !geocode_address(request, lat, lon, matched)) {
        finish_geocode(GeoState::Failed, "That address wasn't found. Include the street number, city and postcode or ZIP, "
                                         "or enter coordinates instead.");
        return;
    }
    float elev = 300;
    String tz;
    lookup_elevation_tz(lat, lon, elev, tz);
    const char* posix = tz.length() ? posix_for_zone(tz.c_str()) : nullptr;

    LOCK();
    S.address = by_address ? request.c_str() : "";
    S.home_lat = lat;
    S.home_lon = lon;
    S.home_elev = elev;
    if (posix) {
        S.tz_name = tz.c_str();
        S.tz_posix = posix;
        // first guess at temperature units from the time zone; changeable on the settings page
        S.units_f = tz.startsWith("America/New_York") || tz.startsWith("America/Chicago") || tz.startsWith("America/Denver") ||
                    tz.startsWith("America/Los_Angeles") || tz.startsWith("America/Phoenix") || tz.startsWith("America/Anchorage") ||
                    tz.startsWith("America/Indiana") || tz.startsWith("America/Detroit") || tz.startsWith("Pacific/Honolulu") ||
                    tz.startsWith("America/Kentucky") || tz.startsWith("America/Boise") || tz.startsWith("America/Puerto_Rico");
    }
    bool airport = resolve_airport();
    tracker.clear();
    hour_model = HourModel();  // a new place has new busy hours
    weather = Weather();
    flow = FlowInfo();
    settings_save();
    UNLOCK();
    save_hour_model();
    apply_timezone();
    String msg = by_address ? matched : String("Coordinates saved");
    if (!airport) msg += ". No airport with scheduled flights within 60 nautical miles, so planes overhead will be too high to hear.";
    finish_geocode(GeoState::Ok, msg);
}

static void network_task(void*) {
    double last_refill = now_s(), last_weather = -1e9;
    int today = -1;
    for (;;) {
        if (WiFi.status() != WL_CONNECTED || !time_valid()) { delay(500); continue; }
        if (ESP.getFreeHeap() < 40000) { set_error("low memory, skipping network"); delay(5000); continue; }

        if (geo_state == GeoState::Pending) do_geocode();

        // snapshot what this pass needs, so the settings page can change S at any time
        LOCK();
        bool have_loc = location_set();
        double home_lat = S.home_lat, home_lon = S.home_lon;
        std::string airport = S.airport_iata;
        int quiet_start = S.quiet_start, quiet_end = S.quiet_end, budget = S.daily_budget;
        bool smart = S.smart_polling, board = S.airport_board;
        UNLOCK();

        double now = now_s();
        time_t tt = (time_t)now;
        struct tm lt;
        localtime_r(&tt, &lt);
        if (lt.tm_yday != today) {
            if (today < 0) load_today(lt.tm_yday);  // after a restart: pick the day up where it left off
            if (today >= 0) {  // a day ended: learn its busy hours, start fresh stats
                LOCK();
                if (tracker.stats.yday == today) hour_model.end_of_day(tracker.stats.by_hour);
                tracker.stats.reset(lt.tm_yday);
                UNLOCK();
                save_hour_model();
            }
            today = lt.tm_yday;
            calls_today = 0;
            board_calls_today = 0;
        }

        // airport weather every 30 minutes (separate free service, not part of the flight-data budget)
        if (have_loc && !airport.empty() && now - last_weather > 1800) {
            last_weather = now;
            fetch_weather(airport, home_lat, home_lon);
        }

        LOCK();
        quiet_now = is_quiet(lt.tm_hour);
        UNLOCK();
        if (quiet_now || !have_loc || airport.empty()) {
            LOCK();
            tracker.clear();
            UNLOCK();
            last_refill = now;
            delay(1000);
            continue;
        }

        // token bucket refilled over the active (non-quiet) part of the day
        int quiet_h = quiet_start == quiet_end ? 0 : (quiet_end - quiet_start + 24) % 24;
        double rate = budget / ((24 - quiet_h) * 3600.0);
        tokens = fmin((double)BUCKET_CAPACITY, tokens + (now - last_refill) * rate);
        last_refill = now;

        LOCK();
        double want = tracker.want_poll_at(now);
        bool recent = tracker.n_passes && now - tracker.passes[0].start < 2700;
        bool away = flow.flow == Flow::AwayFromYou, usually_quiet = hour_model.usually_quiet(lt.tm_hour);
        UNLOCK();
        // Smart polling: when the wind sends planes away from the house, or this hour has (over the
        // last week) almost never brought planes over, check every 15 minutes instead of every 4.
        double idle = IDLE_INTERVAL_S;
        if (smart && !recent && (away || usually_quiet)) idle = 900;
        // a few seconds of per-clock jitter so many clocks never poll in lockstep
        static const double jitter = (esp_random() % 8000) / 1000.0;
        double next = fmax(last_poll + HOT_INTERVAL_S, fmin(last_poll + idle + jitter, want));
        if (tokens < 1) next = fmax(next, now + (1 - tokens) / rate);
        next = fmax(next, feed_backoff_until);
        next_poll = next;

        if (now >= next) {
            tokens = tokens - 1;
            calls_today = calls_today + 1;
            last_poll = now;
            std::vector<Fix> fixes;
            if (fetch_aircraft(home_lat, home_lon, fixes)) {
                LOCK();
                tracker.ingest(fixes, now_s());
                UNLOCK();
            }
        }

        static int saved_planes = -1;
        static double last_save = 0;
        if (tracker.stats.planes != saved_planes && now - last_save > 120) {
            saved_planes = tracker.stats.planes;
            last_save = now;
            save_today();
        }

        std::string cs;
        bool dep = false;
        LOCK();
        RouteEntry* e = tracker.routes.next_request();
        if (e) { cs = e->cs; dep = e->departing; }
        UNLOCK();
        if (!cs.empty()) {
            Route r;
            bool ok = valid_callsign(cs) &&
                      (lookup_board(board, airport, cs, dep, r) || lookup_vrs(cs, dep, r) || lookup_adsbdb(cs, dep, r));
            LOCK();
            tracker.routes.complete(cs, dep, ok, r, now_s());
            UNLOCK();
        }
        delay(250);
    }
}

// ================================================================ settings page + API
// Wrong passwords back off per device on the network (so one device can't lock the owner out):
// after 5 failures from an address, each further try from it waits 30 s, doubling up to 10 minutes.
struct AuthStrike { uint32_t ip = 0; int fails = 0; uint32_t until = 0; uint32_t last = 0; };
static AuthStrike strikes[8];

static AuthStrike& strike_for(uint32_t ip) {
    AuthStrike* oldest = &strikes[0];
    for (auto& s : strikes) {
        if (s.ip == ip) return s;
        if (s.last < oldest->last) oldest = &s;
    }
    *oldest = AuthStrike();
    oldest->ip = ip;
    return *oldest;
}

// Requests must be addressed to this clock (by its IP or overhead.local): defeats DNS rebinding.
static bool host_ok() {
    String h = web.hostHeader();
    int colon = h.indexOf(':');
    if (colon >= 0) h = h.substring(0, colon);
    h.toLowerCase();
    return h == WiFi.localIP().toString() || h == HOSTNAME ".local";
}

// The Digest "uri" must be this request's path, so a captured header can't be replayed elsewhere.
static bool digest_uri_matches() {
    String a = web.header("Authorization");
    int i = a.indexOf("uri=\"");
    if (i < 0) return true;  // no digest yet: authenticate() will fail and challenge
    int j = a.indexOf('"', i + 5);
    String uri = a.substring(i + 5, j < 0 ? a.length() : j);
    int q = uri.indexOf('?');
    if (q >= 0) uri = uri.substring(0, q);
    return uri == web.uri();
}

static void security_headers() {
    web.sendHeader("Cache-Control", "no-store");
    web.sendHeader("X-Content-Type-Options", "nosniff");
    web.sendHeader("X-Frame-Options", "DENY");
    web.sendHeader("Referrer-Policy", "no-referrer");
}

static bool authed() {
    security_headers();
    if (!host_ok()) {
        web.send(421, "text/plain", "Use the clock's own address.");
        return false;
    }
    uint32_t now = millis();
    AuthStrike& st = strike_for((uint32_t)web.client().remoteIP());
    st.last = now;
    if (st.until && (int32_t)(st.until - now) > 0) {
        web.send(429, "text/plain", "Too many wrong passwords from this device. Wait a minute and try again.");
        return false;
    }
    if (digest_uri_matches() && web.authenticate("admin", admin_pw.c_str())) {
        st.fails = 0;
        st.until = 0;
        return true;
    }
    if (web.hasHeader("Authorization") && ++st.fails >= 5) {  // a wrong password, not just the first challenge
        uint32_t wait = 30000u << min(st.fails - 5, 4);          // 30 s, 1, 2, 4, 8 min
        st.until = now + min(wait, 600000u);
    }
    web.requestAuthentication(DIGEST_AUTH, "Overhead Clock", "Sign in as admin with the clock's password.");
    return false;
}

// Changes must be JSON with our header. Browsers won't send that cross-site without a CORS
// preflight, which this server never approves, so other web pages can't change settings.
static bool write_ok(JsonDocument& body) {
    if (!authed()) return false;
    if (web.header("X-Overhead") != "1") {
        web.send(403, "application/json", "{\"error\":\"missing X-Overhead header\"}");
        return false;
    }
    String plain = web.arg("plain");
    if (plain.length() > 2048 || deserializeJson(body, plain, DeserializationOption::NestingLimit(3)) || !body.is<JsonObject>()) {
        web.send(400, "application/json", "{\"error\":\"body must be a small JSON object\"}");
        return false;
    }
    return true;
}

static void send_json(JsonDocument& d, int code = 200) {
    String out;
    serializeJson(d, out);
    web.send(code, "application/json", out);
}

static void api_status() {
    if (!authed()) return;
    double now = now_s();
    JsonDocument d;
    LOCK();
    d["version"] = FW_VERSION;
    d["mode"] = S.mode ? "clock" : "plane";
    d["quiet"] = (bool)quiet_now;
    d["sign"] = tracker.msg.active ? tracker.msg.text.c_str() : "";
    d["sign_db"] = tracker.msg.active ? tracker.msg.level : 0;
    d["sign_color"] = rgb_to_hex(((uint32_t)tracker.msg.r << 16) | (tracker.msg.g << 8) | tracker.msg.b);
    JsonObject wifi = d["wifi"].to<JsonObject>();
    wifi["ssid"] = WiFi.SSID();
    wifi["rssi"] = WiFi.RSSI();
    wifi["ip"] = WiFi.localIP().toString();
    JsonObject feed = d["feed"].to<JsonObject>();
    feed["calls_today"] = (int)calls_today;
    feed["budget"] = S.daily_budget;
    feed["next_poll_s"] = (int)fmax(0, next_poll - now);
    feed["error"] = last_error;
    d["free_heap"] = ESP.getFreeHeap();
    d["min_free_heap"] = ESP.getMinFreeHeap();
    d["uptime_s"] = (uint32_t)(esp_timer_get_time() / 1000000);
    static const char* reasons[] = {"unknown", "power on", "external pin", "restart", "crash", "interrupt watchdog",
                                    "task watchdog", "other watchdog", "deep sleep", "brownout (low power)", "SDIO"};
    int rr = (int)esp_reset_reason();
    d["reset_reason"] = rr >= 0 && rr < (int)(sizeof(reasons) / sizeof(reasons[0])) ? reasons[rr] : "unknown";
    JsonObject loc = d["location"].to<JsonObject>();
    loc["set"] = location_set();
    loc["address"] = S.address.c_str();
    loc["lat"] = S.home_lat;
    loc["lon"] = S.home_lon;
    loc["elev_ft"] = S.home_elev;
    loc["timezone"] = S.tz_name.c_str();
    loc["lookup"] = geo_state == GeoState::Pending ? "pending" : geo_state == GeoState::Ok ? "ok" : geo_state == GeoState::Failed ? "failed" : "idle";
    loc["message"] = geo_message;
    JsonObject ap = d["airport"].to<JsonObject>();
    ap["iata"] = S.airport_iata.c_str();
    ap["city"] = S.airport_city.c_str();
    ap["auto"] = S.airport_choice.empty();
    if (location_set()) {
        double dist, brg;
        dist_bearing_ft(S.home_lat, S.home_lon, S.airport_lat, S.airport_lon, dist, brg);
        ap["dist_nm"] = S.airport_iata.empty() ? 0 : dist / 6076.12;
        JsonArray nearby = ap["nearby"].to<JsonArray>();
        for (auto& a : nearest_airports(S.home_lat, S.home_lon, 6, MAX_AIRPORT_NM)) {
            JsonObject o = nearby.add<JsonObject>();
            o["iata"] = a.iata;
            o["city"] = a.city;
            o["dist_nm"] = a.dist_nm;
        }
    }
    JsonObject wx = d["weather"].to<JsonObject>();
    wx["ok"] = weather.ok;
    if (weather.ok) {
        wx["temp_c"] = weather.temp_c;
        wx["wind_dir"] = weather.wind_dir;
        wx["wind_kt"] = weather.wind_kt;
        wx["gust_kt"] = weather.gust_kt;
        wx["cover"] = weather.cover.c_str();
        wx["raw"] = weather.raw.c_str();
        wx["obs"] = (long)weather.obs;
    }
    wx["flow"] = flow_text(flow.flow);
    wx["runway"] = flow.runway.c_str();
    if (!isnan(indoor_c)) {
        d["indoor"]["temp_c"] = indoor_c;
        d["indoor"]["rh"] = indoor_rh;
    }
    JsonObject today = d["today"].to<JsonObject>();
    today["planes"] = tracker.stats.planes;
    today["arrivals"] = tracker.stats.arrivals;
    today["departures"] = tracker.stats.departures;
    today["farthest_city"] = tracker.stats.farthest_city.c_str();
    today["farthest_mi"] = (int)(tracker.stats.farthest_nm * 1.15078);
    today["loudest_type"] = tracker.stats.loudest_type.c_str();
    today["loudest_db"] = tracker.stats.loudest_db;
    JsonArray rare = today["rare"].to<JsonArray>();
    for (auto& r : tracker.stats.rare) rare.add(r.c_str());
    JsonArray hours = today["by_hour"].to<JsonArray>();
    for (int h = 0; h < 24; h++) hours.add(tracker.stats.by_hour[h]);
    JsonArray usual = d["usual_by_hour"].to<JsonArray>();
    for (int h = 0; h < 24; h++) usual.add(roundf(hour_model.avg[h] * 10) / 10);
    d["usual_days"] = hour_model.days;
    d["inbound_s"] = tracker.inbound_s;
    JsonArray planes = d["planes"].to<JsonArray>();
    bool shown[MAX_TRACKS] = {false};
    for (int k = 0; k < 8; k++) {
        int bi = -1;
        for (int i = 0; i < MAX_TRACKS; i++)
            if (tracker.tracks[i].used && !shown[i] && (bi < 0 || tracker.tracks[i].level > tracker.tracks[bi].level)) bi = i;
        if (bi < 0) break;
        shown[bi] = true;
        Track& t = tracker.tracks[bi];
        JsonObject p = planes.add<JsonObject>();
        p["cs"] = t.f.cs;
        p["type"] = t.f.type;
        p["dist_nm"] = t.horiz_ft / 6076.12f;
        p["agl_ft"] = (int)t.agl_ft;
        p["db"] = t.level;
        p["dep"] = t.departing;
    }
    JsonArray passes = d["passes"].to<JsonArray>();
    for (int i = 0; i < tracker.n_passes; i++) {
        Pass& ps = tracker.passes[i];
        JsonObject p = passes.add<JsonObject>();
        p["t"] = (long)ps.start;
        p["cs"] = ps.cs;
        p["type"] = ps.type;
        p["city"] = ps.city;
        p["dep"] = ps.dep;
        p["rare"] = ps.rare;
        p["peak"] = ps.peak;
        p["agl_ft"] = ps.agl;
        p["secs"] = ps.secs;
    }
    UNLOCK();
    send_json(d);
}

static void api_get_settings() {
    if (!authed()) return;
    JsonDocument d;
    settings_to_json(d.to<JsonObject>());
    send_json(d);
}

static void api_post_settings() {
    JsonDocument body;
    if (!write_ok(body)) return;
    LOCK();
    std::string ntp_before = S.ntp_server;
    String err = settings_apply_json(body.as<JsonObjectConst>());
    bool ntp_changed = S.ntp_server != ntp_before;
    UNLOCK();
    apply_timezone();
    if (ntp_changed) start_time_sync();
    JsonDocument d;
    if (err.length()) { d["error"] = err; send_json(d, 400); return; }
    settings_to_json(d.to<JsonObject>());
    send_json(d);
}

// {"address": "..."} or {"lat": .., "lon": ..}; coordinates keep the street address off the internet entirely
static void api_locate() {
    JsonDocument body;
    if (!write_ok(body)) return;
    JsonDocument d;
    if (geo_state == GeoState::Pending) {
        d["error"] = "Still looking up the last address. Try again in a moment.";
        send_json(d, 409);
        return;
    }
    String request;
    double lat = 0, lon = 0;
    if (body["lat"].is<double>() && body["lon"].is<double>()) {
        lat = body["lat"];
        lon = body["lon"];
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180 || (lat == 0 && lon == 0)) {
            d["error"] = "Latitude must be -90 to 90 and longitude -180 to 180.";
            send_json(d, 400);
            return;
        }
    } else {
        String addr = body["address"] | "";
        addr.trim();
        if (addr.length() < 8 || addr.length() > 120) {
            d["error"] = "Enter the full street address, including city and postcode or ZIP.";
            send_json(d, 400);
            return;
        }
        request = addr;
    }
    LOCK();
    geo_request = request;
    geo_lat = lat;
    geo_lon = lon;
    geo_message = "";
    geo_state = GeoState::Pending;
    UNLOCK();
    d["lookup"] = "pending";
    send_json(d, 202);
}

static void api_preview() {
    JsonDocument body;
    if (!write_ok(body)) return;
    String t = body["text"] | "";
    t = t.substring(0, 40);
    t.toUpperCase();
    uint32_t c = 0xFF7800;
    hex_to_rgb(body["color"] | "", c);
    show_override(t.c_str(), 10, c);
    web.send(204);
}

static void api_password() {
    JsonDocument body;
    if (!write_ok(body)) return;
    String pw = body["password"] | "";
    JsonDocument d;
    if (pw.length() < 8 || pw.length() > 64) {
        d["error"] = "Use 8 to 64 characters.";
        send_json(d, 400);
        return;
    }
    LOCK();
    secret_set_admin_password(pw);
    admin_pw = pw;
    UNLOCK();
    d["ok"] = true;
    send_json(d);
}

static void api_forget_wifi() {
    JsonDocument body;
    if (!write_ok(body)) return;
    web.send(202, "application/json", "{\"ok\":true}");
    delay(300);
    WiFiManager wm;
    wm.setDebugOutput(false);
    wm.resetSettings();
    ESP.restart();
}

static void api_restart() {
    JsonDocument body;
    if (!write_ok(body)) return;
    web.send(202, "application/json", "{\"ok\":true}");
    delay(300);
    ESP.restart();
}

// ---- firmware update: POST /api/update with the raw .bin as the body (same auth + header rules)
static bool ota_allowed = false;
static String ota_error;

// The quiet version of authed() + write_ok() for the streaming callback (it can't send responses).
static bool update_request_ok() {
    AuthStrike& st = strike_for((uint32_t)web.client().remoteIP());
    if (st.until && (int32_t)(st.until - millis()) > 0) return false;
    return host_ok() && digest_uri_matches() && web.authenticate("admin", admin_pw.c_str()) &&
           web.header("X-Overhead") == "1";
}

static void api_update_stream() {
    HTTPRaw& raw = web.raw();
    if (raw.status == RAW_START) {
        ota_error = "";
        ota_allowed = update_request_ok();
        if (!ota_allowed) return;
        show_override("UPDATING", 600, 0x00B4FF);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) ota_error = Update.errorString();
    } else if (raw.status == RAW_WRITE) {
        if (ota_allowed && ota_error.isEmpty() && Update.write(raw.buf, raw.currentSize) != raw.currentSize)
            ota_error = Update.errorString();
    } else if (raw.status == RAW_END) {
        if (ota_allowed && ota_error.isEmpty() && !Update.end(true)) ota_error = Update.errorString();
    } else if (raw.status == RAW_ABORTED) {
        if (ota_allowed) Update.abort();
        ota_error = "upload interrupted";
    }
}

static void api_update_done() {
    if (!ota_allowed) {  // let authed()/write_ok() send the right challenge or refusal
        JsonDocument body;
        if (!authed()) return;
        web.send(403, "application/json", "{\"error\":\"missing X-Overhead header\"}");
        return;
    }
    JsonDocument d;
    if (ota_error.length()) {
        show_override("UPDATE FAILED", 6, 0xFF2020);
        d["error"] = "Update failed: " + ota_error + ". The clock is still running the old firmware.";
        send_json(d, 400);
        return;
    }
    d["ok"] = true;
    send_json(d);
    show_override("RESTARTING", 3, 0x00B4FF);
    delay(800);
    ESP.restart();
}

static void handle_ui() {
    if (!authed()) return;
    web.sendHeader("Content-Security-Policy",
                   "default-src 'none'; connect-src 'self'; style-src 'unsafe-inline'; "
                   "script-src 'unsafe-inline'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'");
    web.send_P(200, "text/html; charset=utf-8", UI_HTML);
}

// ================================================================ sensors and real-time clock
// TC001 I2C bus: SHT3x temperature/humidity at 0x44, DS1307 real-time clock at 0x68.
static bool i2c_present(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static time_t rtc_read_epoch() {
    Wire.beginTransmission(0x68);
    Wire.write(0);
    if (Wire.endTransmission() != 0 || Wire.requestFrom(0x68, 7) != 7) return 0;
    uint8_t r[7];
    for (auto& b : r) b = Wire.read();
    if (r[0] & 0x80) return 0;  // oscillator halted: never set
    struct tm t = {};
    t.tm_sec = bcd2bin(r[0] & 0x7F);
    t.tm_min = bcd2bin(r[1]);
    t.tm_hour = bcd2bin(r[2] & 0x3F);
    t.tm_mday = bcd2bin(r[4]);
    t.tm_mon = bcd2bin(r[5] & 0x1F) - 1;
    t.tm_year = bcd2bin(r[6]) + 100;
    time_t utc = utc_from_tm(t);
    return utc > 1700000000 ? utc : 0;
}

static void rtc_read_into_system_clock() {
    Wire.beginTransmission(0x68);
    Wire.write(0);
    if (Wire.endTransmission() != 0 || Wire.requestFrom(0x68, 7) != 7) return;
    uint8_t r[7];
    for (auto& b : r) b = Wire.read();
    if (r[0] & 0x80) return;  // oscillator halted: the RTC has never been set
    struct tm t = {};
    t.tm_sec = bcd2bin(r[0] & 0x7F);
    t.tm_min = bcd2bin(r[1]);
    t.tm_hour = bcd2bin(r[2] & 0x3F);
    t.tm_mday = bcd2bin(r[4]);
    t.tm_mon = bcd2bin(r[5] & 0x1F) - 1;
    t.tm_year = bcd2bin(r[6]) + 100;
    time_t utc = utc_from_tm(t);  // the RTC keeps UTC
    if (utc < 1700000000) return;
    struct timeval tv = {utc, 0};
    settimeofday(&tv, nullptr);
}

static void rtc_write_from_system_clock() {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    Wire.beginTransmission(0x68);
    Wire.write(0);
    Wire.write(bin2bcd(t.tm_sec));
    Wire.write(bin2bcd(t.tm_min));
    Wire.write(bin2bcd(t.tm_hour));
    Wire.write(bin2bcd(t.tm_wday + 1));
    Wire.write(bin2bcd(t.tm_mday));
    Wire.write(bin2bcd(t.tm_mon + 1));
    Wire.write(bin2bcd(t.tm_year - 100));
    Wire.endTransmission();
}

static void read_indoor() {
    Wire.beginTransmission(0x44);
    Wire.write(0x24);
    Wire.write(0x00);  // single shot, high repeatability, no clock stretching
    if (Wire.endTransmission() != 0) return;
    delay(16);
    if (Wire.requestFrom(0x44, 6) != 6) return;
    uint8_t b[6];
    for (auto& v : b) v = Wire.read();
    float t = -45 + 175 * ((b[0] << 8) | b[1]) / 65535.0f;
    float rh = 100 * ((b[3] << 8) | b[4]) / 65535.0f;
    indoor_c = t + S.temp_offset_c;
    indoor_rh = constrain(rh, 0.0f, 100.0f);
}

// Short two-note chime for rare aircraft (only if enabled, never in quiet hours).
static uint32_t chime_until = 0;
static void chime() {
    if (!S.rare_chime || quiet_now) return;
    tone(PIN_BUZZER, 1760, 90);
    chime_until = millis() + 200;
}
static void chime_tick() {
    if (chime_until && (int32_t)(millis() - chime_until) >= 0) {
        chime_until = 0;
        tone(PIN_BUZZER, 2349, 120);
    }
}

static String temp_text(float c) {
    if (isnan(c)) return "";
    return String((int)lroundf(S.units_f ? c * 9 / 5 + 32 : c)) + (S.units_f ? "F" : "C");
}

static const char* compass(int deg) {
    static const char* pts[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return pts[((deg + 22) % 360) / 45];
}

static String fmt_hm(int minutes) {
    char b[8];
    int h = minutes / 60, m = minutes % 60;
    if (S.clock_24h) snprintf(b, sizeof(b), "%02d:%02d", h, m);
    else snprintf(b, sizeof(b), "%d:%02d", (h + 11) % 12 + 1, m);
    return b;
}

// Text for one extra clock screen, or "" if there is nothing to say for it.
static std::string rotation_text(int bit, time_t now) {
    String t;
    switch (bit) {
        case ROT_INDOOR:
            if (!isnan(indoor_c)) t = "INSIDE " + temp_text(indoor_c) + " " + String((int)lroundf(indoor_rh)) + "%";
            break;
        case ROT_OUTDOOR:
            if (weather.ok && !isnan(weather.temp_c)) {
                t = "OUTSIDE " + temp_text(weather.temp_c);
                if (weather.wind_kt >= 3)
                    t += String("  WIND ") + (weather.wind_dir >= 0 ? compass(weather.wind_dir) : "VRB") + " " +
                         String((int)lroundf(weather.wind_kt * 1.15078f)) + " MPH";
            }
            break;
        case ROT_DATE: {
            struct tm lt;
            localtime_r(&now, &lt);
            char b[24];
            strftime(b, sizeof(b), "%a %b %e", &lt);
            t = b;
            break;
        }
        case ROT_SUN: {
            int rise, set;
            if (location_set() && sun_times(now, S.home_lat, S.home_lon, rise, set))
                t = "SUNRISE " + fmt_hm(rise) + "  SUNSET " + fmt_hm(set);
            break;
        }
        case ROT_FLOW:
            if (flow.flow != Flow::Unknown) t = String(flow_text(flow.flow)) + (flow.runway.length() ? " " + String(flow.runway.c_str()) : "");
            break;
        case ROT_TODAY:
            t = String(tracker.stats.planes) + (tracker.stats.planes == 1 ? " PLANE TODAY" : " PLANES TODAY");
            break;
    }
    t.toUpperCase();
    return t.c_str();
}

// Small plane icon (7 wide) for the inbound countdown.
static const uint8_t PLANE_ICON[7] = {0x08, 0x08, 0x1C, 0x7F, 0x1C, 0x08, 0x1C};

static void inbound_frame(float secs, uint8_t* cols) {
    for (int i = 0; i < MATRIX_W; i++) cols[i] = 0;
    std::vector<uint8_t> num = text_columns(std::to_string((int)ceilf(secs)));
    int num_x = MATRIX_W - (int)num.size();
    for (size_t i = 0; i < num.size(); i++) cols[num_x + i] = num[i];
    // the plane flies from the left edge toward the number as the seconds count down
    int travel = num_x - 9;
    int x = travel - (int)(travel * fminf(secs, 30.0f) / 30.0f);
    for (int i = 0; i < 7; i++)
        if (x + i >= 0 && x + i < num_x - 1) cols[x + i] = PLANE_ICON[i] >> 0;
}

// ================================================================ buttons & light
//   Left    press: what to show, CITY > CODES > AIRLINE > FULL     hold 1.5 s: replay recent planes
//   Middle  press: Clock + planes / Planes only                    hold 3 s:   settings address + password
//   Right   press: brightness, AUTO > LOW > MEDIUM > HIGH
//   Holds act while the button is still down. Left held while plugging in: forget Wi-Fi and start setup again
static float ambient = 0.5f;
static int history_pos = -1;
static double history_until = 0;

static void read_light() {
    float raw = analogRead(PIN_LDR) / 4095.0f;  // brighter room -> higher reading on the TC001
    ambient = ambient * 0.98f + raw * 0.02f;
}

static void preview_format() {
    // show the format's name, then what the last plane would look like in it (or a sample)
    Route sample;
    sample.from_city = "Denver"; sample.from_iata = "DEN"; sample.to_city = "Minneapolis"; sample.to_iata = "MSP"; sample.mkt = "DL";
    std::string text = format_flight(S.display_format, &sample, false, "DAL1902", "A321");
    show_override(std::string(format_name(S.display_format)) + ":  " + text, 6, 0xFFFFFF);
}

static void replay_recent() {
    double now = now_s();
    LOCK();
    int n = tracker.n_passes;
    history_pos = (now < history_until && history_pos + 1 < n) ? history_pos + 1 : 0;
    std::string text = "NO PLANES YET";
    if (n) {
        Pass& p = tracker.passes[history_pos];
        char ts[8];
        struct tm lt;
        localtime_r(&p.start, &lt);
        strftime(ts, sizeof(ts), "%H:%M", &lt);
        text = std::string(ts) + "  " + (p.city[0] ? p.city : p.cs) + "  " + p.cs;
    }
    UNLOCK();
    for (auto& c : text) c = toupper((unsigned char)c);
    history_until = now + 8;
    show_override(text, 8, 0xFFFFFF);
}

static void handle_buttons() {
    // A press acts when it's released; a hold acts as soon as the button has been down long
    // enough, so the display answers while it's still held and the release afterwards does nothing.
    struct Btn { int pin; bool was; uint32_t at; bool fired; };
    static Btn left{PIN_BTN_LEFT, false, 0, false}, mid{PIN_BTN_MID, false, 0, false}, right{PIN_BTN_RIGHT, false, 0, false};
    auto poll = [](Btn& b, uint32_t hold_ms, bool& press, bool& hold) {
        bool down = digitalRead(b.pin) == LOW;
        uint32_t ms = millis();
        press = hold = false;
        if (down && !b.was) { b.at = ms; b.fired = false; }
        if (down && hold_ms && !b.fired && ms - b.at >= hold_ms) { b.fired = true; hold = true; }
        if (!down && b.was && !b.fired) press = ms - b.at > 30;  // ignore contact bounce
        b.was = down;
    };
    bool press_l, hold_l, press_m, hold_m, press_r, hold_r;
    poll(left, HOLD_LEFT_MS, press_l, hold_l);
    poll(mid, HOLD_MID_MS, press_m, hold_m);
    poll(right, 0, press_r, hold_r);
    double now = now_s();
    LOCK();
    bool seq_on = credential_seq.active(now);
    if (seq_on) {  // while the address and password are showing, the buttons page through them
        if (press_l) credential_seq.prev(now);
        if (press_r) credential_seq.next(now);
        if (press_m) credential_seq.stop();
    }
    UNLOCK();
    if (seq_on) {
        if (hold_m) show_credentials();  // start over
        return;
    }
    if (hold_l) replay_recent();
    if (press_l) {
        S.display_format = (S.display_format + 1) % DISPLAY_FORMATS;
        save_settings_locked();
        preview_format();
    }
    if (hold_m) show_credentials();  // physical presence: how to reach the settings page
    if (press_m) {
        S.mode = S.mode ? 0 : 1;
        save_settings_locked();
        show_override(S.mode ? "CLOCK" : "PLANES", 2, 0x00B4FF);
    }
    if (press_r) {
        S.brightness = (S.brightness + 1) % 4;
        save_settings_locked();
        static const char* names[] = {"AUTO", "LOW", "MEDIUM", "HIGH"};
        show_override(names[S.brightness], 2, 0xFFFFFF);
    }
}

static float brightness_base() {
    switch (S.brightness) {
        case 1: return S.bright_min + 4.0f;
        case 2: return (S.bright_min + S.bright_max) * 0.5f;
        case 3: return (float)S.bright_max;
        default: return S.bright_min + (S.bright_max - S.bright_min) * ambient;
    }
}

// ================================================================ setup / loop
// The portal runs non-blocking so the sign can keep scrolling the network name and password.
static void run_setup_portal(WiFiManager& wm, const String& ap_name, const String& ap_pw) {
    SignSequence seq;
    std::vector<SignStep> steps = credential_steps(("WIFI " + ap_name).c_str(), ap_pw);
    uint8_t cols[MATRIX_W];
    bool was_l = false, was_r = false;
    while (WiFi.status() != WL_CONNECTED) {
        wm.process();
        double now = now_s();
        if (!seq.active(now)) seq.start(steps, now, 2, 1000);  // loops for as long as setup takes
        bool l = digitalRead(PIN_BTN_LEFT) == LOW, r = digitalRead(PIN_BTN_RIGHT) == LOW;
        if (was_l && !l) seq.prev(now);
        if (was_r && !r) seq.next(now);
        was_l = l;
        was_r = r;
        for (auto& c : cols) c = 0;
        seq.frame(now, cols);
        draw_columns(cols, CRGB(255, 0, 180));
        FastLED.setBrightness(30);
        FastLED.show();
        delay(30);
    }
}

void setup() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);  // the TC001 buzzer whines if left floating
    pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
    pinMode(PIN_BTN_MID, INPUT_PULLUP);
    pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
    btStop();
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);  // Bluetooth is never used

    FastLED.addLeds<WS2812B, PIN_MATRIX, GRB>(leds, MATRIX_W * MATRIX_H);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);

    // orientation check: top-left pixel red, bottom-right green
    fill_solid(leds, MATRIX_W * MATRIX_H, CRGB::Black);
    leds[xy(0, 0)] = CRGB::Red;
    leds[xy(MATRIX_W - 1, MATRIX_H - 1)] = CRGB::Green;
    FastLED.setBrightness(30);
    FastLED.show();
    delay(1200);

    Wire.begin(21, 22);
    have_rtc = i2c_present(0x68);
    have_sht = i2c_present(0x44);
    if (have_rtc) rtc_read_into_system_clock();  // the clock works even with no internet

    lock = xSemaphoreCreateRecursiveMutex();
    settings_load();
    load_hour_model();
    apply_timezone();
    admin_pw = secret_admin_password();
    String setup_pw = secret_setup_password();
    String ap_name = "Overhead-" + String((uint32_t)(ESP.getEfuseMac() >> 24) & 0xFFFF, HEX);
    ap_name.toUpperCase();

    WiFiManager wm;
    wm.setDebugOutput(false);  // never print network details or passwords to the serial port
    if (digitalRead(PIN_BTN_LEFT) == LOW) {  // left button held at power-up: forget Wi-Fi
        draw_static("RESET", CRGB(255, 0, 0));
        wm.resetSettings();
        delay(1000);
    }
    wm.setTitle("Overhead Clock setup");
    wm.setClass("invert");
    std::vector<const char*> menu = {"wifi"};
    wm.setMenu(menu);  // no firmware upload, info, erase or exit pages
    wm.setShowInfoUpdate(false);
    wm.setShowInfoErase(false);
    // Hiding menu items isn't enough: WiFiManager still registers its firmware-upload, erase and info
    // routes. This callback runs before it does, and the first matching route wins, so these say 404.
    wm.setWebServerCallback([&wm]() {
        auto deny = [&wm]() { wm.server->send(404, "text/plain", "Not found"); };
        for (const char* path : {"/update", "/erase", "/info", "/exit", "/restart", "/param", "/paramsave"})
            wm.server->on(path, HTTP_ANY, deny);
        wm.server->on("/u", HTTP_ANY, deny, []() {});  // swallow any upload instead of flashing it
    });
    wm.setMinimumSignalQuality(10);
    wm.setHostname(HOSTNAME);
    WiFiManagerParameter p_addr("address", "Home street address (to work out which planes you can hear)",
                                "", 120, "placeholder='Street, city, postcode or ZIP'");
    wm.addParameter(&p_addr);
    bool first_setup = false;
    wm.setSaveConfigCallback([&]() { first_setup = true; });

    WiFi.mode(WIFI_STA);
    WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);  // refuse open, WEP and WPA1 networks
    WiFi.setAutoReconnect(true);
    if (!wm.getWiFiIsSaved()) {
        // brand-new clock: the setup hotspot is the only way forward
        wm.setConfigPortalBlocking(false);
        wm.autoConnect(ap_name.c_str(), setup_pw.c_str());
        run_setup_portal(wm, ap_name, setup_pw);
        wm.stopConfigPortal();
        WiFi.softAPdisconnect(true);  // make sure the setup hotspot is gone
        WiFi.mode(WIFI_STA);
    } else {
        // already set up: never block. Show the time and keep reconnecting in the background.
        WiFi.begin();
    }

    start_time_sync();
    // mDNS starts in loop() once Wi-Fi is up
    static const char* hdrs[] = {"X-Overhead", "Authorization"};
    web.collectHeaders(hdrs, 2);
    web.on("/", HTTP_GET, handle_ui);
    web.on("/api/status", HTTP_GET, api_status);
    web.on("/api/settings", HTTP_GET, api_get_settings);
    web.on("/api/settings", HTTP_POST, api_post_settings);
    web.on("/api/locate", HTTP_POST, api_locate);
    web.on("/api/preview", HTTP_POST, api_preview);
    web.on("/api/password", HTTP_POST, api_password);
    web.on("/api/wifi/forget", HTTP_POST, api_forget_wifi);
    web.on("/api/restart", HTTP_POST, api_restart);
    web.on("/api/update", HTTP_POST, api_update_done, api_update_stream);
    web.onNotFound([]() { security_headers(); web.send(404, "text/plain", "Not found"); });
    web.begin();

    String addr = p_addr.getValue();
    addr.trim();
    if (first_setup && addr.length() >= 8 && addr.length() <= 120) {
        geo_request = addr;
        geo_state = GeoState::Pending;
    }
    if (first_setup || (!location_set() && WiFi.status() == WL_CONNECTED))
        show_credentials();
    else
        show_override("HELLO", 2);

    xTaskCreatePinnedToCore(network_task, "net", 16384, nullptr, 1, nullptr, 0);
    // The display runs in its own higher-priority task with the watchdog, so a slow or malicious web
    // client (handled in loop()) can delay the settings page but never the clock.
    esp_task_wdt_init(15, true);
    xTaskCreatePinnedToCore(display_task, "display", 12288, nullptr, 3, nullptr, 1);
}

static void housekeeping(double now) {
    static bool was_connected = false, mdns_up = false;
    static uint32_t last_sensor = 0, last_rtc_write = 0, last_inbound = 0;
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !was_connected && !mdns_up) mdns_up = MDNS.begin(HOSTNAME);  // name only, no service ads
    was_connected = connected;
    // keep the battery-backed RTC in step with internet time (every 6 hours once synced)
    if (have_rtc && time_valid() && connected && (!last_rtc_write || millis() - last_rtc_write > 6 * 3600000UL)) {
        // NTP is unauthenticated: only overwrite the battery clock if it was never set, or the two agree
        // within a day (normal drift); a spoofed time then can't stick across reboots.
        time_t rtc = rtc_read_epoch();
        if (!rtc || fabs(difftime(time(nullptr), rtc)) < 86400) rtc_write_from_system_clock();
        last_rtc_write = millis();
    }
    if (have_sht && millis() - last_sensor > 30000) {
        last_sensor = millis();
        read_indoor();
    }
    if (millis() - last_inbound > 1000) {
        last_inbound = millis();
        LOCK();
        tracker.update_inbound(now);
        UNLOCK();
    }
    chime_tick();
}

// Extra screens between clock minutes, and the evening recap.
static bool rotation_override(double now) {
    static double next_rot = 0;
    static int rot_index = 0, recap_day = -1;
    time_t tt = (time_t)now;
    struct tm lt;
    localtime_r(&tt, &lt);
    if (S.recap && lt.tm_hour == S.recap_hour && lt.tm_min == 0 && recap_day != lt.tm_yday && time_valid()) {
        recap_day = lt.tm_yday;
        LOCK();
        std::string r = tracker.stats.recap();
        UNLOCK();
        show_override(r, 3 + r.size() * 6.0 / S.scroll_speed, S.clock_rgb);
        return true;
    }
    if (!S.rotation || S.mode != 1 || now < next_rot) return false;
    next_rot = now + S.rotate_every_s;
    for (int tries = 0; tries < 6; tries++) {
        int bit = 1 << (rot_index++ % 6);
        if (!(S.rotation & bit)) continue;
        LOCK();
        std::string t = rotation_text(bit, tt);
        UNLOCK();
        if (t.empty()) continue;
        show_override(t, 2.5 + t.size() * 6.0 / S.scroll_speed, S.clock_rgb);
        return true;
    }
    return false;
}

static void render_frame() {
    static uint32_t last_rare_hex = 0;
    read_light();
    housekeeping(now_s());

    {
        uint8_t cols[MATRIX_W] = {0};
        LOCK();
        bool showing = credential_seq.frame(now_s(), cols);
        UNLOCK();
        if (showing) {
            draw_columns(cols, rgb(credential_rgb));
            FastLED.setBrightness((uint8_t)fmaxf(20, brightness_base()));
            FastLED.show();
            return;
        }
    }

    double now = now_s();
    std::string text;
    CRGB color;
    float loud = 0;
    bool active = false, rare = false;
    float inbound = -1;
    LOCK();
    bool overriding = now < override_until, plane_active = tracker.msg.active;
    UNLOCK();
    if (!overriding && !plane_active) rotation_override(now);
    LOCK();
    float speed = 0;
    if (now < override_until) {
        text = override_text;
        speed = override_speed;
        color = rgb(override_rgb);
        loud = 1;
        active = true;
    }
    UNLOCK();
    if (active) {
    } else if (time_valid() && !quiet_now && location_set()) {
        LOCK();
        tracker.tick(now);
        if (tracker.msg.active) {
            text = tracker.msg.text;
            color = CRGB(tracker.msg.r, tracker.msg.g, tracker.msg.b);
            loud = fminf(1.0f, fmaxf(0.0f, (tracker.msg.level - S.on_db) / 20.0f));
            active = true;
            rare = tracker.msg.rare;
            if (rare && tracker.msg.hex != last_rare_hex) { last_rare_hex = tracker.msg.hex; chime(); }
        } else if (S.inbound_countdown && tracker.inbound_s > 0) {
            inbound = tracker.inbound_s;
        }
        UNLOCK();
    }

    float base = brightness_base();
    if (!active && inbound > 0) {  // a plane is about to be audible: plane icon flying toward the countdown
        uint8_t cols[MATRIX_W];
        inbound_frame(inbound, cols);
        draw_columns(cols, rgb(S.plane_rgb));
        FastLED.setBrightness((uint8_t)fmaxf(1, base * 0.8f));
        FastLED.show();
        return;
    }
    if (!active) {
        marquee.set_text("", now);
        if (S.mode == 1 && time_valid()) {  // classic clock between planes (also during quiet hours)
            time_t tt = (time_t)now;
            struct tm lt;
            localtime_r(&tt, &lt);
            uint8_t cols[MATRIX_W];
            clock_frame(lt.tm_hour, lt.tm_min, (tt % 2) == 0, S.clock_24h, cols);
            draw_columns(cols, rgb(S.clock_rgb));
            FastLED.setBrightness((uint8_t)fmaxf(1, base * 0.6f));
            FastLED.show();
        } else {
            FastLED.clear(true);
        }
        return;
    }
    marquee.set_text(text, now, speed);
    uint8_t cols[MATRIX_W];
    marquee.frame(now, cols);
    draw_columns(cols, color);
    if (rare) {  // sparkle: a few white twinkles in the dark pixels around the gold text
        for (int k = 0; k < 4; k++) {
            int x = random(MATRIX_W), y = random(MATRIX_H);
            if (!(cols[x] & (1 << y))) leds[xy(x, y)] = CRGB(90, 90, 70);
        }
    }
    FastLED.setBrightness((uint8_t)fmaxf(1, base * (S.brightness ? 1.0f : 0.55f + 0.45f * loud)));
    FastLED.show();
}

static void display_task(void*) {
    esp_task_wdt_add(nullptr);
    for (;;) {
        esp_task_wdt_reset();
        handle_buttons();
        render_frame();
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

void loop() {
    web.handleClient();  // may block on a slow client; the display task is unaffected
    delay(2);
}
