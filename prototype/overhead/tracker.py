"""Keeps a dead-reckoned picture of nearby aircraft and decides what the sign shows."""
import csv
import math
import os
import time
from dataclasses import dataclass, field

from . import acoustics, geo
from .routes import airline_rgb

KT_TO_FPS = 1.68781


@dataclass
class Track:
    hex: str
    callsign: str = ""
    type: str = ""
    category: str = ""
    lat: float = 0.0
    lon: float = 0.0
    alt: float = 0.0          # ft MSL
    gs: float = 0.0           # kt
    trk: float = 0.0          # deg true
    vrate: float = 0.0        # ft/min
    t_fix: float = 0.0        # unix time of the position fix
    sim_route: dict | None = None
    # derived each tick
    level: float = -99.0
    horiz_ft: float = 0.0
    agl_ft: float = 0.0
    departing: bool = False
    # pass bookkeeping
    in_pass: bool = False
    pass_start: float = 0.0
    pass_peak: float = -99.0
    pass_min_slant: float = 1e9
    pass_alt_at_min: float = 0.0

    def predict(self, t, floor_ft):
        dt = t - self.t_fix
        lat, lon = geo.move(self.lat, self.lon, self.trk, self.gs * KT_TO_FPS * dt) if self.gs else (self.lat, self.lon)
        return lat, lon, max(floor_ft, self.alt + self.vrate / 60.0 * dt)


@dataclass
class Message:
    hex: str = ""
    text: str = ""
    level: float = -99.0
    since: float = 0.0
    route: dict | None = None
    callsign: str = ""
    type: str = ""
    rgb: tuple = (255, 120, 0)


class Tracker:
    def __init__(self, cfg, routes):
        self.cfg = cfg
        self.routes = routes
        self.tracks: dict[str, Track] = {}
        self.current: Message | None = None
        self.last_fetch = 0.0
        self.recent_passes = []

    # ---------------------------------------------------------------- input
    def ingest(self, aircraft, fetch_time):
        self.last_fetch = fetch_time
        for a in aircraft:
            if "lat" not in a or a.get("alt_baro") == "ground":
                continue
            alt = a.get("alt_geom", a.get("alt_baro"))
            if not isinstance(alt, (int, float)):
                continue
            t = self.tracks.get(a["hex"]) or Track(hex=a["hex"])
            t.callsign = (a.get("flight") or t.callsign or "").strip()
            t.type = a.get("t", t.type) or ""
            t.category = a.get("category", t.category) or ""
            t.lat, t.lon, t.alt = a["lat"], a["lon"], float(alt)
            t.gs = float(a.get("gs") or 0.0)
            t.trk = float(a.get("track", a.get("true_heading", t.trk)) or 0.0)
            t.vrate = float(a.get("geom_rate", a.get("baro_rate", 0.0)) or 0.0)
            t.t_fix = fetch_time - float(a.get("seen_pos", 0.0) or 0.0)
            t.sim_route = a.get("_route")
            self.tracks[a["hex"]] = t

    # ---------------------------------------------------------------- physics
    def _is_departing(self, t):
        if t.vrate > 400:
            return True
        if t.vrate < -300:
            return False
        _, brg_to_apt = geo.dist_bearing_ft(t.lat, t.lon, self.cfg.airport_lat, self.cfg.airport_lon)
        diff = abs((t.trk - brg_to_apt + 180) % 360 - 180)
        return diff > 90

    def _heard_level(self, t, now):
        """Level arriving now = sound emitted at te where te = now - r(te)/c."""
        c = self.cfg
        te = now
        for _ in range(3):
            lat, lon, alt = t.predict(te, c.home_elev_ft - 150)
            horiz, _ = geo.dist_bearing_ft(c.home_lat, c.home_lon, lat, lon)
            h = alt - c.home_elev_ft
            te = now - math.hypot(horiz, h) / c.speed_of_sound_fps
        t.horiz_ft, t.agl_ft = horiz, h
        return acoustics.level_at(c, t.category, t.departing, horiz, h), math.hypot(horiz, h)

    # ---------------------------------------------------------------- tick
    def tick(self, now):
        c = self.cfg
        for k in [k for k, t in self.tracks.items() if now - t.t_fix > c.max_extrapolate_s]:
            self._end_pass(self.tracks.pop(k), now)

        for t in self.tracks.values():
            t.departing = self._is_departing(t)
            t.level, slant = self._heard_level(t, now)
            if t.level >= c.on_db and not t.in_pass:
                t.in_pass, t.pass_start, t.pass_peak, t.pass_min_slant = True, now, t.level, slant
            if t.in_pass:
                t.pass_peak = max(t.pass_peak, t.level)
                if slant < t.pass_min_slant:
                    t.pass_min_slant, t.pass_alt_at_min = slant, t.agl_ft
                if t.level < c.off_db:
                    self._end_pass(t, now)
            # warm the route cache before the plane gets loud
            if t.level > c.on_db - 20 and t.sim_route is None:
                self.routes.get(t.callsign, t.departing)

        self._choose(now)
        return self.current

    def _choose(self, now):
        c = self.cfg
        cur = self.current
        if cur:
            t = self.tracks.get(cur.hex)
            keep = t is not None and (t.level >= c.off_db or now - cur.since < c.min_show_s)
            if keep:
                cur.level = t.level
                if cur.route is None or cur.route == "pending":
                    self._fill_text(cur, t)
                return
            self.current = None
        loud = [t for t in self.tracks.values() if t.level >= c.on_db]
        if loud:
            t = max(loud, key=lambda x: x.level)
            m = Message(hex=t.hex, level=t.level, since=now, callsign=t.callsign, type=t.type)
            self._fill_text(m, t)
            self.current = m

    def _fill_text(self, m, t):
        c = self.cfg
        route = t.sim_route if t.sim_route is not None else self.routes.get(t.callsign, t.departing)
        m.route = route
        if isinstance(route, dict):
            if t.departing:
                text = f"{c.departure_prefix}{route['to_city']}" if c.departure_prefix else route["to_city"]
            else:
                text = route["from_city"]
            if c.show_flight_number and t.callsign:
                text += "  " + t.callsign
        else:
            text = c.unknown_text or t.callsign or t.type or "?"
        m.text = text.upper()
        m.rgb = airline_rgb(route.get("mkt") if isinstance(route, dict) else None, t.callsign)

    # ---------------------------------------------------------------- polling
    def want_poll_at(self, now):
        """When would a fresh fix be most useful? Aim for ~30 s before each plane's
        closest approach, and keep low traffic near the airport refreshed."""
        c = self.cfg
        best = now + 3600
        for t in self.tracks.values():
            age = now - t.t_fix
            x, y = geo.local_xy_ft(c.home_lat, c.home_lon, *t.predict(now, 0)[:2])
            v = t.gs * KT_TO_FPS
            vx, vy = v * math.sin(math.radians(t.trk)), v * math.cos(math.radians(t.trk))
            if v > 1:
                t_cpa = -(x * vx + y * vy) / (v * v)
                miss = math.hypot(x + vx * t_cpa, y + vy * t_cpa)
                if 0 < t_cpa < 600 and miss < 2 * geo.NM_FT and t.alt - c.home_elev_ft < 8000:
                    best = min(best, now + max(0.0, t_cpa - 30))
                    continue
            # maneuvering in the terminal area: refresh every ~45 s
            if math.hypot(x, y) < 12 * geo.NM_FT and t.alt - c.home_elev_ft < 6000 and age > 45:
                best = min(best, now)
        return best

    # ---------------------------------------------------------------- logging
    def _end_pass(self, t, now):
        if not t.in_pass:
            return
        t.in_pass = False
        dur = now - t.pass_start
        route = t.sim_route if t.sim_route is not None else self.routes.get(t.callsign, t.departing)
        city = ""
        if isinstance(route, dict):
            city = route["to_city"] if t.departing else route["from_city"]
        row = {"time": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(t.pass_start)),
               "callsign": t.callsign, "type": t.type, "category": t.category,
               "phase": "departure" if t.departing else "arrival", "city": city,
               "peak_dba": round(t.pass_peak, 1), "closest_slant_ft": round(t.pass_min_slant),
               "agl_at_closest_ft": round(t.pass_alt_at_min), "audible_s": round(dur, 1)}
        self.recent_passes = ([row] + self.recent_passes)[:20]
        if self.cfg.pass_log_csv:
            new = not os.path.exists(self.cfg.pass_log_csv)
            with open(self.cfg.pass_log_csv, "a", newline="") as f:
                w = csv.DictWriter(f, fieldnames=list(row))
                if new:
                    w.writeheader()
                w.writerow(row)

    # ---------------------------------------------------------------- ui data
    def snapshot(self, now):
        c = self.cfg
        out = []
        for t in sorted(self.tracks.values(), key=lambda x: -x.level)[:40]:
            lat, lon, alt = t.predict(now, 0)
            x, y = geo.local_xy_ft(c.home_lat, c.home_lon, lat, lon)
            out.append({"hex": t.hex, "cs": t.callsign, "type": t.type, "cat": t.category,
                        "x_nm": x / geo.NM_FT, "y_nm": y / geo.NM_FT, "agl": round(alt - c.home_elev_ft),
                        "gs": round(t.gs), "trk": t.trk, "db": round(t.level, 1),
                        "dep": t.departing, "age": round(now - t.t_fix, 1),
                        "audible": t.level >= c.on_db})
        return out
