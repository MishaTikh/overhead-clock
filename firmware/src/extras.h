// Extras around the main feature: rare aircraft, daily stats, wind/runway flow, sunrise/sunset,
// learned busy hours. Platform independent (host-tested like overhead.cpp).
#pragma once
#include <stdint.h>
#include <time.h>
#include <string>
#include <vector>

namespace ov {

// ---------------------------------------------------------------- rare aircraft
// Returns a short label ("747", "A380", "MILITARY", "HELICOPTER", "VINTAGE", ...) or "" if ordinary.
// db_flags is readsb's dbFlags (bit 0 = military in the aircraft database).
std::string rare_label(const char* type, const char* callsign, const char* category, int db_flags);

// ---------------------------------------------------------------- airports
struct AirportInfo { std::string iata, icao, city; double lat, lon; };
bool airport_by_iata(const std::string& iata, AirportInfo& out);

// ---------------------------------------------------------------- daily stats
struct DayStats {
    int yday = -1;            // day these stats belong to
    int planes = 0;           // audible passes today
    int arrivals = 0, departures = 0;
    std::string farthest_city; double farthest_nm = 0;
    std::string loudest_type, loudest_cs; float loudest_db = 0;
    std::vector<std::string> rare;   // e.g. "747 DAL164 14:05" (newest first, up to 10)
    int by_hour[24] = {0};
    void reset(int day);
    // other_iata: the route's far end (origin if landing, destination if departing), "" if unknown.
    void add_pass(int yday, int hour, const char* cs, const char* type, const std::string& other_iata,
                  const std::string& other_city, bool departing, float peak_db, const std::string& rare_lbl,
                  const char* hhmm);
    std::string recap() const;   // "142 PLANES TODAY  FARTHEST TOKYO  LOUDEST A330"
};

// ---------------------------------------------------------------- learned busy hours
// Average audible passes per hour of day, learned slowly (exponential average per day).
struct HourModel {
    float avg[24] = {0};
    int days = 0;
    void end_of_day(const int counts[24]);
    // true once a week of history says this hour almost never brings planes over the house
    bool usually_quiet(int hour) const { return days >= 7 && avg[hour] < 0.15f; }
};

// ---------------------------------------------------------------- airport weather and flow
struct Weather {
    bool ok = false;
    time_t obs = 0;
    float temp_c = 0, dewp_c = 0;
    int wind_dir = -1;        // degrees true the wind blows FROM; -1 = variable
    int wind_kt = 0, gust_kt = 0;
    std::string cover, raw;   // "CLR", "BKN", ...; raw METAR text
};

enum class Flow { Unknown, Calm, ArrivalsOverYou, DeparturesOverYou, AwayFromYou };
struct FlowInfo { Flow flow = Flow::Unknown; std::string runway; };
// Which way the airport is likely landing given the wind, and whether that brings planes over the house.
FlowInfo flow_for(const std::string& airport_iata, double home_lat, double home_lon, const Weather& w);
const char* flow_text(Flow f);   // short phrase for the sign / dashboard

// Calendar fields read as UTC -> epoch seconds (timegm(), which the ESP32's C library lacks).
time_t utc_from_tm(const struct tm& t);

// ---------------------------------------------------------------- sun
// Local sunrise and sunset (minutes after local midnight) for the date of `t`; false in polar day/night.
bool sun_times(time_t t, double lat, double lon, int& rise_min, int& set_min);

}  // namespace ov
