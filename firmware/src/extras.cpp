#include "extras.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "airports_data.h"
#include "overhead.h"

namespace ov {

static const double NM = 6076.12;

// ================================================================ rare aircraft
std::string rare_label(const char* type, const char* cs, const char* cat, int db_flags) {
    struct T { const char* code; const char* label; };
    static const T types[] = {
        {"B741", "747"}, {"B742", "747"}, {"B743", "747"}, {"B744", "747"}, {"B748", "747"}, {"B74S", "747"}, {"B74R", "747"},
        {"A388", "A380"}, {"A124", "ANTONOV"}, {"A225", "ANTONOV"}, {"BLCF", "DREAMLIFTER"}, {"A3ST", "BELUGA"}, {"A337", "BELUGA"},
        {"B788", "DREAMLINER"}, {"B789", "DREAMLINER"}, {"B78X", "DREAMLINER"},
        {"A359", "A350"}, {"A35K", "A350"}, {"B77W", "777"}, {"B77L", "777"}, {"B772", "777"}, {"B773", "777"},
        {"B778", "777X"}, {"B779", "777X"}, {"MD11", "MD-11"}, {"DC10", "DC-10"}, {"CONC", "CONCORDE"},
        {"C17", "MILITARY"}, {"C5M", "MILITARY"}, {"C130", "MILITARY"}, {"C30J", "MILITARY"}, {"KC46", "MILITARY"},
        {"K35R", "MILITARY"}, {"E3TF", "MILITARY"}, {"B52", "MILITARY"}, {"V22", "OSPREY"},
        {"DC3", "VINTAGE"}, {"C47", "VINTAGE"}, {"B17", "VINTAGE"}, {"B29", "VINTAGE"}, {"P51", "VINTAGE"},
        {"SPIT", "VINTAGE"}, {"DC6", "VINTAGE"}, {"CONI", "VINTAGE"}, {"JU52", "VINTAGE"}, {"T6", "VINTAGE"},
    };
    if (cs && (!strncmp(cs, "SAM", 3) || !strcmp(cs, "AF1") || !strcmp(cs, "AF2"))) return "VIP";
    if (type && type[0])
        for (auto& t : types)
            if (!strcmp(type, t.code)) return t.label;
    if (db_flags & 1) return "MILITARY";
    if (cat && !strcmp(cat, "A7")) return "HELICOPTER";
    return "";
}

// ================================================================ airports
bool airport_by_iata(const std::string& iata, AirportInfo& out) {
    if (iata.size() != 3) return false;
    for (int i = 0; i < AIRPORT_COUNT; i++)
        if (iata == AIRPORTS[i].iata) {
            out = {AIRPORTS[i].iata, AIRPORTS[i].icao, AIRPORTS[i].city, AIRPORTS[i].lat, AIRPORTS[i].lon};
            return true;
        }
    return false;
}

// ================================================================ daily stats
void DayStats::reset(int day) {
    *this = DayStats();
    yday = day;
}

void DayStats::add_pass(int day, int hour, const char* cs, const char* type, const std::string& other_iata,
                        const std::string& other_city, bool departing, float peak_db, const std::string& rare_lbl,
                        const char* hhmm) {
    if (day != yday) reset(day);
    if (!other_iata.empty() && std::find(cities.begin(), cities.end(), other_iata) == cities.end()) {
        if (cities.size() >= 100) cities.erase(cities.begin());
        cities.push_back(other_iata);
    }
    planes++;
    (departing ? departures : arrivals)++;
    if (hour >= 0 && hour < 24) by_hour[hour]++;
    AirportInfo far, home;
    if (!other_iata.empty() && airport_by_iata(other_iata, far) && airport_by_iata(S.airport_iata, home)) {
        double d, brg;
        dist_bearing_ft(home.lat, home.lon, far.lat, far.lon, d, brg);
        if (d / NM > farthest_nm) { farthest_nm = d / NM; farthest_city = other_city.empty() ? far.city : other_city; }
    }
    if (peak_db > loudest_db) { loudest_db = peak_db; loudest_type = type ? type : ""; loudest_cs = cs ? cs : ""; }
    if (!rare_lbl.empty()) {
        rare.insert(rare.begin(), rare_lbl + " " + (cs ? cs : "") + " " + (hhmm ? hhmm : ""));
        if (rare.size() > 10) rare.pop_back();
    }
}

std::string DayStats::recap() const {
    char b[160];
    std::string s = std::to_string(planes) + (planes == 1 ? " PLANE TODAY" : " PLANES TODAY");
    if (!farthest_city.empty()) {
        snprintf(b, sizeof(b), "  FARTHEST %s %d MI", farthest_city.c_str(), (int)(farthest_nm * 1.15078));
        s += b;
    }
    if (!loudest_type.empty()) s += "  LOUDEST " + loudest_type;
    if (!rare.empty()) s += "  RARE: " + rare.front().substr(0, rare.front().find(' '));
    for (auto& c : s) c = toupper((unsigned char)c);
    return s;
}

// ================================================================ learned busy hours
void HourModel::end_of_day(const int counts[24]) {
    float a = days < 7 ? 1.0f / (days + 1) : 1.0f / 7;  // plain average for a week, then a 7-day moving average
    for (int h = 0; h < 24; h++) avg[h] += a * (counts[h] - avg[h]);
    days++;
}

// ================================================================ flow
const char* flow_text(Flow f) {
    switch (f) {
        case Flow::ArrivalsOverYou: return "LANDINGS OVER YOU";
        case Flow::DeparturesOverYou: return "TAKEOFFS OVER YOU";
        case Flow::AwayFromYou: return "PLANES AWAY FROM YOU";
        case Flow::Calm: return "CALM WIND";
        default: return "";
    }
}

static double ang_diff(double a, double b) { return fabs(fmod(a - b + 540.0, 360.0) - 180.0); }

// Distance along and across a line that starts at (lat0, lon0) and heads `brg`.
static void along_cross(double lat0, double lon0, double brg, double lat, double lon, double& along_nm, double& cross_nm) {
    double d, b;
    dist_bearing_ft(lat0, lon0, lat, lon, d, b);
    double a = (b - brg) * 3.14159265358979 / 180.0;
    along_nm = d * cos(a) / NM;
    cross_nm = d * sin(a) / NM;
}

FlowInfo flow_for(const std::string& iata, double hlat, double hlon, const Weather& w) {
    FlowInfo out;
    if (!w.ok) return out;
    int first = -1, count = 0;
    for (int i = 0; i < AIRPORT_COUNT; i++)
        if (iata == AIRPORTS[i].iata) { first = AIRPORTS[i].rwy_first; count = AIRPORTS[i].rwy_count; break; }
    if (first < 0 || count == 0) return out;
    if (w.wind_dir < 0 || w.wind_kt < 5) { out.flow = Flow::Calm; return out; }  // the airport could use either direction
    out.flow = Flow::AwayFromYou;
    double best_arr = 1e9, best_dep = 1e9;  // with parallel runways, the centerline closest to the house wins
    std::string arr_rwy, dep_rwy;
    for (int i = first; i < first + count; i++) {
        const RunwayRec& r = RUNWAYS[i];
        for (int dir = 0; dir < 2; dir++) {
            // landing on the threshold (tlat, tlon), rolling toward the far end (flat, flon)
            double tlat = dir ? r.he_lat : r.le_lat, tlon = dir ? r.he_lon : r.le_lon;
            double flat = dir ? r.le_lat : r.he_lat, flon = dir ? r.le_lon : r.he_lon;
            double d, heading;
            dist_bearing_ft(tlat, tlon, flat, flon, d, heading);
            if (ang_diff(heading, w.wind_dir) > 60) continue;  // not into the wind: not in use
            double along, cross;
            along_cross(tlat, tlon, fmod(heading + 180.0, 360.0), hlat, hlon, along, cross);  // approach side
            if (along > 0.3 && along < 15 && fabs(cross) < 1.2 && fabs(cross) < best_arr) {
                best_arr = fabs(cross);
                arr_rwy = dir ? r.he : r.le;
            }
            along_cross(flat, flon, heading, hlat, hlon, along, cross);  // beyond the far end, climbing out
            if (along > 0.3 && along < 10 && fabs(cross) < 1.5 && fabs(cross) < best_dep) {
                best_dep = fabs(cross);
                dep_rwy = dir ? r.he : r.le;
            }
        }
    }
    if (!arr_rwy.empty()) { out.flow = Flow::ArrivalsOverYou; out.runway = arr_rwy; }
    else if (!dep_rwy.empty()) { out.flow = Flow::DeparturesOverYou; out.runway = dep_rwy; }
    return out;
}

time_t utc_from_tm(const struct tm& t) {
    // days from civil (Howard Hinnant's algorithm)
    int y = t.tm_year + 1900, m = t.tm_mon + 1, dd = t.tm_mday;
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468;
    return (time_t)(days * 86400L + t.tm_hour * 3600L + t.tm_min * 60L + t.tm_sec);
}

// ================================================================ sun (NOAA approximation)
bool sun_times(time_t t, double lat, double lon, int& rise_min, int& set_min) {
    struct tm lt;
    localtime_r(&t, &lt);
    struct tm noon = lt;
    noon.tm_hour = 12; noon.tm_min = 0; noon.tm_sec = 0;
    time_t tn = mktime(&noon);
    struct tm ln;
    localtime_r(&tn, &ln);
    double tz_off_min = difftime(utc_from_tm(ln), tn) / 60.0;  // local minus UTC
    int doy = lt.tm_yday + 1;
    const double D = 3.14159265358979 / 180.0;
    double gam = 2 * 3.14159265358979 / 365.0 * (doy - 1);
    double eqt = 229.18 * (0.000075 + 0.001868 * cos(gam) - 0.032077 * sin(gam) - 0.014615 * cos(2 * gam) - 0.040849 * sin(2 * gam));
    double decl = 0.006918 - 0.399912 * cos(gam) + 0.070257 * sin(gam) - 0.006758 * cos(2 * gam) + 0.000907 * sin(2 * gam) -
                  0.002697 * cos(3 * gam) + 0.00148 * sin(3 * gam);
    double c = cos(90.833 * D) / (cos(lat * D) * cos(decl)) - tan(lat * D) * tan(decl);
    if (c < -1 || c > 1) return false;
    double ha = acos(c) / D;
    double noon_utc = 720 - 4 * lon - eqt;
    rise_min = (int)lround(noon_utc - 4 * ha + tz_off_min);
    set_min = (int)lround(noon_utc + 4 * ha + tz_off_min);
    rise_min = (rise_min % 1440 + 1440) % 1440;
    set_min = (set_min % 1440 + 1440) % 1440;
    return true;
}

}  // namespace ov
