#include "settings.h"

#include <Preferences.h>
#include <esp_random.h>

#include "overhead.h"

namespace ov {

// Each function opens its own Preferences handle: settings are saved from more than one task.

void settings_load() {
    Preferences prefs;
    prefs.begin("overhead", true);
    S.address = prefs.getString("addr", "").c_str();
    S.home_lat = prefs.getDouble("lat", 0);
    S.home_lon = prefs.getDouble("lon", 0);
    S.home_elev = prefs.getFloat("elev", 900);
    S.airport_choice = prefs.getString("airport", "").c_str();
    S.tz_name = prefs.getString("tz", "UTC").c_str();
    S.tz_posix = prefs.getString("tz_posix", "UTC0").c_str();
    S.brightness = prefs.getInt("bright", 0);
    S.display_format = prefs.getInt("format", 0) % DISPLAY_FORMATS;
    S.rare_alerts = prefs.getBool("rare", true);
    S.rare_chime = prefs.getBool("chime", false);
    S.inbound_countdown = prefs.getBool("inbound", false);
    S.recap = prefs.getBool("recap", false);
    S.recap_hour = prefs.getInt("recap_h", 20);
    S.rotation = prefs.getInt("rotation", 0);
    S.rotate_every_s = prefs.getInt("rot_s", 60);
    S.units_f = prefs.getBool("units_f", true);
    S.temp_offset_c = prefs.getFloat("t_off", -4.0f);
    S.smart_polling = prefs.getBool("smart", true);
    S.mode = prefs.getInt("mode", MODE);
    S.clock_24h = prefs.getInt("h24", CLOCK_24H);
    S.quiet_start = prefs.getInt("q_start", QUIET_START_H);
    S.quiet_end = prefs.getInt("q_end", QUIET_END_H);
    S.clock_rgb = prefs.getUInt("clock_rgb", 0xFFAA6E);
    S.plane_rgb = prefs.getUInt("plane_rgb", 0xFF7800);
    S.color_by_airline = prefs.getInt("color", COLOR_BY_AIRLINE);
    S.bright_min = prefs.getInt("b_min", BRIGHT_MIN);
    S.bright_max = prefs.getInt("b_max", BRIGHT_MAX);
    S.scroll_speed = prefs.getFloat("scroll", SCROLL_PX_PER_S);
    S.on_db = prefs.getFloat("on_db", ON_DB);
    S.off_db = prefs.getFloat("off_db", OFF_DB);
    S.cal_offset = prefs.getFloat("cal", CAL_OFFSET_DB);
    S.daily_budget = prefs.getInt("budget", DAILY_BUDGET);
    prefs.end();
    if (location_set()) resolve_airport();
}

void settings_save() {
    Preferences prefs;
    prefs.begin("overhead", false);
    prefs.putString("addr", S.address.c_str());
    prefs.putDouble("lat", S.home_lat);
    prefs.putDouble("lon", S.home_lon);
    prefs.putFloat("elev", S.home_elev);
    prefs.putString("airport", S.airport_choice.c_str());
    prefs.putString("tz", S.tz_name.c_str());
    prefs.putString("tz_posix", S.tz_posix.c_str());
    prefs.putInt("bright", S.brightness);
    prefs.putInt("format", S.display_format);
    prefs.putBool("rare", S.rare_alerts);
    prefs.putBool("chime", S.rare_chime);
    prefs.putBool("inbound", S.inbound_countdown);
    prefs.putBool("recap", S.recap);
    prefs.putInt("recap_h", S.recap_hour);
    prefs.putInt("rotation", S.rotation);
    prefs.putInt("rot_s", S.rotate_every_s);
    prefs.putBool("units_f", S.units_f);
    prefs.putFloat("t_off", S.temp_offset_c);
    prefs.putBool("smart", S.smart_polling);
    prefs.putInt("mode", S.mode);
    prefs.putInt("h24", S.clock_24h);
    prefs.putInt("q_start", S.quiet_start);
    prefs.putInt("q_end", S.quiet_end);
    prefs.putUInt("clock_rgb", S.clock_rgb);
    prefs.putUInt("plane_rgb", S.plane_rgb);
    prefs.putInt("color", S.color_by_airline);
    prefs.putInt("b_min", S.bright_min);
    prefs.putInt("b_max", S.bright_max);
    prefs.putFloat("scroll", S.scroll_speed);
    prefs.putFloat("on_db", S.on_db);
    prefs.putFloat("off_db", S.off_db);
    prefs.putFloat("cal", S.cal_offset);
    prefs.putInt("budget", S.daily_budget);
    prefs.end();
}

String rgb_to_hex(uint32_t rgb) {
    char b[8];
    snprintf(b, sizeof(b), "#%06X", (unsigned)(rgb & 0xFFFFFF));
    return String(b);
}

bool hex_to_rgb(const char* s, uint32_t& out) {
    if (!s || s[0] != '#' || strlen(s) != 7) return false;
    for (int i = 1; i < 7; i++)
        if (!isxdigit((unsigned char)s[i])) return false;
    out = strtoul(s + 1, nullptr, 16);
    return true;
}

void settings_to_json(JsonObject o) {
    o["mode"] = S.mode ? "clock" : "plane";
    o["clock_24h"] = (bool)S.clock_24h;
    o["quiet_start"] = S.quiet_start;
    o["quiet_end"] = S.quiet_end;
    o["clock_color"] = rgb_to_hex(S.clock_rgb);
    o["plane_color"] = rgb_to_hex(S.plane_rgb);
    o["airline_colors"] = (bool)S.color_by_airline;
    o["bright_min"] = S.bright_min;
    o["bright_max"] = S.bright_max;
    static const char* levels[] = {"auto", "low", "medium", "high"};
    o["brightness"] = levels[S.brightness & 3];
    o["airport"] = S.airport_choice.empty() ? "auto" : S.airport_choice.c_str();
    o["timezone"] = S.tz_name.c_str();
    static const char* formats[] = {"city", "codes", "airline", "full"};
    o["display"] = formats[S.display_format % DISPLAY_FORMATS];
    o["rare_alerts"] = S.rare_alerts;
    o["rare_chime"] = S.rare_chime;
    o["inbound_countdown"] = S.inbound_countdown;
    o["recap"] = S.recap;
    o["recap_hour"] = S.recap_hour;
    JsonArray rot = o["rotation"].to<JsonArray>();
    static const char* rot_names[] = {"indoor", "outdoor", "date", "sun", "flow", "today"};
    for (int i = 0; i < 6; i++)
        if (S.rotation & (1 << i)) rot.add(rot_names[i]);
    o["rotate_every_s"] = S.rotate_every_s;
    o["units"] = S.units_f ? "F" : "C";
    o["temp_offset_c"] = S.temp_offset_c;
    o["smart_polling"] = S.smart_polling;
    o["scroll_speed"] = S.scroll_speed;
    o["on_db"] = S.on_db;
    o["off_db"] = S.off_db;
    o["cal_offset"] = S.cal_offset;
    o["daily_budget"] = S.daily_budget;
}

String settings_apply_json(JsonObjectConst in) {
    auto num = [&](const char* k, float lo, float hi, float& dst) -> bool {
        if (!in[k].is<float>()) return false;
        dst = constrain(in[k].as<float>(), lo, hi);
        return true;
    };
    float f;
    if (in["mode"].is<const char*>()) {
        String m = in["mode"].as<const char*>();
        if (m != "clock" && m != "plane") return "mode must be clock or plane";
        S.mode = m == "clock";
    }
    if (in["clock_24h"].is<bool>()) S.clock_24h = in["clock_24h"].as<bool>();
    if (num("quiet_start", 0, 23, f)) S.quiet_start = (int)f;
    if (num("quiet_end", 0, 23, f)) S.quiet_end = (int)f;
    uint32_t rgb;
    if (in["clock_color"].is<const char*>()) {
        if (!hex_to_rgb(in["clock_color"], rgb)) return "clock_color must look like #FFAA6E";
        S.clock_rgb = rgb;
    }
    if (in["plane_color"].is<const char*>()) {
        if (!hex_to_rgb(in["plane_color"], rgb)) return "plane_color must look like #FF7800";
        S.plane_rgb = rgb;
    }
    if (in["airline_colors"].is<bool>()) S.color_by_airline = in["airline_colors"].as<bool>();
    if (num("bright_min", 1, 255, f)) S.bright_min = (int)f;
    if (num("bright_max", 1, 255, f)) S.bright_max = (int)f;
    if (S.bright_max < S.bright_min) S.bright_max = S.bright_min;
    if (in["brightness"].is<const char*>()) {
        String v = in["brightness"].as<const char*>();
        if (v == "auto") S.brightness = 0;
        else if (v == "low") S.brightness = 1;
        else if (v == "medium") S.brightness = 2;
        else if (v == "high") S.brightness = 3;
        else return "brightness must be auto, low, medium or high";
    }
    if (in["airport"].is<const char*>()) {
        String v = in["airport"].as<const char*>();
        v.toUpperCase();
        std::string prev = S.airport_choice;
        S.airport_choice = (v == "AUTO" || v.length() == 0) ? "" : v.c_str();
        if (!S.airport_choice.empty() && (v.length() != 3 || !resolve_airport() || S.airport_iata != S.airport_choice)) {
            S.airport_choice = prev;
            resolve_airport();
            return "unknown airport code";
        }
        if (location_set()) resolve_airport();
    }
    if (in["display"].is<const char*>()) {
        String v = in["display"].as<const char*>();
        if (v == "city") S.display_format = 0;
        else if (v == "codes") S.display_format = 1;
        else if (v == "airline") S.display_format = 2;
        else if (v == "full") S.display_format = 3;
        else return "display must be city, codes, airline or full";
    }
    auto flag = [&](const char* k, bool& dst) { if (in[k].is<bool>()) dst = in[k].as<bool>(); };
    flag("rare_alerts", S.rare_alerts);
    flag("rare_chime", S.rare_chime);
    flag("inbound_countdown", S.inbound_countdown);
    flag("recap", S.recap);
    flag("smart_polling", S.smart_polling);
    if (num("recap_hour", 0, 23, f)) S.recap_hour = (int)f;
    if (num("rotate_every_s", 20, 600, f)) S.rotate_every_s = (int)f;
    if (num("temp_offset_c", -15, 15, f)) S.temp_offset_c = f;
    if (in["units"].is<const char*>()) S.units_f = String(in["units"].as<const char*>()) != "C";
    if (in["rotation"].is<JsonArrayConst>()) {
        static const char* rot_names[] = {"indoor", "outdoor", "date", "sun", "flow", "today"};
        int bits = 0;
        for (JsonVariantConst v : in["rotation"].as<JsonArrayConst>()) {
            const char* n = v | "";
            for (int i = 0; i < 6; i++)
                if (!strcmp(n, rot_names[i])) bits |= 1 << i;
        }
        S.rotation = bits;
    }
    if (in["timezone"].is<const char*>()) {
        const char* posix = posix_for_zone(in["timezone"].as<const char*>());
        if (!posix) return "unknown time zone";
        S.tz_name = in["timezone"].as<const char*>();
        S.tz_posix = posix;
    }
    if (num("scroll_speed", 5, 60, f)) S.scroll_speed = f;
    if (num("on_db", 40, 90, f)) S.on_db = f;
    if (num("off_db", 35, 90, f)) S.off_db = f;
    if (S.off_db > S.on_db - 1) S.off_db = S.on_db - 1;
    if (num("cal_offset", -20, 20, f)) S.cal_offset = f;
    if (num("daily_budget", 50, 480, f)) S.daily_budget = (int)f;
    settings_save();
    return "";
}

// ---------------------------------------------------------------- secrets
String secret_setup_password() {
    Preferences prefs;
    prefs.begin("secrets", false);
    String pw = prefs.getString("setup_pw", "");
    if (pw.length() < 12) {
        // 12 characters from 31 unambiguous symbols: about 59 bits, shown only on the clock's display
        static const char* alphabet = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";  // no 0/O, 1/I/L
        pw = "";
        for (int i = 0; i < 12; i++) pw += alphabet[esp_random() % 31];
        prefs.putString("setup_pw", pw);
    }
    prefs.end();
    return pw;
}

String secret_admin_password() {
    Preferences prefs;
    prefs.begin("secrets", true);
    String pw = prefs.getString("admin_pw", "");
    prefs.end();
    return pw.length() >= 8 ? pw : secret_setup_password();
}

void secret_set_admin_password(const String& pw) {
    Preferences prefs;
    prefs.begin("secrets", false);
    prefs.putString("admin_pw", pw);
    prefs.end();
}

}  // namespace ov
