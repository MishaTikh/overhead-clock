"""Render the Overhead teaser video (docs/media/overhead-teaser.mp4).

Stock footage (Pexels) and music (Kevin MacLeod, CC BY 4.0) are downloaded on first run into
tools/teaser/assets/ (not committed); everything else is drawn in code with cairo, using the
clock's own 5x7 font. Needs:  pip install pycairo numpy imageio-ffmpeg

    python tools/teaser/make_teaser.py            # full video
    python tools/teaser/make_teaser.py --stills   # a few PNG frames for review
"""
import math
import os
import random
import subprocess
import sys
import wave

import cairo
import imageio_ffmpeg
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "prototype"))
from overhead.matrix import text_columns  # noqa: E402  same 5x7 font as the firmware

W, H, FPS = 1920, 1080, 30
DURATION = 48.0
OUT_DIR = os.path.join(ROOT, "docs", "media")
OUT_MP4 = os.path.join(OUT_DIR, "overhead-teaser.mp4")

# ---------------------------------------------------------------- palette and type
NIGHT_TOP = (0.020, 0.031, 0.075)
NIGHT_BOT = (0.075, 0.063, 0.110)
INK = (0.93, 0.92, 0.90)
MUTED = (0.60, 0.62, 0.68)
AMBER = (1.00, 0.60, 0.24)
DELTA = (1.00, 0.06, 0.10)
GOLD = (1.00, 0.78, 0.10)
CLOCK = (1.00, 0.67, 0.43)
OFF = (0.085, 0.085, 0.09)
DISPLAY_FONT = "Bahnschrift"     # DIN-like, airport-signage feel
TEXT_FONT = "Segoe UI"


# ---------------------------------------------------------------- easing and timing
def clamp(x, a=0.0, b=1.0):
    return max(a, min(b, x))


def ease(x):  # ease in-out cubic
    x = clamp(x)
    return 4 * x * x * x if x < 0.5 else 1 - (-2 * x + 2) ** 3 / 2


def ease_out(x):
    x = clamp(x)
    return 1 - (1 - x) ** 3


def window(t, a, b, fade=0.5):
    """1 inside [a, b], fading in/out over `fade` seconds."""
    return clamp((t - a) / fade) * clamp((b - t) / fade)


# ---------------------------------------------------------------- drawing helpers
def rounded_rect(ctx, x, y, w, h, r):
    ctx.new_sub_path()
    ctx.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    ctx.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    ctx.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
    ctx.arc(x + r, y + r, r, math.pi, 3 * math.pi / 2)
    ctx.close_path()


def text(ctx, s, x, y, size, color=INK, alpha=1.0, font=TEXT_FONT, bold=False, align="center", tracking=0.0):
    if alpha <= 0:
        return
    ctx.select_font_face(font, cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_BOLD if bold else cairo.FONT_WEIGHT_NORMAL)
    ctx.set_font_size(size)
    widths = [ctx.text_extents(c).x_advance for c in s]
    total = sum(widths) + tracking * max(0, len(s) - 1)
    cx = x - total / 2 if align == "center" else (x - total if align == "right" else x)
    ctx.set_source_rgba(*color, alpha)
    for c, w_ in zip(s, widths):
        ctx.move_to(cx, y)
        ctx.show_text(c)
        cx += w_ + tracking


def caption(ctx, t, s, a, b, y=H - 110):
    al = window(t, a, b, 0.45)
    if al <= 0:
        return
    rise = (1 - ease_out(clamp((t - a) / 0.6))) * 18
    text(ctx, s, W / 2, y + rise, 46, INK, al, TEXT_FONT, bold=False)


def frame_cols(text_s, t_since, speed=22.0, width=32, hold=1.0):
    """Big-letter marquee exactly like the firmware: hold, then scroll left and repeat."""
    c = text_columns(text_s)
    n = len(c)
    cycle = max(n + 12, width + 4)
    off = int(max(0.0, t_since - hold) * speed) % cycle
    return [c[(off + i) % cycle] if (off + i) % cycle < n else 0 for i in range(width)]


def clock_cols(h, m, colon=True):
    c = text_columns(str(h)) + [0, 0x14 if colon else 0, 0] + text_columns("%02d" % m)
    pad = (32 - len(c)) // 2
    return ([0] * pad + c + [0] * 32)[:32]


def draw_matrix(ctx, x, y, w, cols, color, bright=1.0, sparkle=None):
    """32x8 LED matrix of rounded-square pixels with a soft glow."""
    p = w / 32.0
    s = p * 0.78
    lit = []
    for cx in range(32):
        for cy in range(8):
            px, py = x + cx * p + (p - s) / 2, y + cy * p + (p - s) / 2
            on = (cols[cx] >> cy) & 1
            if on:
                lit.append((px, py))
            else:
                ctx.set_source_rgb(*OFF)
                rounded_rect(ctx, px, py, s, s, s * 0.18)
                ctx.fill()
    for px, py in lit:  # glow pass
        g = cairo.RadialGradient(px + s / 2, py + s / 2, s * 0.2, px + s / 2, py + s / 2, s * 1.3)
        g.add_color_stop_rgba(0, *color, 0.35 * bright)
        g.add_color_stop_rgba(1, *color, 0.0)
        ctx.set_source(g)
        ctx.arc(px + s / 2, py + s / 2, s * 1.3, 0, 2 * math.pi)
        ctx.fill()
    for px, py in lit:
        ctx.set_source_rgba(*[c * (0.55 + 0.45 * bright) for c in color], 1)
        rounded_rect(ctx, px, py, s, s, s * 0.18)
        ctx.fill()
    if sparkle:
        for (cx, cy, a) in sparkle:
            if not (cols[cx] >> cy) & 1:
                px, py = x + cx * p + p / 2, y + cy * p + p / 2
                g = cairo.RadialGradient(px, py, 0, px, py, p * 0.9)
                g.add_color_stop_rgba(0, 1, 1, 0.92, 0.9 * a)
                g.add_color_stop_rgba(1, 1, 1, 0.9, 0)
                ctx.set_source(g)
                ctx.arc(px, py, p * 0.9, 0, 2 * math.pi)
                ctx.fill()


def draw_device(ctx, cx, cy, w, cols, color, bright=1.0, pressed=None, sparkle=None, alpha=1.0):
    """The Ulanzi TC001: light shell, black face, three buttons on top."""
    h = w * 0.36
    x, y = cx - w / 2, cy - h / 2
    ctx.push_group()
    # shadow
    for i in range(14):
        ctx.set_source_rgba(0, 0, 0, 0.035)
        rounded_rect(ctx, x - i * 1.5, y + 22 - i * 0.5 + i * 2.2, w + i * 3, h, w * 0.06 + i)
        ctx.fill()
    # buttons on top
    bw, bh = w * 0.075, w * 0.022
    for i, bx in enumerate((cx - w * 0.12, cx, cx + w * 0.12)):
        down = pressed == i
        ctx.set_source_rgb(0.80, 0.80, 0.78) if not down else ctx.set_source_rgb(*AMBER)
        rounded_rect(ctx, bx - bw / 2, y - bh * (0.35 if down else 0.85), bw, bh * 1.4, bh * 0.5)
        ctx.fill()
    # body
    g = cairo.LinearGradient(0, y, 0, y + h)
    g.add_color_stop_rgb(0, 0.95, 0.95, 0.93)
    g.add_color_stop_rgb(1, 0.74, 0.74, 0.71)
    ctx.set_source(g)
    rounded_rect(ctx, x, y, w, h, w * 0.06)
    ctx.fill()
    ctx.set_source_rgba(1, 1, 1, 0.6)
    ctx.set_line_width(2)
    rounded_rect(ctx, x + 2, y + 2, w - 4, h - 4, w * 0.058)
    ctx.stroke()
    # face
    m = w * 0.045
    fx, fy, fw, fh = x + m, y + m * 0.9, w - 2 * m, h - 2 * m * 0.9
    ctx.set_source_rgb(0.03, 0.03, 0.035)
    rounded_rect(ctx, fx, fy, fw, fh, w * 0.02)
    ctx.fill()
    mw = fw * 0.94
    draw_matrix(ctx, fx + (fw - mw) / 2, fy + (fh - mw / 4) / 2, mw, cols, color, bright, sparkle)
    # glass reflection
    g = cairo.LinearGradient(fx, fy, fx + fw * 0.6, fy + fh)
    g.add_color_stop_rgba(0, 1, 1, 1, 0.07)
    g.add_color_stop_rgba(0.5, 1, 1, 1, 0.0)
    ctx.set_source(g)
    rounded_rect(ctx, fx, fy, fw, fh, w * 0.02)
    ctx.fill()
    ctx.pop_group_to_source()
    ctx.paint_with_alpha(alpha)


def plane_path(ctx, x, y, size, pitch=0.0):
    """Side-view airliner silhouette, nose to the right."""
    ctx.save()
    ctx.translate(x, y)
    ctx.rotate(pitch)
    s = size / 100.0
    ctx.scale(s, s)
    ctx.new_path()
    ctx.move_to(50, -2)                       # nose
    ctx.curve_to(46, -9, 38, -9, 30, -9)
    ctx.line_to(-38, -9)
    ctx.line_to(-48, -32)                     # tail fin
    ctx.line_to(-56, -32)
    ctx.line_to(-54, -8)
    ctx.line_to(-56, 0)
    ctx.line_to(-40, 4)
    ctx.line_to(30, 5)
    ctx.curve_to(42, 5, 49, 3, 50, -2)
    ctx.close_path()
    ctx.move_to(4, 2)                         # wing
    ctx.line_to(-18, 18)
    ctx.line_to(-10, 18)
    ctx.line_to(16, 2)
    ctx.close_path()
    ctx.move_to(-44, -4)                      # stabilizer
    ctx.line_to(-56, 6)
    ctx.line_to(-50, 6)
    ctx.line_to(-38, -3)
    ctx.close_path()
    ctx.restore()


def background(ctx, t, stars=True):
    g = cairo.LinearGradient(0, 0, 0, H)
    g.add_color_stop_rgb(0, *NIGHT_TOP)
    g.add_color_stop_rgb(1, *NIGHT_BOT)
    ctx.set_source(g)
    ctx.paint()
    if stars:
        rnd = random.Random(7)
        for i in range(220):
            sx, sy = rnd.uniform(0, W), rnd.uniform(0, H * 0.75)
            tw = 0.35 + 0.35 * math.sin(t * rnd.uniform(0.6, 2.2) + i)
            ctx.set_source_rgba(1, 1, 1, tw * rnd.uniform(0.2, 0.8))
            ctx.arc(sx, sy, rnd.uniform(0.6, 1.8), 0, 2 * math.pi)
            ctx.fill()


# ---------------------------------------------------------------- animated scenes (shared)
def scene_intro(ctx, t):
    background(ctx, t)
    a1 = window(t, 0.6, 4.6, 0.8)
    a2 = window(t, 1.9, 4.6, 0.8)
    text(ctx, "You hear it", W / 2, H / 2 - 20, 92, INK, a1, DISPLAY_FONT, tracking=1)
    text(ctx, "before you see it.", W / 2, H / 2 + 90, 92, AMBER, a2, DISPLAY_FONT, tracking=1)


GROUND = 860


def house(ctx, x, glow):
    ctx.set_source_rgb(0.05, 0.05, 0.08)
    ctx.move_to(x - 120, GROUND)
    ctx.line_to(x - 120, GROUND - 120)
    ctx.line_to(x, GROUND - 210)
    ctx.line_to(x + 120, GROUND - 120)
    ctx.line_to(x + 120, GROUND)
    ctx.close_path()
    ctx.fill()
    ctx.rectangle(x + 55, GROUND - 205, 26, 60)     # chimney
    ctx.fill()
    for wx in (x - 78, x + 22):                      # windows
        ctx.set_source_rgba(1.0, 0.72, 0.38, 0.55 + 0.45 * glow)
        ctx.rectangle(wx, GROUND - 100, 56, 48)
        ctx.fill()
    if glow > 0.02:
        g = cairo.RadialGradient(x, GROUND - 76, 10, x, GROUND - 76, 260)
        g.add_color_stop_rgba(0, 1.0, 0.55, 0.25, 0.30 * glow)
        g.add_color_stop_rgba(1, 1.0, 0.55, 0.25, 0)
        ctx.set_source(g)
        ctx.paint()


def scene_flyover(ctx, t):
    """5-14 s: side view. Plane on a 3-degree path over the house; sound rings; loudness meter."""
    lt = t - 5.0
    background(ctx, t)
    # far skyline and ground
    rnd = random.Random(3)
    ctx.set_source_rgb(0.035, 0.035, 0.06)
    x = 0
    while x < W:
        bw, bh = rnd.uniform(40, 120), rnd.uniform(20, 90)
        ctx.rectangle(x, GROUND - bh, bw, bh)
        x += bw + rnd.uniform(0, 30)
    ctx.fill()
    ctx.set_source_rgb(0.03, 0.03, 0.05)
    ctx.rectangle(0, GROUND, W, H - GROUND)
    ctx.fill()
    hx = 1180
    # glide path (dashed), descending left to right toward the airport off-screen right
    y_at = lambda px: 250 + (px + 200) * 0.17  # noqa: E731
    ctx.set_dash([14, 16])
    ctx.set_line_width(2)
    ctx.set_source_rgba(*AMBER, 0.25 * window(lt, 0.3, 9.2, 0.6))
    ctx.move_to(-200, y_at(-200))
    ctx.line_to(W + 200, y_at(W + 200))
    ctx.stroke()
    ctx.set_dash([])
    text(ctx, "3° approach", 900, y_at(900) - 24, 26, AMBER, 0.55 * window(lt, 0.6, 8.8, 0.6), TEXT_FONT, align="left")
    # plane position
    px = -260 + lt * 380
    py = y_at(px)
    # sound rings emitted along the path; radius grows at "speed of sound" (scaled)
    loud_at_house = 0.0
    c = 1.0
    for k in range(60):
        te = k * 0.18
        if te > lt:
            break
        ex = -260 + te * 380
        ey = y_at(ex)
        r = (lt - te) * 520
        d_house = math.hypot(hx - ex, (GROUND - 120) - ey)
        strength = clamp(1 - d_house / 1500)
        if r < 1600:
            ctx.set_line_width(2.2)
            ctx.set_source_rgba(*AMBER, 0.22 * clamp(1 - r / 1500) * window(lt, 0, 9.4, 0.4))
            ctx.arc(ex, ey, r, 0, 2 * math.pi)
            ctx.stroke()
        if abs(r - d_house) < 260:  # this ring is passing the house now
            loud_at_house = max(loud_at_house, strength)
    house(ctx, hx, ease(loud_at_house * 1.3))
    # plane with blinking beacon
    ctx.set_source_rgb(0.80, 0.82, 0.88)
    plane_path(ctx, px, py, 200, math.atan(0.17))
    ctx.fill()
    if int(lt * 2.5) % 2 == 0:
        ctx.set_source_rgba(1, 0.2, 0.2, 0.9)
        ctx.arc(px - 6, py + 2, 4, 0, 2 * math.pi)
        ctx.fill()
    # loudness meter
    db = 40 + 38 * loud_at_house
    ma = window(lt, 0.6, 8.9, 0.5)
    mx, my, mw = 120, 120, 520
    text(ctx, "AT THE HOUSE", mx, my, 24, MUTED, ma, TEXT_FONT, align="left", tracking=3)
    text(ctx, "%d dBA" % round(db), mx, my + 76, 66, INK, ma, DISPLAY_FONT, align="left")
    ctx.set_source_rgba(1, 1, 1, 0.12 * ma)
    rounded_rect(ctx, mx, my + 104, mw, 14, 7)
    ctx.fill()
    frac = clamp((db - 35) / 50)
    ctx.set_source_rgba(*(AMBER if db >= 60 else MUTED), ma)
    rounded_rect(ctx, mx, my + 104, max(14, mw * frac), 14, 7)
    ctx.fill()
    thr = mx + mw * (60 - 35) / 50
    ctx.set_source_rgba(1, 1, 1, 0.6 * ma)
    ctx.rectangle(thr - 1.5, my + 96, 3, 30)
    ctx.fill()
    audible = db >= 60
    text(ctx, "clearly audible" if audible else "quiet street ~45", mx, my + 160, 28,
         AMBER if audible else MUTED, ma, TEXT_FONT, align="left")
    caption(ctx, t, "But only while you can actually hear it.", 6.0, 11.4)


def scene_device(ctx, t):
    """14-22 s: the clock, then DENVER in Delta red while a plane is audible."""
    background(ctx, t, stars=True)
    lt = t - 14.0
    zoom = ease_out(clamp(lt / 1.2))
    w = 1180 * (0.85 + 0.15 * zoom)
    active = lt > 2.0
    if active:
        cols = frame_cols("DENVER", lt - 2.0)
        color, bright = DELTA, 0.6 + 0.4 * math.sin(clamp((lt - 2.0) / 6.0) * math.pi)
    else:
        cols, color, bright = clock_cols(9, 41, int(t * 2) % 2 == 0), CLOCK, 0.75
    draw_device(ctx, W / 2, H / 2 - 30, w, cols, color, bright, alpha=zoom)
    caption(ctx, t, "Then it shows where the plane came from.", 16.2, 21.8)


FORMATS = [
    ("CITY", "DENVER"),
    ("CODES", "DEN > MSP"),
    ("AIRLINE", "DELTA DEN > MSP"),
    ("FULL", "DELTA 1902  DENVER > MINNEAPOLIS  A321"),
]


def scene_formats(ctx, t):
    """22-30 s: left button cycles the four display styles."""
    background(ctx, t)
    lt = t - 22.0
    idx = min(3, int(lt / 2.0))
    since = lt - idx * 2.0
    pressed = 0 if since < 0.25 else None
    name, s = FORMATS[idx]
    cols = frame_cols(s, since, speed=26, hold=0.35)
    draw_device(ctx, W / 2, H / 2 - 10, 1180, cols, DELTA, 0.9, pressed=pressed)
    # format tabs above the device
    for i, (n, _) in enumerate(FORMATS):
        x = W / 2 + (i - 1.5) * 230
        on = i == idx
        text(ctx, n, x, 215, 34, AMBER if on else MUTED, 1.0 if on else 0.55, DISPLAY_FONT, tracking=3)
        if on:
            ctx.set_source_rgba(*AMBER, 1)
            rounded_rect(ctx, x - 40, 232, 80, 5, 2.5)
            ctx.fill()
    caption(ctx, t, "Four ways to show it. One button.", 22.3, 29.8)


def scene_rare(ctx, t):
    """30-35 s: a rare visitor in gold, with sparkles."""
    background(ctx, t)
    lt = t - 30.0
    rnd = random.Random(int(t * 12))
    sparkle = [(rnd.randrange(32), rnd.randrange(8), rnd.uniform(0.4, 1.0)) for _ in range(6)]
    cols = frame_cols("747!  LONDON", lt, speed=22, hold=0.8)
    pulse = 0.8 + 0.2 * math.sin(lt * 6)
    draw_device(ctx, W / 2, H / 2 - 10, 1180, cols, GOLD, pulse, sparkle=sparkle)
    caption(ctx, t, "Rare visitors get the gold treatment.", 30.3, 34.8)


def phone(ctx, x, y, t, lt):
    pw, ph = 420, 860
    ctx.set_source_rgb(0.06, 0.06, 0.07)
    rounded_rect(ctx, x, y, pw, ph, 56)
    ctx.fill()
    ctx.set_source_rgb(0.11, 0.115, 0.13)
    rounded_rect(ctx, x + 14, y + 14, pw - 28, ph - 28, 44)
    ctx.fill()
    ix = x + 44
    text(ctx, "Overhead Clock", ix, y + 110, 32, INK, 1, TEXT_FONT, bold=True, align="left")
    ctx.set_source_rgba(0.36, 0.77, 0.55, 1)
    rounded_rect(ctx, ix + 250, y + 84, 82, 34, 17)
    ctx.set_line_width(2)
    ctx.stroke()
    text(ctx, "live", ix + 291, y + 108, 20, (0.36, 0.77, 0.55), 1, TEXT_FONT)
    draw_matrix(ctx, ix, y + 150, pw - 88, clock_cols(9, 41), CLOCK, 0.8)
    text(ctx, "TODAY", ix, y + 290, 18, MUTED, 1, TEXT_FONT, align="left", tracking=2)
    n = int(41 * ease_out(clamp(lt / 1.5)))
    text(ctx, str(n), ix, y + 360, 64, INK, 1, DISPLAY_FONT, align="left")
    text(ctx, "planes heard", ix, y + 392, 22, MUTED, 1, TEXT_FONT, align="left")
    text(ctx, "Amsterdam", ix + 170, y + 352, 36, INK, 1, DISPLAY_FONT, align="left")
    text(ctx, "farthest, 4,170 mi", ix + 170, y + 392, 22, MUTED, 1, TEXT_FONT, align="left")
    usual = [0, 0, 0, 0, 0, .4, 2.1, 4.2, 3.8, 3.1, 3.4, 3.0, 2.6, 2.9, 3.3, 3.7, 4.1, 3.6, 2.8, 2.2, 1.4, .6, .2, 0]
    today = [0, 0, 0, 0, 0, 0, 2, 5, 4, 3, 4, 3, 2, 3, 4, 3, 3, 2, 1, 1, 0, 0, 0, 0]
    bx, by, bwid = ix, y + 560, (pw - 88) / 24
    grow = ease_out(clamp((lt - 0.4) / 1.6))
    for h_ in range(24):
        uh = usual[h_] / 5 * 120 * grow
        th = today[h_] / 5 * 120 * grow
        ctx.set_source_rgba(1, 1, 1, 0.12)
        ctx.rectangle(bx + h_ * bwid + 1, by - uh, bwid - 2, uh)
        ctx.fill()
        ctx.set_source_rgba(*AMBER, 1)
        ctx.rectangle(bx + h_ * bwid + bwid * 0.25, by - th, bwid * 0.5, th)
        ctx.fill()
    text(ctx, "today vs a usual day", ix, by + 34, 20, MUTED, 1, TEXT_FONT, align="left")
    ba = clamp((lt - 1.6) / 0.5)
    ctx.set_source_rgba(0.36, 0.77, 0.55, ba)
    rounded_rect(ctx, ix, y + 650, 300, 50, 25)
    ctx.set_line_width(2.5)
    ctx.stroke()
    text(ctx, "Landings over you (12R)", ix + 150, y + 683, 22, (0.36, 0.77, 0.55), ba, TEXT_FONT)
    text(ctx, "MSP: 45°F, wind SE 13 mph", ix, y + 750, 22, MUTED, ba, TEXT_FONT, align="left")


def scene_phone(ctx, t):
    """35-41 s: set up and watch from your phone."""
    background(ctx, t)
    lt = t - 35.0
    slide = ease(clamp(lt / 0.9))
    draw_device(ctx, W / 2 - 330 * slide, H / 2 - 40, 1060 - 160 * slide, clock_cols(9, 42, int(t * 2) % 2 == 0), CLOCK, 0.75)
    phone(ctx, W + 40 - 640 * slide, 110, t, lt)
    caption(ctx, t, "Set up from your phone. Works near any airport.", 35.4, 40.8)


def scene_outro(ctx, t):
    """41-48 s: OVERHEAD assembles from LED dots."""
    background(ctx, t)
    lt = t - 41.0
    cols = text_columns("OVERHEAD")
    n = len(cols)
    p = 30
    x0 = (W - n * p) / 2
    y0 = 300
    for cx in range(n):
        for cy in range(8):
            if not (cols[cx] >> cy) & 1:
                continue
            delay = cx * 0.022 + cy * 0.015
            k = ease_out(clamp((lt - delay) / 0.7))
            if k <= 0:
                continue
            rnd = random.Random(cx * 31 + cy)
            sx, sy = rnd.uniform(0, W), rnd.uniform(-200, H + 200)
            px = sx + (x0 + cx * p - sx) * k
            py = sy + (y0 + cy * p - sy) * k
            g = cairo.RadialGradient(px + p / 2, py + p / 2, 2, px + p / 2, py + p / 2, p * 1.2)
            g.add_color_stop_rgba(0, *AMBER, 0.35 * k)
            g.add_color_stop_rgba(1, *AMBER, 0)
            ctx.set_source(g)
            ctx.arc(px + p / 2, py + p / 2, p * 1.2, 0, 2 * math.pi)
            ctx.fill()
            ctx.set_source_rgba(*AMBER, k)
            rounded_rect(ctx, px + 3, py + 3, p - 6, p - 6, 4)
            ctx.fill()
    a = window(t, 42.6, 48.5, 0.6)
    text(ctx, "Open-source firmware for the Ulanzi TC001 pixel clock", W / 2, 680, 42, INK, a, TEXT_FONT)
    a2 = window(t, 43.6, 48.5, 0.6)
    text(ctx, "github.com/MishaTikh/overhead-clock", W / 2, 760, 34, AMBER, a2, DISPLAY_FONT, tracking=1)
    a3 = window(t, 44.4, 48.5, 0.6)
    text(ctx, "Free  ·  MIT licensed  ·  No cloud account", W / 2, 830, 28, MUTED, a3, TEXT_FONT)


# ---------------------------------------------------------------- stock footage
ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
# Stock media (downloaded by fetch_assets(); not committed). Pexels License: free, no attribution
# required. Music: CC BY 4.0, credited in the video and in the README.
MEDIA = {
    "approach_melbourne.mp4": "https://videos.pexels.com/video-files/36253286/15374391_1920_1080_30fps.mp4",
    "landing_trees.mp4": "https://videos.pexels.com/video-files/12086902/12086902-hd_1920_1080_60fps.mp4",
    "overhead_melbourne.mp4": "https://videos.pexels.com/video-files/36248449/15372581_1920_1080_30fps.mp4",
    "fluffing_a_duck.mp3": "https://incompetech.com/music/royalty-free/mp3-royaltyfree/Fluffing%20a%20Duck.mp3",
}
CREDITS = ("Footage: Joolsmagools and Sammy on Pexels  ·  Music: “Fluffing a Duck” by Kevin MacLeod "
           "(incompetech.com), CC BY 4.0")


def fetch_assets():
    import urllib.request
    os.makedirs(ASSETS, exist_ok=True)
    for name, url in MEDIA.items():
        path = os.path.join(ASSETS, name)
        if not os.path.exists(path):
            req = urllib.request.Request(url, headers={"User-Agent": "overhead-clock-teaser (+https://github.com/MishaTikh/overhead-clock)"})
            with urllib.request.urlopen(req) as r, open(path, "wb") as f:
                f.write(r.read())


class Footage:
    """Streams a clip's frames at 30 fps; frames are requested in increasing time order."""

    def __init__(self, name, start=0.0):
        self.path = os.path.join(ASSETS, name)
        self.start = start
        self.gen = None
        self.index = -1
        self.frame = None

    def surface(self, t):
        if self.gen is None:
            self.gen = imageio_ffmpeg.read_frames(self.path, pix_fmt="rgb24",
                                                  input_params=["-ss", "%.3f" % self.start],
                                                  output_params=["-r", str(FPS), "-s", f"{W}x{H}"])
            next(self.gen)  # metadata
        want = int(t * FPS)
        while self.index < want:
            try:
                raw = next(self.gen)
            except StopIteration:
                break  # hold the last frame
            self.index += 1
            rgb = np.frombuffer(raw, np.uint8).reshape(H, W, 3)
            bgra = np.empty((H, W, 4), np.uint8)
            bgra[..., 0], bgra[..., 1], bgra[..., 2], bgra[..., 3] = rgb[..., 2], rgb[..., 1], rgb[..., 0], 255
            self.frame = bgra
        return cairo.ImageSurface.create_for_data(memoryview(self.frame), cairo.FORMAT_RGB24, W, H)


def paint_footage(ctx, clip, lt, length, zoom_to=1.06, grade=0.0):
    """Full-frame footage with a slow push-in, optional dark grade, and a bottom vignette for text."""
    surf = clip.surface(lt)
    z = 1.0 + (zoom_to - 1.0) * clamp(lt / length)
    ctx.save()
    ctx.translate(W / 2, H / 2)
    ctx.scale(z, z)
    ctx.translate(-W / 2, -H / 2)
    ctx.set_source_surface(surf, 0, 0)
    ctx.paint()
    ctx.restore()
    if grade:
        ctx.set_source_rgba(0.02, 0.03, 0.08, grade)
        ctx.paint()
    g = cairo.LinearGradient(0, H * 0.55, 0, H)
    g.add_color_stop_rgba(0, 0, 0, 0, 0)
    g.add_color_stop_rgba(1, 0, 0, 0, 0.6)
    ctx.set_source(g)
    ctx.rectangle(0, 0, W, H)
    ctx.fill()


def shadow_text(ctx, s, x, y, size, color=INK, alpha=1.0, font=DISPLAY_FONT, tracking=0.0):
    text(ctx, s, x + 3, y + 4, size, (0, 0, 0), alpha * 0.55, font, tracking=tracking)
    text(ctx, s, x, y, size, color, alpha, font, tracking=tracking)


CLIPS = {}


def clip(name, start=0.0):
    if name not in CLIPS:
        CLIPS[name] = Footage(name, start)
    return CLIPS[name]


# ---------------------------------------------------------------- scenes
def scene_open(ctx, t):
    """0-4.5 s: a jet on approach grows in the frame."""
    paint_footage(ctx, clip("approach_melbourne.mp4", 2.9), t, 4.5, 1.05)
    shadow_text(ctx, "You hear it", W / 2, H - 230, 96, INK, window(t, 0.4, 4.4, 0.5), tracking=1)
    shadow_text(ctx, "before you see it.", W / 2, H - 120, 96, AMBER, window(t, 1.4, 4.4, 0.5), tracking=1)


def scene_trees(ctx, t):
    """4.5-9.5 s: a Dreamliner low over the trees, real engine sound."""
    lt = t - 4.5
    paint_footage(ctx, clip("landing_trees.mp4"), lt, 5.0, 1.08)
    shadow_text(ctx, "Live under a flight path?", W / 2, H - 120, 72, INK, window(lt, 0.5, 4.9, 0.5), TEXT_FONT)


def scene_overhead(ctx, t):
    """9.5-16.5 s: a jet banks overhead while the clock slides in and names its origin."""
    lt = t - 9.5
    paint_footage(ctx, clip("overhead_melbourne.mp4"), lt, 7.0, 1.05)
    slide = ease_out(clamp((lt - 0.8) / 0.9))
    if slide > 0:
        cols = frame_cols("DENVER", max(0.0, lt - 1.4), speed=22, hold=0.8)
        draw_device(ctx, W - 520, H - 300 + (1 - slide) * 420, 820, cols, (1.0, 0.47, 0.0), 0.95, alpha=slide)  # default plane color: airline unknown
    shadow_text(ctx, "Overhead tells you where it came from.", W / 2, 140, 64, INK, window(lt, 1.6, 6.9, 0.5), TEXT_FONT)


def scene_rings(ctx, t):
    """16.5-23 s: the idea in one diagram: loud enough to hear?"""
    scene_flyover(ctx, t - 11.5)  # scene_flyover counts from 5 s


def scene_formats2(ctx, t):
    """23-29 s: left button cycles the four display styles (1.5 s each)."""
    background(ctx, t)
    lt = t - 23.0
    idx = min(3, int(lt / 1.5))
    since = lt - idx * 1.5
    pressed = 0 if since < 0.22 else None
    name, s = FORMATS[idx]
    cols = frame_cols(s, since, speed=30, hold=0.25)
    draw_device(ctx, W / 2, H / 2 - 10, 1180, cols, DELTA, 0.9, pressed=pressed)
    for i, (n, _) in enumerate(FORMATS):
        x = W / 2 + (i - 1.5) * 230
        on = i == idx
        text(ctx, n, x, 215, 34, AMBER if on else MUTED, 1.0 if on else 0.55, DISPLAY_FONT, tracking=3)
        if on:
            ctx.set_source_rgba(*AMBER, 1)
            rounded_rect(ctx, x - 40, 232, 80, 5, 2.5)
            ctx.fill()
    caption(ctx, t, "Four ways to show it. One button.", 23.2, 28.9)


def scene_rare2(ctx, t):
    background(ctx, t)
    lt = t - 29.0
    rnd = random.Random(int(t * 12))
    sparkle = [(rnd.randrange(32), rnd.randrange(8), rnd.uniform(0.4, 1.0)) for _ in range(6)]
    cols = frame_cols("A350!  AMSTERDAM", lt, speed=24, hold=0.6)
    draw_device(ctx, W / 2, H / 2 - 10, 1180, cols, GOLD, 0.8 + 0.2 * math.sin(lt * 6), sparkle=sparkle)
    caption(ctx, t, "Rare visitors get the gold treatment.", 29.2, 32.9)


def scene_phone2(ctx, t):
    background(ctx, t)
    lt = t - 33.0
    slide = ease(clamp(lt / 0.9))
    draw_device(ctx, W / 2 - 330 * slide, H / 2 - 40, 1060 - 160 * slide, clock_cols(9, 42, int(t * 2) % 2 == 0), CLOCK, 0.75)
    phone(ctx, W + 40 - 640 * slide, 110, t, lt)
    caption(ctx, t, "Set up from your phone. Works near any airport.", 33.3, 38.4)


def scene_outro2(ctx, t):
    scene_outro(ctx, t + (41.0 - 38.5))  # same animation, shifted to start at 38.5 s
    text(ctx, CREDITS, W / 2, H - 46, 20, MUTED, window(t, 40.0, 46.0, 0.6), TEXT_FONT)


DURATION = 45.0
SCENES = [(0, 4.5, scene_open), (4.5, 9.5, scene_trees), (9.5, 16.5, scene_overhead), (16.5, 23, scene_rings),
          (23, 29, scene_formats2), (29, 33, scene_rare2), (33, 38.5, scene_phone2), (38.5, 46, scene_outro2)]


def render(ctx, t):
    for i, (a, b, fn) in enumerate(SCENES):
        if a <= t < b:
            fn(ctx, t)
            edge = min(t - a, b - t)
            if (i > 0 and t - a < 0.3) or (i < len(SCENES) - 1 and b - t < 0.3):
                ctx.set_source_rgba(0, 0, 0, 1 - clamp(edge / 0.3))
                ctx.paint()
            break
    if t > DURATION - 0.8:
        ctx.set_source_rgba(0, 0, 0, clamp((t - (DURATION - 0.8)) / 0.8))
        ctx.paint()


# ---------------------------------------------------------------- soundtrack: music + real jet sound
SR = 48000


def decode_audio(path):
    ff = imageio_ffmpeg.get_ffmpeg_exe()
    raw = subprocess.run([ff, "-loglevel", "error", "-i", path, "-f", "f32le", "-ac", "1", "-ar", str(SR), "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.float32).astype(np.float64)


def soundtrack(path):
    n = int(DURATION * SR)
    t = np.arange(n) / SR
    out = np.zeros(n)
    music = decode_audio(os.path.join(ASSETS, "fluffing_a_duck.mp3"))[:n]
    music = np.pad(music, (0, n - len(music)))
    jet = decode_audio(os.path.join(ASSETS, "landing_trees.mp4"))
    jet /= np.abs(jet).max() + 1e-9

    def place(sample, start, gain, fade=0.4):
        i0 = int(start * SR)
        s = sample[: max(0, min(len(sample), n - i0))].copy()
        f = int(fade * SR)
        s[:f] *= np.linspace(0, 1, f)
        s[-f:] *= np.linspace(1, 0, f)
        out[i0:i0 + len(s)] += gain * s
        env = np.zeros(n)
        env[i0:i0 + len(s)] = np.abs(s).mean() and 1.0
        return env

    jet_env = np.zeros(n)
    jet_env += place(jet, 0.6, 0.55)     # opener: the approach
    jet_env += place(jet, 4.5, 0.95)     # the Dreamliner's own sound
    jet_env += place(jet, 10.6, 0.70)    # the jet overhead
    jet_env += place(jet, 17.6, 0.80)    # the diagram's flyover
    jet_env = np.convolve(np.clip(jet_env, 0, 1), np.ones(SR // 4) / (SR // 4), mode="same")
    duck = 1 - 0.55 * jet_env            # music dips under the jets
    fade = np.clip(t / 0.4, 0, 1) * np.clip((DURATION - t) / 3.0, 0, 1)
    out += 0.42 * music * duck * fade
    rng = np.random.default_rng(1)
    for k in range(4):                   # button clicks
        c0 = int((23.0 + k * 1.5) * SR)
        out[c0:c0 + 480] += np.exp(-np.arange(480) / 70) * rng.standard_normal(480) * 0.22
    for start, freq, dur in ((29.25, 1760, 0.09), (29.45, 2349, 0.14)):  # the firmware's rare-aircraft chime
        i0 = int(start * SR)
        k = np.arange(int(dur * SR * 3)) / SR
        out[i0:i0 + len(k)] += 0.16 * np.sin(2 * np.pi * freq * k) * np.exp(-k / dur)
    out = np.tanh(out * 1.2) * 0.85
    pcm = (out * 32767).astype(np.int16)
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SR)
        wf.writeframes(pcm.tobytes())


# ---------------------------------------------------------------- main
def main():
    fetch_assets()
    os.makedirs(OUT_DIR, exist_ok=True)
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, W, H)
    ctx = cairo.Context(surface)
    ctx.set_antialias(cairo.ANTIALIAS_BEST)
    if "--stills" in sys.argv:
        for t in (3.6, 7.0, 13.5, 20.3, 26.0, 30.5, 36.5, 43.0):
            CLIPS.clear()
            render(ctx, t)
            surface.write_to_png(os.path.join(OUT_DIR, "still_%04.1f.png" % t))
        print("stills written to", OUT_DIR)
        return
    wav = os.path.join(OUT_DIR, "_teaser_audio.wav")
    soundtrack(wav)
    ff = imageio_ffmpeg.get_ffmpeg_exe()
    cmd = [ff, "-y", "-loglevel", "error",
           "-f", "rawvideo", "-pix_fmt", "bgra", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-",
           "-i", wav, "-c:v", "libx264", "-preset", "slow", "-crf", "21", "-pix_fmt", "yuv420p",
           "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", "-shortest", OUT_MP4]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    frames = int(DURATION * FPS)
    for i in range(frames):
        render(ctx, i / FPS)
        surface.flush()
        proc.stdin.write(bytes(surface.get_data()))
        if i % 150 == 0:
            print(f"{i}/{frames} frames", flush=True)
    proc.stdin.close()
    proc.wait()
    os.remove(wav)
    print("wrote", os.path.relpath(OUT_MP4, ROOT))


if __name__ == "__main__":
    main()
