"""Fly ideal paths past the house and report altitude, loudness and audible time."""
import math

from . import acoustics, geo
from .feeds import RWY

CATS = [("A1", "Cessna 172", 90), ("A2", "CRJ200 / bizjet", 140), ("A3", "A320 / 737 / E175", 145),
        ("A4", "757", 140), ("A5", "A330 / 767 / 777", 150)]


def _pass(cfg, rwy, cat, gs, departing):
    lat0, lon0, elev, crs = RWY[rwy]
    out_brg = crs if departing else (crs + 180) % 360
    d_house, b_house = geo.dist_bearing_ft(lat0, lon0, cfg.home_lat, cfg.home_lon)
    along = d_house * math.cos(math.radians(b_house - out_brg))
    cross = d_house * math.sin(math.radians(b_house - out_brg))
    if departing:  # ~2500 fpm at ~165 kt after ~5000 ft of roll
        climb_grad = 2500 / (165 * 101.27)
        alt_at = lambda s: elev + max(0.0, (s - 2000) * climb_grad)
    else:
        alt_at = lambda s: elev + 50 + s * math.tan(math.radians(3.0))
    v = gs * 1.68781
    audible, peak, s = 0.0, -99.0, along + 4 * geo.NM_FT
    first = last = None
    while s > along - 4 * geo.NM_FT:
        h = alt_at(s) - cfg.home_elev_ft
        L = acoustics.level_at(cfg, cat, departing, math.hypot(s - along, cross), h)
        peak = max(peak, L)
        if L >= cfg.on_db:
            audible += 1 / v * 50
            first = first if first is not None else s
            last = s
        s -= 50
    return {"agl": alt_at(along) - cfg.home_elev_ft, "msl": alt_at(along), "cross": cross, "along_nm": along / geo.NM_FT,
            "peak": peak, "secs": audible, "span_nm": (first - last) / geo.NM_FT if first else 0.0}


def report(cfg):
    lines = [f"House {cfg.home_lat:.5f}, {cfg.home_lon:.5f}, ground {cfg.home_elev_ft:.0f} ft MSL",
             f"'Clearly audible' = {cfg.on_db:.0f} dBA (off below {cfg.off_db:.0f}); quiet-neighborhood ambient ~45 dBA", ""]
    for rwy, dep in (("12R", False), ("12L", False), ("30L", True), ("30R", True)):
        r0 = _pass(cfg, rwy, "A3", 145, dep)
        kind = "departure off" if dep else "arrival to"
        lines.append(f"{kind} {rwy}: house is {r0['along_nm']:.1f} nm from threshold, "
                     f"{abs(r0['cross']):.0f} ft {'left' if r0['cross'] < 0 else 'right'} of centerline; "
                     f"plane passes at ~{r0['msl']:.0f} ft MSL = {r0['agl']:.0f} ft above you")
        for cat, name, gs in CATS:
            r = _pass(cfg, rwy, cat, gs if not dep else 165, dep)
            lines.append(f"    {name:20s} peak {r['peak']:5.1f} dBA   audible {r['secs']:5.1f} s "
                         f"over {r['span_nm']:.1f} nm of track")
        lines.append("")
    return "\n".join(lines)
