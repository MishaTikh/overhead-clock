"""How loud is a given airplane at the house, right now?

Model (deliberately simple, tune with calibration_offset_db / on_db after listening):
    L = L_ref(category, phase) - 20*log10(r / 1000 ft)      spherical spreading
                               - a * (r - 1000 ft)           air absorption
                               - G(elevation angle)          ground/house/tree shielding
                               + calibration offset
r is the slant range from the house to where the plane WAS when the sound now
arriving left it (sound takes ~1.5 s from 1,600 ft, ~5 s from a mile away).
"""
import math


def ground_effect_db(cfg, elev_deg):
    lo, hi = cfg.ground_effect_full_below_deg, cfg.ground_effect_zero_above_deg
    if elev_deg <= lo:
        return cfg.ground_effect_max_db
    if elev_deg >= hi:
        return 0.0
    return cfg.ground_effect_max_db * (hi - elev_deg) / (hi - lo)


def source_level(cfg, category, departing):
    base = cfg.src_level_approach.get(category or "", cfg.src_level_approach[""])
    return base + (cfg.departure_boost_db if departing else 0.0)


def level_at(cfg, category, departing, horiz_ft, height_ft):
    height_ft = max(height_ft, 50.0)
    r = max(math.hypot(horiz_ft, height_ft), 100.0)
    elev = math.degrees(math.atan2(height_ft, horiz_ft))
    return (source_level(cfg, category, departing)
            - 20 * math.log10(r / 1000.0)
            - cfg.atmos_absorption_db_per_kft * (r - 1000.0) / 1000.0
            - ground_effect_db(cfg, elev)
            + cfg.calibration_offset_db)
