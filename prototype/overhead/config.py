"""All tunables in one place. Override any of them with a config.json next to main.py."""
import json
import os
from dataclasses import dataclass, field, asdict


@dataclass
class Config:
    # --- Where you are -------------------------------------------------------
    # Sample spot on MSP's runway 12R approach. Set your own in config.json.
    home_lat: float = 44.9483
    home_lon: float = -93.3749
    home_elev_ft: float = 918.0

    # The airport you live under. Used to tell arrivals from departures.
    airport_icao: str = "KMSP"
    airport_iata: str = "MSP"
    airport_lat: float = 44.8820
    airport_lon: float = -93.2218

    # --- Data feed -----------------------------------------------------------
    # "local"  : your own RTL-SDR + readsb/dump1090 on the Pi (recommended, no limits)
    # "adsblol": free api.adsb.lol (capped at ~500 calls/day per IP, so we budget them)
    # "sim"    : synthetic MSP traffic for testing the display
    feed: str = "adsblol"
    local_url: str = "http://127.0.0.1/tar1090/data/aircraft.json"
    adsblol_daily_budget: int = 450      # stay under adsb.lol's 500/day limit
    adsblol_radius_nm: float = 30.0
    adsblol_hot_interval_s: float = 15.0  # poll rate while something is close
    adsblol_idle_interval_s: float = 240.0
    local_interval_s: float = 1.0
    max_extrapolate_s: float = 120.0      # drop aircraft not refreshed in this long

    # --- Acoustics -----------------------------------------------------------
    # A-weighted max level (dBA) heard at 1000 ft slant distance, by ADS-B emitter category.
    # Approach (gear/flaps out, low thrust) values, roughly from FAA AEDT NPD curves.
    src_level_approach: dict = field(default_factory=lambda: {
        "A1": 70.0,  # light piston (C172, PA28)
        "A2": 78.0,  # small: bizjets, CRJ200, turboprops
        "A3": 82.0,  # large: A320 family, 737, CRJ900, E175
        "A4": 85.0,  # B757
        "A5": 88.0,  # heavy: A330, 767, 777
        "A6": 85.0,  # high performance
        "A7": 82.0,  # rotorcraft
        "": 80.0,    # unknown
    })
    departure_boost_db: float = 8.0       # takeoff/climb thrust is a lot louder
    atmos_absorption_db_per_kft: float = 1.2
    ground_effect_max_db: float = 10.0    # extra loss for planes low on the horizon
    ground_effect_full_below_deg: float = 5.0
    ground_effect_zero_above_deg: float = 40.0
    speed_of_sound_fps: float = 1125.0

    # "Clearly audible": ambient in a quiet Minneapolis neighborhood is ~45 dBA.
    # Display turns on at on_db and off below off_db (hysteresis so it doesn't flicker).
    on_db: float = 60.0
    off_db: float = 57.0
    min_show_s: float = 6.0
    calibration_offset_db: float = 0.0    # nudge everything up/down after listening

    # --- Display -------------------------------------------------------------
    mode: str = "clock"                   # "clock" (clock, interrupted by planes) or "plane" (dark otherwise)
    clock_24h: bool = False
    clock_rgb: tuple = (255, 170, 110)    # warm white
    width: int = 32
    height: int = 8
    fps: int = 30
    scroll_px_per_s: float = 22.0
    font_mode: int = 1                    # 0 auto (big, else small if it fits, else scroll), 1 big, 2 small
    show_flight_number: bool = False      # append " DL1234" after the city
    departure_prefix: str = ">"           # departing MSP: show "> DENVER" (destination)
    unknown_text: str = ""                # when route is unknown: "" = show callsign
    brightness_follows_loudness: bool = True
    web_port: int = 8080
    display: str = "web"                  # "web", "max7219", "console", or comma list

    # --- Logging -------------------------------------------------------------
    pass_log_csv: str = "passes.csv"      # every audible pass, for calibrating by ear


def load(path: str | None = None) -> Config:
    cfg = Config()
    path = path or os.path.join(os.path.dirname(os.path.dirname(__file__)), "config.json")
    if os.path.exists(path):
        with open(path) as f:
            for k, v in json.load(f).items():
                if hasattr(cfg, k):
                    setattr(cfg, k, v)
    return cfg


def dump(cfg: Config) -> str:
    return json.dumps(asdict(cfg), indent=2)
