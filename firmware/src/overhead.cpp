#include "overhead.h"

#include "airports_data.h"
#include "timezones_data.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

namespace ov {

Settings S;

static const double EARTH_R_FT = 20902231.0;
static const double NM_FT = 6076.12;
static const double KT_TO_FPS = 1.68781;
static const double D2R = 3.14159265358979323846 / 180.0;

// ================================================================ geometry
void dist_bearing_ft(double lat1, double lon1, double lat2, double lon2, double& dist, double& brg) {
    double p1 = lat1 * D2R, p2 = lat2 * D2R, dl = (lon2 - lon1) * D2R;
    double a = sin((p2 - p1) / 2) * sin((p2 - p1) / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    dist = 2 * EARTH_R_FT * asin(fmin(1.0, sqrt(a)));
    double y = sin(dl) * cos(p2), x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    brg = fmod(atan2(y, x) / D2R + 360.0, 360.0);
}

void move(double lat, double lon, double brg_deg, double dist_ft, double& olat, double& olon) {
    double d = dist_ft / EARTH_R_FT, b = brg_deg * D2R, p1 = lat * D2R, l1 = lon * D2R;
    double p2 = asin(sin(p1) * cos(d) + cos(p1) * sin(d) * cos(b));
    double l2 = l1 + atan2(sin(b) * sin(d) * cos(p1), cos(d) - sin(p1) * sin(p2));
    olat = p2 / D2R;
    olon = l2 / D2R;
}

// ================================================================ acoustics
// Many aircraft send no size category (older transponders), but the feed knows the type: use that to
// pick the right class. Prefixes of ICAO type codes.
static char category_from_type(const char* type) {
    if (!type || !type[0]) return 0;
    static const char* heavy[] = {"A33", "A34", "A35", "A38", "B74", "B76", "B77", "B78", "MD1", "DC1", "A3ST", "K35", "IL"};  // no "C17": it would catch Cessna 172s
    static const char* b757[] = {"B75"};
    static const char* medium[] = {"A19", "A20", "A21", "A31", "A32", "B72", "B73", "B38", "B39", "E17", "E19", "E29", "E75", "E90",
                                   "E95", "CRJ", "BCS", "MD8", "MD9", "DH8", "AT4", "AT7", "DC9", "SB20", "RJ"};
    static const char* small[] = {"C25", "C50", "C55", "C56", "C68", "C70", "C75", "CL3", "CL6", "GLF", "GL", "GA", "E35", "E50",
                                  "E55", "LJ", "H25", "F2T", "F90", "FA", "PC12", "PC24", "B350", "BE20", "BE30", "BE40", "BE9", "SW", "SF34", "C130", "C30"};
    static const char* light[] = {"C1", "C2", "P28", "PA", "SR2", "BE3", "BE5", "BE6", "BE7", "BE8", "M20", "DA", "RV", "G", "AA5", "AC", "CH7", "PIPA", "TB"};
    static const char* heli[] = {"R22", "R44", "R66", "EC", "AS3", "AS5", "B06", "B407", "B429", "B412", "H60", "H53", "UH1", "S76", "S92", "A109", "A139", "AW", "BK17", "H47", "EH10", "MD50", "MD52", "MD60"};
    struct Group { const char** list; int n; char cat; };
    Group groups[] = {
        {heli, (int)(sizeof(heli) / sizeof(*heli)), '7'}, {heavy, (int)(sizeof(heavy) / sizeof(*heavy)), '5'}, {b757, 1, '4'},
        {medium, (int)(sizeof(medium) / sizeof(*medium)), '3'}, {small, (int)(sizeof(small) / sizeof(*small)), '2'},
        {light, (int)(sizeof(light) / sizeof(*light)), '1'}};
    for (auto& g : groups)
        for (int i = 0; i < g.n; i++)
            if (!strncmp(type, g.list[i], strlen(g.list[i]))) return g.cat;
    return 0;
}

static float source_level(const char* cat, bool departing, const char* type = nullptr) {
    float base = SRC_UNK;
    char guessed[3] = {'A', category_from_type(type), 0};
    if (!(cat && cat[0] == 'A' && cat[1] >= '1' && cat[1] <= '7') && guessed[1]) cat = guessed;
    if (cat && cat[0] == 'A') {
        switch (cat[1]) {
            case '1': base = SRC_A1; break;
            case '2': base = SRC_A2; break;
            case '3': base = SRC_A3; break;
            case '4': base = SRC_A4; break;
            case '5': base = SRC_A5; break;
            case '6': base = SRC_A4; break;
            case '7': base = SRC_A7; break;
        }
    }
    return base + (departing ? DEPARTURE_BOOST_DB : 0.0f);
}

static float ground_effect(float elev_deg) {
    if (elev_deg <= GROUND_FULL_DEG) return GROUND_MAX_DB;
    if (elev_deg >= GROUND_ZERO_DEG) return 0;
    return GROUND_MAX_DB * (GROUND_ZERO_DEG - elev_deg) / (GROUND_ZERO_DEG - GROUND_FULL_DEG);
}

float level_at(const char* category, const char* type, bool departing, float horiz_ft, float height_ft) {
    height_ft = fmaxf(height_ft, 50.0f);
    float r = fmaxf(hypotf(horiz_ft, height_ft), 100.0f);
    float elev = atan2f(height_ft, horiz_ft) / (float)D2R;
    return source_level(category, departing, type) - 20.0f * log10f(r / 1000.0f)
           - ABSORB_DB_PER_KFT * (r - 1000.0f) / 1000.0f - ground_effect(elev) + S.cal_offset;
}

std::vector<NearbyAirport> nearest_airports(double lat, double lon, int n, double max_nm) {
    std::vector<NearbyAirport> out;
    for (int i = 0; i < AIRPORT_COUNT; i++) {
        const AirportRec& a = AIRPORTS[i];
        if (fabs(a.lat - lat) > max_nm / 60.0 + 0.1) continue;  // cheap latitude prefilter
        double d, brg;
        dist_bearing_ft(lat, lon, a.lat, a.lon, d, brg);
        d /= NM_FT;
        if (d > max_nm) continue;
        out.push_back({a.iata, a.city, a.lat, a.lon, d, a.large});
    }
    std::sort(out.begin(), out.end(), [](const NearbyAirport& x, const NearbyAirport& y) { return x.dist_nm < y.dist_nm; });
    if ((int)out.size() > n) out.resize(n);
    return out;
}

bool resolve_airport() {
    if (!S.airport_choice.empty()) {
        for (int i = 0; i < AIRPORT_COUNT; i++)
            if (S.airport_choice == AIRPORTS[i].iata) {
                S.airport_iata = AIRPORTS[i].iata; S.airport_city = AIRPORTS[i].city;
                S.airport_lat = AIRPORTS[i].lat; S.airport_lon = AIRPORTS[i].lon;
                return true;
            }
    }
    auto near = nearest_airports(S.home_lat, S.home_lon, 1, 60);
    if (near.empty()) { S.airport_iata.clear(); S.airport_city.clear(); S.airport_lat = S.airport_lon = 0; return false; }
    S.airport_iata = near[0].iata; S.airport_city = near[0].city;
    S.airport_lat = near[0].lat; S.airport_lon = near[0].lon;
    return true;
}

const char* posix_for_zone(const std::string& iana) {
    for (int i = 0; i < TIMEZONE_COUNT; i++)
        if (iana == TIMEZONES[i].name) return TIMEZONES[i].posix;
    return nullptr;
}

bool is_quiet(int h) {
    int a = S.quiet_start, b = S.quiet_end;
    if (a == b) return false;
    return a < b ? (h >= a && h < b) : (h >= a || h < b);
}

// ================================================================ routes
RouteEntry* RouteCache::get(const std::string& cs, bool departing, double now) {
    for (size_t i = 0; i < entries_.size(); i++) {
        RouteEntry& e = entries_[i];
        if (e.cs == cs && e.departing == departing) {
            double ttl = e.state == RouteState::Found ? 12 * 3600 : 2 * 3600;  // flights repeat daily; misses retried later
            if (e.state != RouteState::Pending && now - e.t > ttl) {
                e.state = RouteState::Pending;
                e.t = now;
                queue_.push_back(i);
            }
            return &e;
        }
    }
    if (entries_.size() >= 48) {  // drop the oldest non-pending entry
        size_t old = 0;
        for (size_t i = 1; i < entries_.size(); i++)
            if (entries_[i].t < entries_[old].t && entries_[i].state != RouteState::Pending) old = i;
        entries_.erase(entries_.begin() + old);
        queue_.clear();
        for (size_t i = 0; i < entries_.size(); i++)
            if (entries_[i].state == RouteState::Pending) queue_.push_back(i);
    }
    RouteEntry e;
    e.cs = cs;
    e.departing = departing;
    e.state = RouteState::Pending;
    e.t = now;
    entries_.push_back(e);
    queue_.push_back(entries_.size() - 1);
    return &entries_.back();
}

RouteEntry* RouteCache::next_request() {
    while (!queue_.empty()) {
        size_t i = queue_.front();
        queue_.erase(queue_.begin());
        if (i < entries_.size() && entries_[i].state == RouteState::Pending) return &entries_[i];
    }
    return nullptr;
}

void RouteCache::complete(const std::string& cs, bool departing, bool found, const Route& r, double now) {
    for (auto& e : entries_)
        if (e.cs == cs && e.departing == departing) {
            e.state = found ? RouteState::Found : RouteState::NotFound;
            e.route = r;
            e.t = now;
        }
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static std::string decode_entities(std::string s) {
    static const char* from[] = {"&amp;", "&#039;", "&#39;", "&apos;", "&nbsp;", "&quot;"};
    static const char* to[] = {"&", "'", "'", "'", " ", "\""};
    for (int k = 0; k < 6; k++) {
        size_t p;
        while ((p = s.find(from[k])) != std::string::npos) s.replace(p, strlen(from[k]), to[k]);
    }
    return s;
}

std::string clean_city(const std::string& raw) {
    std::string c = trim(decode_entities(raw));
    // "Cincinnati (CVG)" -> "Cincinnati"
    if (c.size() >= 6 && c.back() == ')' && c[c.size() - 5] == '(') c = trim(c.substr(0, c.size() - 5));
    // "New York-JFK" -> "New York"
    if (c.size() > 4 && c[c.size() - 4] == '-' && isupper(c[c.size() - 1]) && isupper(c[c.size() - 2]) && isupper(c[c.size() - 3]))
        c = c.substr(0, c.size() - 4);
    size_t p = c.find(',');
    if (p != std::string::npos) c = c.substr(0, p);        // "Columbus, OH"
    p = c.find(" - ");
    if (p != std::string::npos) c = c.substr(0, p);        // "Houston - Bush"
    return trim(c);
}

const char* marketing_codes(const std::string& op) {
    static const char* map[][2] = {
        {"DAL", "DL"}, {"EDV", "DL"}, {"SKW", "DL,UA,AA,AS"}, {"RPA", "DL,UA,AA"}, {"GJS", "UA,DL"},
        {"CPZ", "DL,AA"}, {"ASH", "UA,AA"}, {"UAL", "UA"}, {"AAL", "AA"}, {"ENY", "AA"}, {"PDT", "AA"},
        {"JIA", "AA"}, {"SWA", "WN"}, {"SCX", "SY"}, {"FFT", "F9"}, {"ASA", "AS"}, {"QXE", "AS"},
        {"ACA", "AC"}, {"JZA", "AC"}, {"ROU", "AC"}, {"KLM", "KL"}, {"AFR", "AF"}, {"ICE", "FI"},
        {"EIN", "EI"}, {"WJA", "WS"}, {"OCN", "DE"}, {"JBU", "B6"}, {"NKS", "NK"}, {"DJT", "DJ"},
        {"UCA", "UA"}, {"AWI", "UA"}, {"GLA", "UA"}, {"KAP", "K5"},
    };
    for (auto& m : map)
        if (op == m[0]) return m[1];
    return nullptr;
}

bool split_callsign(const std::string& cs, std::string& op, std::string& num) {
    if (cs.size() < 4 || cs.size() > 8) return false;
    for (int i = 0; i < 3; i++)
        if (!isupper((unsigned char)cs[i])) return false;
    size_t i = 3;
    while (i < cs.size() && isdigit((unsigned char)cs[i])) i++;
    if (i == 3 || i - 3 > 4) return false;
    if (i < cs.size() && !(i + 1 == cs.size() && isupper((unsigned char)cs[i]))) return false;
    op = cs.substr(0, 3);
    num = cs.substr(3, i - 3);
    while (num.size() > 1 && num[0] == '0') num.erase(0, 1);
    return true;
}

// ---------------------------------------------------------------- board HTML
void BoardParser::reset() {
    rows.clear();
    in_tbody_ = in_td_ = in_tag_ = done_ = false;
    tag_.clear();
    cell_.clear();
    row_.clear();
}

void BoardParser::feed(char c) {
    if (done_) return;
    if (in_tag_) {
        if (c == '>') {
            in_tag_ = false;
            std::string t = tag_;
            for (auto& ch : t) ch = tolower(ch);
            auto is = [&](const char* name) {
                size_t n = strlen(name);
                return t.compare(0, n, name) == 0 && (t.size() == n || t[n] == ' ' || t[n] == '\t' || t[n] == '\n');
            };
            if (is("tbody")) in_tbody_ = true;
            else if (is("/tbody")) { if (in_tbody_) done_ = true; }
            else if (in_tbody_ && is("tr")) row_.clear();
            else if (in_tbody_ && is("/tr")) { if (!row_.empty() && rows.size() < 12) rows.push_back(row_); row_.clear(); }
            else if (in_tbody_ && is("td")) { in_td_ = true; cell_.clear(); }
            else if (in_tbody_ && is("/td")) { in_td_ = false; if (row_.size() < 8) row_.push_back(trim(cell_)); }
        } else if (tag_.size() < 24) {
            tag_ += c;
        }
        return;
    }
    if (c == '<') {
        in_tag_ = true;
        tag_.clear();
        return;
    }
    if (in_td_ && cell_.size() < 80) cell_ += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
}

static int month_index(const char* s) {
    static const char* m = "JanFebMarAprMayJunJulAugSepOctNovDec";
    for (int i = 0; i < 12; i++)
        if (strncmp(s, m + 3 * i, 3) == 0) return i;
    return -1;
}

// "Oct 18 — 9:04 p.m." -> minutes difference from now (absolute), or -1
static double schedule_offset_s(const std::string& when, const struct tm& now) {
    if (when.size() < 6) return -1;
    int mon = month_index(when.c_str());
    if (mon < 0) return -1;
    int day = atoi(when.c_str() + 4);
    size_t colon = when.find(':');
    if (colon == std::string::npos || colon < 1) return -1;
    size_t hs = colon;
    while (hs > 0 && isdigit((unsigned char)when[hs - 1])) hs--;
    int hour = atoi(when.c_str() + hs), minute = atoi(when.c_str() + colon + 1);
    bool pm = when.find("p.m.") != std::string::npos, am = when.find("a.m.") != std::string::npos;
    if (!pm && !am) return -1;
    hour = hour % 12 + (pm ? 12 : 0);
    struct tm t = now;
    t.tm_mon = mon;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    struct tm n = now;
    time_t a = mktime(&t), b = mktime(&n);
    return fabs(difftime(a, b));
}

bool pick_board_row(const std::vector<std::vector<std::string>>& rows, const std::string& cs,
                    bool departing, const struct tm& now_local, Route& r) {
    std::string op, num;
    if (!split_callsign(cs, op, num)) return false;
    const char* codes = marketing_codes(op);
    if (!codes) return false;
    std::string list = codes;
    int best_rank = 99;
    double best_off = 1e18;
    bool found = false;
    for (auto& row : rows) {
        if (row.size() < 3) continue;
        const std::string& flight = row[2];               // "DeltaDL 5014"
        size_t sp = flight.find_last_of(' ');
        if (sp == std::string::npos || sp < 2) continue;
        if (flight.substr(sp + 1) != num) continue;
        std::string code = flight.substr(sp - 2, 2);
        size_t pos = list.find(code);
        if (pos == std::string::npos) continue;
        int rank = (int)pos / 3;
        double off = schedule_offset_s(row[0], now_local);
        if (off < 0) off = 1e9;
        if (off > 12 * 3600.0) continue;                  // another day's flight with the same number
        if (rank < best_rank || (rank == best_rank && off < best_off)) {
            best_rank = rank;
            best_off = off;
            found = true;
            std::string place = decode_entities(row[1]);
            std::string iata;
            size_t lp = place.rfind('(');
            if (lp != std::string::npos && place.size() >= lp + 5) iata = place.substr(lp + 1, 3);
            std::string city = clean_city(place);
            if (departing) {
                r.from_city = S.airport_city; r.from_iata = S.airport_iata; r.to_city = city; r.to_iata = iata;
            } else {
                r.from_city = city; r.from_iata = iata; r.to_city = S.airport_city; r.to_iata = S.airport_iata;
            }
            r.mkt = code;
        }
    }
    return found;
}

bool pick_leg(const std::vector<std::pair<std::string, std::string>>& ap, bool departing, Route& r) {
    int n = (int)ap.size();
    if (departing) {
        for (int i = 0; i + 1 < n; i++)
            if (ap[i].first == S.airport_iata) {
                r.from_iata = ap[i].first; r.from_city = clean_city(ap[i].second);
                r.to_iata = ap[i + 1].first; r.to_city = clean_city(ap[i + 1].second);
                return true;
            }
    } else {
        for (int i = n - 1; i > 0; i--)
            if (ap[i].first == S.airport_iata) {
                r.from_iata = ap[i - 1].first; r.from_city = clean_city(ap[i - 1].second);
                r.to_iata = ap[i].first; r.to_city = clean_city(ap[i].second);
                return true;
            }
    }
    return false;
}

// ================================================================ what the sign says
std::string airline_name(const std::string& cs, const std::string& mkt) {
    // Marketing airline first (the name on the ticket: Delta for SkyWest-operated Delta flights).
    static const char* by_iata[][2] = {
        {"DL", "DELTA"}, {"UA", "UNITED"}, {"AA", "AMERICAN"}, {"WN", "SOUTHWEST"}, {"SY", "SUN COUNTRY"},
        {"F9", "FRONTIER"}, {"AS", "ALASKA"}, {"B6", "JETBLUE"}, {"NK", "SPIRIT"}, {"AC", "AIR CANADA"},
        {"KL", "KLM"}, {"AF", "AIR FRANCE"}, {"FI", "ICELANDAIR"}, {"EI", "AER LINGUS"}, {"WS", "WESTJET"},
        {"DE", "CONDOR"}, {"K5", "SEAPORT"}, {"DJ", "DJ"},
    };
    for (auto& a : by_iata)
        if (mkt == a[0]) return a[1];
    static const char* by_icao[][2] = {
        {"DAL", "DELTA"}, {"UAL", "UNITED"}, {"AAL", "AMERICAN"}, {"SWA", "SOUTHWEST"}, {"SCX", "SUN COUNTRY"},
        {"FFT", "FRONTIER"}, {"ASA", "ALASKA"}, {"JBU", "JETBLUE"}, {"NKS", "SPIRIT"}, {"ACA", "AIR CANADA"},
        {"KLM", "KLM"}, {"AFR", "AIR FRANCE"}, {"ICE", "ICELANDAIR"}, {"EIN", "AER LINGUS"}, {"WJA", "WESTJET"},
        {"BAW", "BRITISH AIRWAYS"}, {"DLH", "LUFTHANSA"}, {"RYR", "RYANAIR"}, {"EZY", "EASYJET"}, {"UAE", "EMIRATES"},
        {"QTR", "QATAR"}, {"ETD", "ETIHAD"}, {"THY", "TURKISH"}, {"SWR", "SWISS"}, {"AUA", "AUSTRIAN"},
        {"SAS", "SAS"}, {"FIN", "FINNAIR"}, {"IBE", "IBERIA"}, {"VLG", "VUELING"}, {"TAP", "TAP"},
        {"QFA", "QANTAS"}, {"VOZ", "VIRGIN AUSTRALIA"}, {"ANZ", "AIR NEW ZEALAND"}, {"JAL", "JAPAN AIRLINES"},
        {"ANA", "ANA"}, {"SIA", "SINGAPORE"}, {"CPA", "CATHAY"}, {"KAL", "KOREAN AIR"}, {"VIR", "VIRGIN"},
        {"AMX", "AEROMEXICO"}, {"VOI", "VOLARIS"}, {"HAL", "HAWAIIAN"}, {"FDX", "FEDEX"}, {"UPS", "UPS"},
        {"SKW", "SKYWEST"}, {"EDV", "ENDEAVOR"}, {"RPA", "REPUBLIC"}, {"ENY", "ENVOY"}, {"JZA", "JAZZ"},
    };
    std::string op, num;
    if (split_callsign(cs, op, num))
        for (auto& a : by_icao)
            if (op == a[0]) return a[1];
    return op;
}

static std::string upper(std::string s) {
    for (auto& c : s) c = toupper((unsigned char)c);
    return s;
}

std::string format_flight(int fmt, const Route* r, bool departing, const char* cs, const char* type) {
    std::string callsign = cs && cs[0] ? cs : (type && type[0] ? type : "?");
    if (!r) return callsign;  // no route known: the flight number (or aircraft type) is the best we have
    std::string op, num;
    bool airline = split_callsign(callsign, op, num);
    std::string name = airline ? airline_name(callsign, r->mkt) : "";
    switch (fmt) {
        case 1:  // codes
            return upper(r->from_iata + " > " + r->to_iata);
        case 2:  // airline + codes
            return upper((name.empty() ? "" : name + " ") + r->from_iata + " > " + r->to_iata);
        case 3:  // everything
            return upper((name.empty() ? callsign : name + " " + num) + "  " + r->from_city + " > " + r->to_city +
                         (type && type[0] ? std::string("  ") + type : std::string()));
        default:  // city: where it came from if landing, where it's going if taking off
            return upper(departing ? r->to_city : r->from_city);
    }
}

const char* format_name(int fmt) {
    static const char* names[] = {"CITY", "CODES", "AIRLINE", "FULL"};
    return names[(fmt % DISPLAY_FORMATS + DISPLAY_FORMATS) % DISPLAY_FORMATS];
}

// ================================================================ colors
void airline_color(const std::string& mkt, const std::string& cs, uint8_t& r, uint8_t& g, uint8_t& b) {
    struct C { const char* code; uint8_t r, g, b; };
    static const C colors[] = {
        {"DL", 255, 10, 20},   {"SY", 255, 80, 0},    {"UA", 20, 90, 255},  {"AA", 190, 200, 230},
        {"WN", 255, 170, 0},   {"F9", 0, 200, 70},    {"AS", 0, 170, 200},  {"AC", 255, 20, 40},
        {"KL", 0, 160, 255},   {"AF", 40, 60, 255},   {"FI", 255, 190, 0},  {"EI", 0, 200, 90},
        {"WS", 0, 190, 170},   {"B6", 40, 80, 255},   {"NK", 255, 220, 0},  {"DE", 255, 200, 0},
    };
    r = (S.plane_rgb >> 16) & 0xFF; g = (S.plane_rgb >> 8) & 0xFF; b = S.plane_rgb & 0xFF;
    if (!S.color_by_airline) return;
    std::string code = mkt;
    if (code.empty()) {
        std::string op, num;
        if (split_callsign(cs, op, num)) {
            const char* m = marketing_codes(op);
            if (m) code = std::string(m, 2);
        } else if (!cs.empty() && cs[0] == 'N') {
            r = g = b = 200;  // private (N-number): white
            return;
        }
    }
    for (auto& c : colors)
        if (code == c.code) { r = c.r; g = c.g; b = c.b; return; }
}

// ================================================================ tracker
void Track::predict(double t, double& lat, double& lon, float& alt) const {
    double dt = t - t_fix;
    if (f.gs > 0) move(f.lat, f.lon, f.trk, f.gs * KT_TO_FPS * dt, lat, lon);
    else { lat = f.lat; lon = f.lon; }
    alt = fmaxf((float)(S.home_elev - 150), f.alt + f.vrate / 60.0f * (float)dt);
}

int Tracker::count() const {
    int n = 0;
    for (auto& t : tracks) n += t.used;
    return n;
}

void Tracker::clear() {
    for (auto& t : tracks) t.used = false;
    msg = Message();
}

void Tracker::ingest(const std::vector<Fix>& fixes, double fetch_time) {
    for (auto& fx : fixes) {
        Track* slot = nullptr;
        for (auto& t : tracks)
            if (t.used && t.f.hex == fx.hex) { slot = &t; break; }
        if (!slot) {
            for (auto& t : tracks)
                if (!t.used) { slot = &t; *slot = Track(); slot->used = true; break; }
        }
        if (!slot) continue;  // table full: ignore, farthest planes don't matter
        char keep_cs[9];
        memcpy(keep_cs, slot->f.cs, 9);
        slot->f = fx;
        if (!fx.cs[0]) memcpy(slot->f.cs, keep_cs, 9);
        slot->t_fix = fetch_time - fx.seen_pos;
        slot->rare = rare_label(slot->f.type, slot->f.cs, slot->f.cat, slot->f.db_flags);
    }
    // Look up where planes headed over the house are from (or going) now, minutes before they're heard,
    // so the sign shows the city straight away instead of the callsign first.
    for (auto& t : tracks)
        if (t.used && t.f.cs[0] && will_pass_over(t, fetch_time)) routes.get(t.f.cs, is_departing(t), fetch_time);
}

// Straight-line closest approach: within 3 nm of the house in the next 15 minutes, low enough to be heard.
bool Tracker::will_pass_over(const Track& t, double now) const {
    double lat, lon, d, brg;
    float alt;
    t.predict(now, lat, lon, alt);
    dist_bearing_ft(S.home_lat, S.home_lon, lat, lon, d, brg);
    if (d < 2 * NM_FT) return alt - S.home_elev < 12000;
    double x = d * sin(brg * D2R), y = d * cos(brg * D2R);
    double v = t.f.gs * KT_TO_FPS;
    if (v < 1) return false;
    double vx = v * sin(t.f.trk * D2R), vy = v * cos(t.f.trk * D2R);
    double t_cpa = -(x * vx + y * vy) / (v * v);
    double miss = hypot(x + vx * t_cpa, y + vy * t_cpa);
    return t_cpa > 0 && t_cpa < 900 && miss < 3 * NM_FT && alt - S.home_elev < 12000;
}

bool Tracker::is_departing(const Track& t) const {
    if (t.f.vrate > 400) return true;
    if (t.f.vrate < -300) return false;
    double d, brg;
    dist_bearing_ft(t.f.lat, t.f.lon, S.airport_lat, S.airport_lon, d, brg);
    double diff = fabs(fmod(t.f.trk - brg + 540.0, 360.0) - 180.0);
    return diff > 90;
}

void Tracker::heard_level(Track& t, double now, float& slant) {
    double te = now, lat = 0, lon = 0, horiz = 0;
    float alt = 0, h = 0;
    for (int i = 0; i < 3; i++) {
        t.predict(te, lat, lon, alt);
        double brg;
        dist_bearing_ft(S.home_lat, S.home_lon, lat, lon, horiz, brg);
        h = alt - S.home_elev;
        te = now - hypot(horiz, h) / SPEED_OF_SOUND_FPS;
    }
    t.horiz_ft = (float)horiz;
    t.agl_ft = h;
    slant = (float)hypot(horiz, h);
    t.level = level_at(t.f.cat, t.f.type, t.departing, (float)horiz, h);
}

void Tracker::tick(double now) {
    for (auto& t : tracks) {
        if (!t.used) continue;
        if (now - t.t_fix > MAX_EXTRAPOLATE_S) {
            end_pass(t, now);
            t.used = false;
            continue;
        }
        t.departing = is_departing(t);
        float slant;
        heard_level(t, now, slant);
        if (t.level >= S.on_db && !t.in_pass) {
            t.in_pass = true; t.pass_start = now; t.pass_peak = t.level; t.pass_min_slant = slant;
        }
        if (t.in_pass) {
            t.pass_peak = fmaxf(t.pass_peak, t.level);
            if (slant < t.pass_min_slant) { t.pass_min_slant = slant; t.pass_agl = t.agl_ft; }
            if (t.level < S.off_db) end_pass(t, now);
        }
        if (t.level > S.on_db - 25 && t.f.cs[0]) routes.get(t.f.cs, t.departing, now);  // warm cache early
    }
    choose(now);
}

void Tracker::choose(double now) {
    if (msg.active) {
        Track* t = nullptr;
        for (auto& x : tracks)
            if (x.used && x.f.hex == msg.hex) t = &x;
        if (t && (t->level >= S.off_db || now - msg.since < MIN_SHOW_S)) {
            msg.level = t->level;
            fill_text(msg, *t, now);  // picks up a route that arrives late, or a new display format
            return;
        }
        msg.active = false;
    }
    Track* best = nullptr;
    for (auto& t : tracks)
        if (t.used && t.level >= S.on_db && (!best || t.level > best->level)) best = &t;
    if (best) {
        // a lookup still running: give it a few seconds rather than show the callsign and then the city
        RouteEntry* e = best->f.cs[0] ? routes.get(best->f.cs, best->departing, now) : nullptr;
        if (e && e->state == RouteState::Pending && now - best->pass_start < ROUTE_WAIT_S) return;
        msg = Message();
        msg.active = true;
        msg.hex = best->f.hex;
        msg.since = now;
        best->pass_wait = (float)(now - best->pass_start);
        msg.level = best->level;
        fill_text(msg, *best, now);
    }
}


void Tracker::fill_text(Message& m, Track& t, double now) {
    RouteEntry* e = t.f.cs[0] ? routes.get(t.f.cs, t.departing, now) : nullptr;
    const Route* r = (e && e->state == RouteState::Found) ? &e->route : nullptr;
    m.text = format_flight(S.display_format, r, t.departing, t.f.cs, t.f.type);
    m.route_final = r || (e && e->state == RouteState::NotFound);
    airline_color(r ? r->mkt : std::string(), t.f.cs, m.r, m.g, m.b);
    m.rare = S.rare_alerts && !t.rare.empty();
    if (m.rare) {
        m.text = t.rare + "!  " + m.text;
        m.r = 255; m.g = 200; m.b = 0;  // gold
    }
}

void Tracker::end_pass(Track& t, double now) {
    if (!t.in_pass) return;
    t.in_pass = false;
    for (int i = MAX_PASSES - 1; i > 0; i--) passes[i] = passes[i - 1];
    Pass& p = passes[0];
    memset(&p, 0, sizeof(p));
    p.start = (time_t)t.pass_start;
    p.wait = t.pass_wait;
    strncpy(p.cs, t.f.cs, 8);
    strncpy(p.type, t.f.type, 4);
    RouteEntry* e = t.f.cs[0] ? routes.get(t.f.cs, t.departing, now) : nullptr;
    if (e && e->state == RouteState::Found) {
        strncpy(p.city, (t.departing ? e->route.to_city : e->route.from_city).c_str(), sizeof(p.city) - 1);
        strncpy(p.other_iata, (t.departing ? e->route.to_iata : e->route.from_iata).c_str(), 3);
    }
    strncpy(p.rare, t.rare.c_str(), sizeof(p.rare) - 1);
    p.peak = t.pass_peak;
    p.agl = (int)t.pass_agl;
    p.secs = (float)(now - t.pass_start);
    p.dep = t.departing;
    if (n_passes < MAX_PASSES) n_passes++;
    struct tm lt;
    localtime_r(&p.start, &lt);
    char hhmm[6];
    strftime(hhmm, sizeof(hhmm), "%H:%M", &lt);
    stats.add_pass(lt.tm_yday, lt.tm_hour, p.cs, p.type, p.other_iata, p.city, p.dep, p.peak, p.rare, hhmm);
}

void Tracker::update_inbound(double now) {
    inbound_s = -1;
    inbound_cs.clear();
    for (auto& t : tracks) {
        if (!t.used || t.in_pass || t.level >= S.on_db || t.horiz_ft > 8 * NM_FT) continue;
        for (int dt = 2; dt <= 30; dt += 2) {  // step the prediction forward until the plane would be heard
            double lat, lon, d, brg;
            float alt;
            t.predict(now + dt, lat, lon, alt);
            dist_bearing_ft(S.home_lat, S.home_lon, lat, lon, d, brg);
            float h = alt - S.home_elev;
            if (level_at(t.f.cat, t.f.type, t.departing, (float)d, h) >= S.on_db) {
                float eta = dt + (float)(hypot(d, h) / SPEED_OF_SOUND_FPS);
                if (inbound_s < 0 || eta < inbound_s) { inbound_s = eta; inbound_cs = t.f.cs; }
                break;
            }
        }
    }
}

double Tracker::want_poll_at(double now) {
    double best = now + 3600;
    for (auto& t : tracks) {
        if (!t.used) continue;
        double lat, lon, d, brg;
        float alt;
        t.predict(now, lat, lon, alt);
        dist_bearing_ft(S.home_lat, S.home_lon, lat, lon, d, brg);
        double x = d * sin(brg * D2R), y = d * cos(brg * D2R);
        double v = t.f.gs * KT_TO_FPS;
        double vx = v * sin(t.f.trk * D2R), vy = v * cos(t.f.trk * D2R);
        float agl = alt - S.home_elev;
        if (v > 1) {
            double t_cpa = -(x * vx + y * vy) / (v * v);
            double miss = hypot(x + vx * t_cpa, y + vy * t_cpa);
            if (t_cpa > 0 && t_cpa < 600 && miss < 2 * NM_FT && agl < 8000) {
                best = fmin(best, now + fmax(0.0, t_cpa - 30));
                continue;
            }
        }
        if (d < 12 * NM_FT && agl < 6000 && now - t.t_fix > 45) best = fmin(best, now);
    }
    return best;
}

}  // namespace ov
