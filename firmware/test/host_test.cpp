// Host-side checks of the firmware logic: python -m ziglang c++ -std=c++17 -Isrc test/host_test.cpp src/overhead.cpp src/render.cpp
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include "overhead.h"
#include "render.h"
using namespace ov;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(int argc, char** argv) {
    S.home_lat = 44.9483; S.home_lon = -93.3749; S.home_elev = 918;   // a sample spot 7 nm out on MSP's 12R approach
    CHECK(resolve_airport() && S.airport_iata == "MSP");                 // nearest airport
    CHECK(posix_for_zone("America/Chicago") && std::string(posix_for_zone("America/Chicago")) == "CST6CDT,M3.2.0,M11.1.0");
    // 1. flight board parser on a saved mspairport.com page (EDV5014 = DL 5014 from Cincinnati)
    if (argc > 1) {
        std::ifstream f(argv[1], std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        std::string html = ss.str();
        BoardParser p; p.reset();
        for (char c : html) p.feed(c);
        printf("board rows: %zu\n", p.rows.size());
        for (auto& r : p.rows) printf("  [%s] [%s] [%s]\n", r[0].c_str(), r[1].c_str(), r[2].c_str());
        struct tm now = {}; now.tm_year = 126; now.tm_mon = 9; now.tm_mday = 18; now.tm_hour = 20; now.tm_min = 50; now.tm_isdst = -1;
        mktime(&now);
        Route r;
        CHECK(pick_board_row(p.rows, "EDV5014", false, now, r));
        printf("EDV5014 -> %s (%s) mkt %s\n", r.from_city.c_str(), r.from_iata.c_str(), r.mkt.c_str());
        CHECK(r.from_city == "Cincinnati");
        CHECK(!pick_board_row(p.rows, "UAL5014", false, now, r));   // wrong airline
    }
    // 2. city cleanup and callsign parsing
    CHECK(clean_city("New York-JFK (JFK)") == "New York");
    CHECK(clean_city("Columbus, OH (CMH)") == "Columbus");
    CHECK(clean_city("Washington - Reagan (DCA)") == "Washington");
    CHECK(clean_city("Raleigh/Durham (RDU)") == "Raleigh/Durham");
    std::string op, num;
    CHECK(split_callsign("DAL39", op, num) && op == "DAL" && num == "39");
    CHECK(!split_callsign("N737YU", op, num));
    // 3. legs: SKW4063 MSP-MEM-MSP arriving -> Memphis
    Route r2;
    CHECK(pick_leg({{"MSP","Minneapolis"},{"MEM","Memphis"},{"MSP","Minneapolis"}}, false, r2) && r2.from_city == "Memphis");
    // 4. tracker: A320 on the 12R glideslope passing over the house at 145 kt
    Tracker tr;
    double t0 = 1.0e9, peak = -99, on = -1, off = -1;
    for (int s = 0; s <= 120; s++) {
        double t = t0 + s;
        if (s % 15 == 0) {   // a fresh fix every 15 s, like the budgeted feed
            double d_nm = 9.5 - 145.0 / 3600 * s;
            double lat, lon;
            move(44.88780, -93.23410, 301.3, d_nm * 6076.12, lat, lon);
            Fix f; f.hex = 0xabc123; strcpy(f.cs, "DAL2159"); strcpy(f.type, "B739"); strcpy(f.cat, "A3");
            f.lat = lat; f.lon = lon; f.alt = 841 + 50 + d_nm * 6076.12 * 0.0524; f.gs = 145; f.trk = 121.3; f.vrate = -770;
            tr.ingest({f}, t);
        }
        tr.tick(t);
        if (tr.msg.active && on < 0) on = s;
        if (!tr.msg.active && on >= 0 && off < 0) off = s;
        for (auto& k : tr.tracks) if (k.used && k.level > peak) peak = k.level;
    }
    printf("pass: on at %.0f s, off at %.0f s (%.0f s audible), peak %.1f dBA, text '%s'\n", on, off, off - on, peak, tr.msg.text.c_str());
    CHECK(on > 0 && off > on && off - on > 20 && off - on < 50);
    CHECK(peak > 70 && peak < 78);   // A320-class at ~2,200 ft overhead
    // early route lookup: queued on the first fix, minutes out, for a plane headed over the house only
    {
        double lat, lon;
        move(44.88780, -93.23410, 301.3, 9.5 * 6076.12, lat, lon);
        Fix f; f.hex = 0xabc124; strcpy(f.cs, "DAL2160"); strcpy(f.cat, "A3");
        f.lat = lat; f.lon = lon; f.alt = 3900; f.gs = 145; f.trk = 121.3; f.vrate = -770;
        Tracker tp;
        tp.ingest({f}, t0);
        RouteEntry* q = tp.routes.next_request();
        CHECK(q && q->cs == "DAL2160" && !q->departing);
        f.trk = 30;                   // same spot, flying past to the north-east
        Tracker tq;
        tq.ingest({f}, t0);
        CHECK(tq.routes.next_request() == nullptr);
    }
    // 5. rendering
    Marquee m; uint8_t cols[32];
    for (const char* city : {"DENVER", "SAN FRANCISCO"}) {
        m.set_text(city, 0); m.frame(0, cols);
        printf("%s (%zu px)\n", city, text_columns(city).size());
        for (int y = 0; y < 8; y++) { for (int x = 0; x < 32; x++) putchar(cols[x] >> y & 1 ? '#' : '.'); putchar('\n'); }
    }
    // 6. display formats, arriving and departing
    Route rr; rr.from_city = "Denver"; rr.from_iata = "DEN"; rr.to_city = "Minneapolis"; rr.to_iata = "MSP"; rr.mkt = "DL";
    for (int f = 0; f < DISPLAY_FORMATS; f++) printf("%-8s %s\n", format_name(f), format_flight(f, &rr, false, "DAL1902", "A321").c_str());
    CHECK(format_flight(0, &rr, false, "DAL1902", "A321") == "DENVER");
    CHECK(format_flight(0, &rr, true, "DAL1902", "A321") == "MINNEAPOLIS");
    CHECK(format_flight(1, &rr, false, "DAL1902", "A321") == "DEN > MSP");
    CHECK(format_flight(2, &rr, false, "SKW3829", "CRJ9") == "DELTA DEN > MSP");
    CHECK(format_flight(3, &rr, false, "DAL1902", "A321") == "DELTA 1902  DENVER > MINNEAPOLIS  A321");
    CHECK(format_flight(2, nullptr, false, "DAL1902", "A321") == "DAL1902");
    // short text scrolls too: after the hold the first column moves
    m.set_text("FARGO", 0); uint8_t a[32], b2[32]; m.frame(0, a); m.frame(3.0, b2);
    CHECK(memcmp(a, b2, 32) != 0);
    // airports elsewhere
    S.home_lat = 51.4816; S.home_lon = -0.4510; resolve_airport();
    printf("near Heathrow -> %s %s\n", S.airport_iata.c_str(), S.airport_city.c_str());
    CHECK(S.airport_iata == "LHR");
    // 7. extras: runway flow from the wind (MSP; house under the 12R approach)
    S.home_lat = 44.9483; S.home_lon = -93.3749; resolve_airport();
    Weather w; w.ok = true; w.wind_kt = 12;
    w.wind_dir = 120; FlowInfo fl = flow_for("MSP", S.home_lat, S.home_lon, w);
    printf("wind 120 @ 12 kt: %s %s\n", flow_text(fl.flow), fl.runway.c_str());
    CHECK(fl.flow == Flow::ArrivalsOverYou && fl.runway == "12R");
    w.wind_dir = 300; fl = flow_for("MSP", S.home_lat, S.home_lon, w);
    printf("wind 300 @ 12 kt: %s %s\n", flow_text(fl.flow), fl.runway.c_str());
    CHECK(fl.flow == Flow::DeparturesOverYou && fl.runway == "30L");
    w.wind_kt = 2; CHECK(flow_for("MSP", S.home_lat, S.home_lon, w).flow == Flow::Calm);
    // sun: Minneapolis on 2026-10-19, sunrise about 7:31, sunset about 18:23 (CDT)
    setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1); tzset();
    struct tm day = {}; day.tm_year = 126; day.tm_mon = 9; day.tm_mday = 19; day.tm_hour = 12; day.tm_isdst = -1;
    int rise = 0, set = 0;
    CHECK(sun_times(mktime(&day), S.home_lat, S.home_lon, rise, set));
    printf("sunrise %d:%02d sunset %d:%02d\n", rise / 60, rise % 60, set / 60, set % 60);
    CHECK(abs(rise - (7 * 60 + 31)) <= 4 && abs(set - (18 * 60 + 23)) <= 4);
    // rare aircraft and the daily recap
    CHECK(rare_label("B744", "UPS2", "A5", 0) == "747");
    CHECK(rare_label("C17", "RCH123", "A5", 0) == "MILITARY");
    CHECK(rare_label("A320", "DAL1", "A3", 0) == "");
    DayStats ds; ds.reset(291);
    ds.add_pass(291, 9, "DAL39", "A359", "HND", "Tokyo", false, 82.7f, "A350", "09:12");
    ds.add_pass(291, 10, "SKW3829", "CRJ9", "YYZ", "Toronto", false, 76.7f, "", "10:02");
    printf("recap: %s\n", ds.recap().c_str());
    CHECK(ds.planes == 2 && ds.farthest_city == "Tokyo" && ds.loudest_type == "A359");
    struct tm u = {}; u.tm_year = 126; u.tm_mon = 9; u.tm_mday = 19; u.tm_hour = 1; u.tm_min = 2; u.tm_sec = 3;
    CHECK(utc_from_tm(u) == 1792371723);
    // sign sequence: still password pages with progress marks, buttons step through
    {
        SignSequence q;
        std::vector<SignStep> st = {SignStep("SETTINGS 192.168.1.2", 2.0, false, 10), SignStep("K7PR", 3.0, true, 0, 1, 3),
                                    SignStep("3XQ9", 3.0, true, 0, 2, 3), SignStep("TMWA", 3.0, true, 0, 3, 3)};
        q.start(st, 100.0, 1, 1);
        uint8_t f[32], g[32];
        CHECK(q.frame(102.5, f));                     // on the first page
        int lit = 0; for (int x = 0; x < 32; x++) lit += (f[x] & 0x80) != 0;
        CHECK(lit == 3);                               // one of three progress marks
        q.frame(103.0, g);
        CHECK(memcmp(f, g, 32) == 0);                  // still: doesn't scroll
        CHECK(q.frame(110.0, f));                      // repeats the pages once
        CHECK(q.frame(119.5, f) && !q.frame(120.5, f));  // header 2 s + pages 9 s + one repeat 9 s, then done
        q.start(st, 200.0, 1, 1);
        q.next(200.1); q.next(200.2);                  // buttons: manual mode, holds the page
        CHECK(q.frame(290.0, f));
        lit = 0; for (int x = 0; x < 32; x++) lit += (f[x] & 0x80) != 0;
        CHECK(lit == 6);                               // second of three pages
        CHECK(!q.frame(330.0, f));                     // closes 2 minutes after the last press
    }
    // 8. clock face
    clock_frame(21, 45, true, false, cols);
    printf("clock 21:45 (12 h)\n");
    for (int y = 0; y < 8; y++) { for (int x = 0; x < 32; x++) putchar(cols[x] >> y & 1 ? '#' : '.'); putchar('\n'); }
    clock_frame(12, 59, true, false, cols);
    int lit = 0; for (int x = 0; x < 32; x++) lit += cols[x] != 0;
    CHECK(cols[0] == 0 && cols[31] == 0 && lit > 15);   // "12:59" fits with margins
    printf(fails ? "\n%d FAILED\n" : "\nall checks passed\n", fails);
    return fails != 0;
}
