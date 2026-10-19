import math

EARTH_R_FT = 20_902_231.0
NM_FT = 6076.12


def dist_bearing_ft(lat1, lon1, lat2, lon2):
    """Great-circle distance (ft) and initial bearing (deg true) from point 1 to point 2."""
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dl = math.radians(lon2 - lon1)
    a = math.sin((p2 - p1) / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    d = 2 * EARTH_R_FT * math.asin(min(1.0, math.sqrt(a)))
    y = math.sin(dl) * math.cos(p2)
    x = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dl)
    return d, (math.degrees(math.atan2(y, x)) + 360) % 360


def move(lat, lon, bearing_deg, dist_ft):
    """Point reached travelling dist_ft along bearing_deg from (lat, lon)."""
    d = dist_ft / EARTH_R_FT
    b = math.radians(bearing_deg)
    p1, l1 = math.radians(lat), math.radians(lon)
    p2 = math.asin(math.sin(p1) * math.cos(d) + math.cos(p1) * math.sin(d) * math.cos(b))
    l2 = l1 + math.atan2(math.sin(b) * math.sin(d) * math.cos(p1), math.cos(d) - math.sin(p1) * math.sin(p2))
    return math.degrees(p2), math.degrees(l2)


def local_xy_ft(lat0, lon0, lat, lon):
    """Flat east/north offset in feet (fine for the ~30 nm we care about)."""
    d, b = dist_bearing_ft(lat0, lon0, lat, lon)
    return d * math.sin(math.radians(b)), d * math.cos(math.radians(b))
