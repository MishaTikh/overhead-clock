"""Callsign -> which city the flight came from (or is going to).

1. MSP's own flight board (mspairport.com flight search): today's real schedule, so it
   is right even when airlines reshuffle flight numbers. Regional operators fly under
   their partners' codes (EDV5014 = Delta "DL 5014"), so we try each marketing code.
2. adsb.lol VRS standing data, then adsbdb.com: static route databases. Often stale,
   so only trusted when a leg actually touches MSP.
Results are cached per (callsign, direction) for a few hours.
"""
import html
import json
import re
import threading
import time
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime

UA = "msp-overhead/0.2 (hobby LED sign; one lookup per overhead flight)"
CITY_FIXES = {"STT": "St Thomas", "DFW": "Dallas", "MSP": "Minneapolis", "NYC": "New York"}

# ICAO operator prefix -> IATA marketing codes it flies under (most likely first)
MARKETING = {
    "DAL": ["DL"], "EDV": ["DL"], "SKW": ["DL", "UA", "AA", "AS"], "RPA": ["DL", "UA", "AA"],
    "GJS": ["UA", "DL"], "CPZ": ["DL", "AA"], "ASH": ["UA", "AA"], "UAL": ["UA"], "AAL": ["AA"],
    "ENY": ["AA"], "PDT": ["AA"], "JIA": ["AA"], "SWA": ["WN"], "SCX": ["SY"], "FFT": ["F9"],
    "ASA": ["AS"], "QXE": ["AS"], "ACA": ["AC"], "JZA": ["AC"], "ROU": ["AC"], "KLM": ["KL"],
    "AFR": ["AF"], "ICE": ["FI"], "EIN": ["EI"], "WJA": ["WS"], "OCN": ["DE"], "JBU": ["B6"],
    "NKS": ["NK"], "DJT": ["DJ"], "UCA": ["UA"], "AWI": ["UA"], "GLA": ["UA"], "KAP": ["K5"],
}
MONTHS = {m: i for i, m in enumerate("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec".split(), 1)}


def _get(url, as_json=True):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.load(r) if as_json else r.read().decode("utf-8", "replace")


def _clean_city(iata, city):
    city = CITY_FIXES.get(iata, city or iata or "")
    city = re.sub(r"\s*\([A-Z0-9]{3}\)\s*$", "", city)          # "Cincinnati (CVG)"
    city = re.sub(r"-[A-Z]{3}$", "", city)                       # "New York-JFK"
    return city.split(",")[0].split(" - ")[0].strip()            # "Columbus, OH", "Houston - Bush"


def _split_callsign(cs):
    m = re.match(r"^([A-Z]{3})(\d{1,4})[A-Z]?$", cs)
    return (m.group(1), m.group(2)) if m else (None, None)


# ---------------------------------------------------------------------------
def msp_board(cs, departing, now=None):
    """Look the flight up on mspairport.com. Returns route dict or None."""
    op, num = _split_callsign(cs)
    if not op or op not in MARKETING:
        return None
    now = now or datetime.now()
    q = urllib.parse.urlencode({"flight_type": "departure" if departing else "arrival", "text": num})
    page = _get(f"https://www.mspairport.com/flights-and-airlines/flights?{q}", as_json=False)
    body = page[page.find("<tbody"):page.find("</tbody>")]
    best = None
    for row in re.findall(r"<tr>(.*?)</tr>", body, re.S):
        cells = [html.unescape(re.sub(r"<[^>]+>", "", c)).strip() for c in re.findall(r"<td[^>]*>(.*?)</td>", row, re.S)]
        if len(cells) < 3:
            continue
        when, place, flight = cells[0], cells[1], cells[2]
        fm = re.search(r"([A-Z0-9]{2})\s*(\d+)$", flight)
        if not fm or fm.group(2) != num:
            continue
        rank = MARKETING[op].index(fm.group(1)) if fm.group(1) in MARKETING[op] else None
        if rank is None:
            continue
        tm = re.search(r"([A-Z][a-z]{2}) (\d+)\D+(\d+):(\d+) ([ap])\.m\.", when)
        if tm:
            h = int(tm.group(3)) % 12 + (12 if tm.group(5) == "p" else 0)
            sched = datetime(now.year, MONTHS[tm.group(1)], int(tm.group(2)), h, int(tm.group(4)))
            off = abs((sched - now).total_seconds())
        else:
            off = 1e9
        if off > 12 * 3600:  # tomorrow's or yesterday's instance of the same number
            continue
        iata = (re.search(r"\(([A-Z0-9]{3})\)", place) or [None, ""])[1]
        cand = (rank, off, _clean_city(iata, place), iata, fm.group(1))
        best = min(best, cand) if best else cand
    if not best:
        return None
    _, _, city, iata, mkt = best
    if departing:
        return {"from_city": "Minneapolis", "from_iata": "MSP", "to_city": city, "to_iata": iata, "src": "msp", "mkt": mkt}
    return {"from_city": city, "from_iata": iata, "to_city": "Minneapolis", "to_iata": "MSP", "src": "msp", "mkt": mkt}


AIRLINE_RGB = {  # same palette as the TC001 firmware
    "DL": (255, 10, 20), "SY": (255, 80, 0), "UA": (20, 90, 255), "AA": (190, 200, 230), "WN": (255, 170, 0),
    "F9": (0, 200, 70), "AS": (0, 170, 200), "AC": (255, 20, 40), "KL": (0, 160, 255), "AF": (40, 60, 255),
    "FI": (255, 190, 0), "EI": (0, 200, 90), "WS": (0, 190, 170), "B6": (40, 80, 255), "NK": (255, 220, 0),
    "DE": (255, 200, 0),
}


def airline_rgb(mkt, callsign):
    if not mkt:
        op, num = _split_callsign((callsign or "").upper())
        if op in MARKETING:
            mkt = MARKETING[op][0]
        elif (callsign or "").upper().startswith("N"):
            return (200, 200, 200)
    return AIRLINE_RGB.get(mkt or "", (255, 120, 0))


def _pick_leg(airports, home_iata, departing):
    """airports: list of (iata, city). Return route dict around home airport."""
    codes = [a[0] for a in airports]
    if departing:
        for i, c in enumerate(codes[:-1]):
            if c == home_iata:
                return {"from_iata": c, "from_city": airports[i][1],
                        "to_iata": codes[i + 1], "to_city": airports[i + 1][1], "src": "db"}
    else:
        for i in range(len(codes) - 1, 0, -1):
            if codes[i] == home_iata:
                return {"from_iata": codes[i - 1], "from_city": airports[i - 1][1],
                        "to_iata": codes[i], "to_city": airports[i][1], "src": "db"}
    return None


def route_databases(cs, home_iata, departing):
    for fetch in (_vrs, _adsbdb):
        try:
            r = _pick_leg(fetch(cs), home_iata, departing)
            if r:
                return r
        except Exception:
            pass
    return None


def _vrs(cs):
    d = _get(f"https://vrs-standing-data.adsb.lol/routes/{cs[:2]}/{cs}.json")
    return [(a.get("iata") or a.get("icao"), _clean_city(a.get("iata"), a.get("location"))) for a in d.get("_airports", [])]


def _adsbdb(cs):
    fr = _get(f"https://api.adsbdb.com/v0/callsign/{cs}")["response"]["flightroute"]
    return [(x["iata_code"], _clean_city(x["iata_code"], x["municipality"])) for x in (fr["origin"], fr["destination"])]


# ---------------------------------------------------------------------------
class RouteLookup:
    def __init__(self, cfg):
        self.cfg = cfg
        self.cache = {}        # (callsign, departing) -> (timestamp, route | None | "pending")
        self.lock = threading.Lock()
        self.pool = ThreadPoolExecutor(max_workers=2)

    def resolve(self, cs, departing):
        route = None
        if self.cfg.airport_iata == "MSP":
            try:
                route = msp_board(cs, departing)
            except Exception:
                route = None
        return route or route_databases(cs, self.cfg.airport_iata, departing)

    def _fetch(self, key):
        route = self.resolve(*key)
        with self.lock:
            self.cache[key] = (time.time(), route)

    def get(self, callsign, departing):
        """Non-blocking. Returns route dict, None if unknown, or 'pending'."""
        cs = (callsign or "").strip().upper()
        if not cs:
            return None
        key = (cs, bool(departing))
        with self.lock:
            hit = self.cache.get(key)
            ttl = 4 * 3600 if hit and hit[1] else 20 * 60   # retry misses sooner
            if hit is None or (hit[1] != "pending" and time.time() - hit[0] > ttl):
                self.cache[key] = (time.time(), "pending")
                self.pool.submit(self._fetch, key)
                return "pending"
        return hit[1]
