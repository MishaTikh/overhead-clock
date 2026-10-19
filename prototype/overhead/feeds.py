"""ADS-B position sources. All return readsb/tar1090-style aircraft dicts."""
import json
import math
import random
import time
import urllib.request

from . import geo

UA = "msp-overhead/0.1 (hobby LED sign)"


def _get_json(url, timeout=10):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)


class LocalFeed:
    """Your own receiver: RTL-SDR dongle + readsb on the Pi. No limits, ~1 s latency."""
    name = "local"

    def __init__(self, cfg):
        self.cfg = cfg

    def interval(self, hot):
        return self.cfg.local_interval_s

    def fetch(self):
        return _get_json(self.cfg.local_url, timeout=3).get("aircraft", [])


class AdsbLolFeed:
    """api.adsb.lol free API. Rate limit is 4/s, 60/min and 500/day per IP, so we
    keep a token bucket refilled at daily_budget/day and spend it mostly when
    something is close. Dead reckoning fills in between polls."""
    name = "adsblol"
    CAPACITY = 40

    def __init__(self, cfg):
        self.cfg = cfg
        self.tokens = float(self.CAPACITY)
        self.last_refill = time.time()
        self.calls = 0

    def _refill(self):
        now = time.time()
        self.tokens = min(self.CAPACITY, self.tokens + (now - self.last_refill) * self.cfg.adsblol_daily_budget / 86400)
        self.last_refill = now

    def interval(self, hot):
        self._refill()
        base = self.cfg.adsblol_hot_interval_s if hot else self.cfg.adsblol_idle_interval_s
        if self.tokens >= 1:
            return base
        # out of tokens: wait for the next one
        return max(base, (1 - self.tokens) * 86400 / self.cfg.adsblol_daily_budget)

    def fetch(self):
        self._refill()
        self.tokens -= 1
        self.calls += 1
        c = self.cfg
        url = f"https://api.adsb.lol/v2/point/{c.home_lat:.4f}/{c.home_lon:.4f}/{int(c.adsblol_radius_nm)}"
        return _get_json(url).get("ac", [])


# ---------------------------------------------------------------------------
# Simulator: realistic MSP traffic flying the real runway geometry.
RWY = {  # threshold lat, lon, elev ft, inbound course (true)
    "12R": (44.88780, -93.23410, 841, 121.3),
    "12L": (44.89290, -93.22100, 838, 121.3),
    "30L": (44.87350, -93.20120, 813, 301.3),
    "30R": (44.88130, -93.19400, 819, 301.3),
}
SIM_FLIGHTS = [
    ("DAL1902", "A321", "A3", "Denver", "DEN"), ("SKW3829", "CRJ9", "A3", "Toronto", "YYZ"),
    ("DAL2159", "B739", "A3", "San Francisco", "SFO"), ("UAL1421", "A320", "A3", "Chicago", "ORD"),
    ("EDV5331", "CRJ9", "A3", "Cincinnati", "CVG"), ("SCX402", "B738", "A3", "Cancun", "CUN"),
    ("DAL164", "A333", "A5", "Amsterdam", "AMS"), ("AAL2201", "A321", "A3", "Dallas-Fort Worth", "DFW"),
    ("SWA1188", "B38M", "A3", "Las Vegas", "LAS"), ("DAL1033", "B752", "A4", "Atlanta", "ATL"),
    ("SKW4170", "E75L", "A3", "Fargo", "FAR"), ("FDX1450", "B763", "A5", "Memphis", "MEM"),
    ("ACA791", "A220", "A3", "Montreal", "YUL"), ("DAL39", "A359", "A5", "Tokyo", "HND"),
    ("N172SP", "C172", "A1", "", ""), ("DAL870", "A21N", "A3", "Phoenix", "PHX"),
]


class SimFeed:
    """Arrivals on 12R/12L (or departures off 30L/30R with flow='30'), every ~80 s."""
    name = "sim"

    def __init__(self, cfg, flow="12", seed=None):
        self.cfg = cfg
        self.flow = flow
        self.rng = random.Random(seed)
        self.planes = []
        self.next_spawn = time.time() + 40
        # start with one plane already on final so there is something to see right away
        self._spawn(time.time(), head_start=150.0 if flow == "12" else 60.0)
        self.next_spawn = time.time() + 40

    def interval(self, hot):
        return 1.0

    def _spawn(self, now, head_start=0.0):
        cs, typ, cat, city, iata = self.rng.choice(SIM_FLIGHTS)
        if self.flow == "12":
            rwy = self.rng.choices(["12R", "12L"], weights=[3, 2])[0]
        else:
            rwy = self.rng.choices(["30L", "30R"], weights=[3, 2])[0]
        self.planes.append({"cs": cs, "type": typ, "cat": cat, "city": city, "iata": iata,
                            "rwy": rwy, "t": now - head_start, "hex": "%06x" % self.rng.randrange(1 << 24)})
        self.next_spawn = now + self.rng.uniform(60, 110)

    def _state(self, p, now):
        lat0, lon0, elev, crs = RWY[p["rwy"]]
        dt = now - p["t"]
        if p["rwy"].startswith("12"):
            # 3-degree glideslope from 14 nm out; 180 kt slowing to 140 kt inside 5 nm
            start_nm = 14.0
            flown = 0.0
            # integrate speed profile cheaply
            v1, v2 = 180 / 3600, 140 / 3600
            t_fast = (start_nm - 5.0) / v1
            flown = v1 * dt if dt < t_fast else (start_nm - 5.0) + v2 * (dt - t_fast)
            d_nm = start_nm - flown
            if d_nm < 0:
                return None
            gs = 180.0 if d_nm > 5 else 140.0
            lat, lon = geo.move(lat0, lon0, (crs + 180) % 360, d_nm * geo.NM_FT)
            alt = elev + 50 + d_nm * geo.NM_FT * math.tan(math.radians(3.0))
            return lat, lon, alt, gs, crs, -gs * 101.3 * math.tan(math.radians(3.0))
        # departure: rolls 35 s, then 160 kt -> 220 kt climbing 2500 fpm on runway heading
        if dt < 35:
            return None
        t = dt - 35
        gs = min(220.0, 160 + t * 0.6)
        d_nm = (160 * t + 0.3 * t * t) / 3600 if gs < 220 else None
        if d_nm is None:
            t_cap = 100.0
            d_nm = (160 * t_cap + 0.3 * t_cap ** 2) / 3600 + 220 * (t - t_cap) / 3600
        if d_nm > 18:
            return None
        alt = min(elev + 2500 * t / 60, 10000)
        lat, lon = geo.move(lat0, lon0, crs, d_nm * geo.NM_FT)
        return lat, lon, alt, gs, crs, 2500.0 if alt < 10000 else 0.0

    def fetch(self):
        now = time.time()
        if now >= self.next_spawn:
            self._spawn(now)
        out, keep = [], []
        for p in self.planes:
            s = self._state(p, now)
            if s is None and now - p["t"] > 60:
                continue
            keep.append(p)
            if s is None:
                continue
            lat, lon, alt, gs, trk, vr = s
            dep = not p["rwy"].startswith("12")
            route = None
            if p["iata"]:
                route = {"from_city": "Minneapolis", "from_iata": "MSP", "to_city": p["city"], "to_iata": p["iata"]} if dep \
                    else {"from_city": p["city"], "from_iata": p["iata"], "to_city": "Minneapolis", "to_iata": "MSP"}
            out.append({"hex": p["hex"], "flight": p["cs"], "t": p["type"], "category": p["cat"],
                        "lat": lat, "lon": lon, "alt_geom": round(alt), "alt_baro": round(alt),
                        "gs": gs, "track": trk, "geom_rate": vr, "seen_pos": 0.0, "_route": route})
        self.planes = keep
        return out


def make(cfg, sim_flow="12"):
    return {"local": LocalFeed, "adsblol": AdsbLolFeed}.get(cfg.feed, None)(cfg) if cfg.feed != "sim" \
        else SimFeed(cfg, flow=sim_flow)
