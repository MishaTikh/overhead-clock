"""Build docs/Overhead-Quick-Start.pdf (and the .html it's printed from).

LED illustrations are drawn from the clock's own 5x7 font. Printing uses Microsoft Edge or Google Chrome headless:

    python tools/make_quickstart.py
"""
import html
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "prototype"))
from overhead.matrix import text_columns  # noqa: E402  (same font table as the firmware)

OUT_HTML = os.path.join(ROOT, "docs", "quickstart.html")
OUT_PDF = os.path.join(ROOT, "docs", "Overhead-Quick-Start.pdf")


def led(text, color="#ff9a3c", width=32, scroll_offset=0, cols=None):
    """SVG of the 32x8 matrix showing `text` (first 32 columns, or a scroll position)."""
    if cols is None:
        c = text_columns(text)
        if len(c) <= width:
            pad = (width - len(c)) // 2
            cols = [0] * pad + c + [0] * (width - len(c) - pad)
        else:
            cols = (c + [0] * 12)[scroll_offset:scroll_offset + width]
            cols += [0] * (width - len(cols))
    p, r = 10, 3.6
    dots = []
    for x in range(width):
        for y in range(8):
            on = (cols[x] >> y) & 1
            dots.append(f'<circle cx="{x * p + p / 2:.1f}" cy="{y * p + p / 2:.1f}" r="{r}" fill="{color if on else "#1d1d1d"}"/>')
    return (f'<svg class="led" viewBox="0 0 {width * p} {8 * p}" role="img" aria-label="{html.escape(text)}">'
            f'<rect width="100%" height="100%" rx="4" fill="#0b0b0b"/>{"".join(dots)}</svg>')


def clock_cols(h, m):
    c = text_columns(str(h)) + [0, 0x14, 0] + text_columns("%02d" % m)
    pad = (32 - len(c)) // 2
    return [0] * pad + c + [0] * (32 - len(c) - pad)


def inbound_cols(secs):
    """Plane icon partway across, seconds right-aligned (as the firmware draws the countdown)."""
    num = text_columns(str(secs))
    cols = [0] * 32
    cols[32 - len(num):] = num
    icon = [0x08, 0x08, 0x1C, 0x7F, 0x1C, 0x08, 0x1C]
    x = 9
    cols[x:x + 7] = icon
    return cols


CSS = """
@page { size: Letter; margin: 0.55in 0.6in; }
:root { --ink: #1b1d21; --muted: #5f646b; --line: #dcd8d0; --accent: #c4410f; --soft: #f5f2ec; }
* { box-sizing: border-box; }
body { margin: 0; color: var(--ink); font: 10.5pt/1.45 "Segoe UI", "Helvetica Neue", Arial, sans-serif; }
h1 { font-size: 30pt; margin: 0; letter-spacing: -0.02em; }
h2 { font-size: 15pt; margin: 0 0 8pt; letter-spacing: -0.01em; }
h3 { font-size: 11pt; margin: 12pt 0 4pt; }
p { margin: 0 0 7pt; }
.page { page-break-after: always; }
.page:last-child { page-break-after: auto; }
.eyebrow { font: 600 8.5pt "Segoe UI", Arial, sans-serif; letter-spacing: 0.12em; text-transform: uppercase; color: var(--accent); }
.lede { font-size: 13pt; color: var(--muted); max-width: 34em; margin: 6pt 0 10pt; }
.led { width: 100%; height: auto; display: block; }
.device { background: linear-gradient(#efefec, #d3d3ce); border-radius: 14pt; padding: 10pt 12pt 8pt; margin: 6pt auto 4pt; max-width: 5.2in; }
.device .keys { display: flex; justify-content: center; gap: 10pt; margin-top: 6pt; }
.device .keys i { width: 22pt; height: 4pt; border-radius: 2pt; background: #b5b5b0; }
.caption { color: var(--muted); font-size: 9pt; }
.grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 14pt 18pt; }
.grid3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12pt; }
.card { border: 1pt solid var(--line); border-radius: 8pt; padding: 10pt 12pt; break-inside: avoid; }
.steps { counter-reset: s; display: grid; gap: 10pt; }
.step { display: grid; grid-template-columns: 26pt 1fr; gap: 8pt; break-inside: avoid; }
.step::before { counter-increment: s; content: counter(s); width: 22pt; height: 22pt; border-radius: 50%;
  background: var(--accent); color: #fff; font-weight: 700; display: grid; place-items: center; font-size: 11pt; }
.step b { display: block; font-size: 11pt; margin-bottom: 2pt; }
table { width: 100%; border-collapse: collapse; font-size: 9.5pt; }
th, td { text-align: left; padding: 5pt 6pt; border-bottom: 1pt solid var(--line); vertical-align: top; }
th { color: var(--muted); font-weight: 600; font-size: 8.5pt; text-transform: uppercase; letter-spacing: 0.06em; }
code, .mono { font-family: "Cascadia Mono", Consolas, monospace; font-size: 9pt; background: var(--soft); padding: 0 3pt; border-radius: 3pt; }
.note { background: var(--soft); border-radius: 8pt; padding: 9pt 12pt; font-size: 9.5pt; }
.fmt { display: grid; grid-template-columns: 70pt 1fr 150pt; gap: 8pt; align-items: center; padding: 6pt 0; border-bottom: 1pt solid var(--line); }
.fmt b { font-size: 10pt; }
.foot { margin-top: 14pt; color: var(--muted); font-size: 8.5pt; }
.btnmap { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10pt; text-align: center; }
.btnmap .k { width: 42pt; height: 7pt; border-radius: 4pt; background: #b5b5b0; margin: 0 auto 6pt; }
.btnmap b { display: block; }
"""


def page1():
    return f"""
<section class="page">
  <div class="eyebrow">Quick start guide</div>
  <h1>Overhead</h1>
  <p class="lede">A pixel clock that shows where the plane over your house came from, or where it's going,
  only while you can actually hear it.</p>
  <div class="device">{led("DENVER", "#ff1a1a")}<div class="keys"><i></i><i></i><i></i></div></div>
  <p class="caption" style="text-align:center">A Delta flight from Denver passing overhead. Between planes it's a clock.</p>

  <h2 style="margin-top:12pt">What you need</h2>
  <div class="grid3">
    <div class="card"><b>Ulanzi TC001</b><br>The pixel clock (about $40&ndash;50). Nothing to open or solder.</div>
    <div class="card"><b>Your phone</b><br>To put the clock on your Wi-Fi and to use its settings page.</div>
    <div class="card"><b>A Windows PC</b><br>For the one-click installer. Or any browser, using the clock's update page.</div>
  </div>

  <h2 style="margin-top:12pt">Three steps, all over Wi-Fi</h2>
  <div class="steps">
    <div class="step"><div><b>Put the clock on your Wi-Fi</b>Plug it in. A new TC001 shows <span class="mono">192.168.4.1</span>:
      join the clock's Wi-Fi network on your phone, open <span class="mono">http://192.168.4.1</span> and choose your home
      network. The clock then joins it.</div></div>
    <div class="step"><div><b>Install Overhead</b>On a Windows PC on the same Wi-Fi, download
      <span class="mono">overhead-installer.zip</span> from the project's Releases page on GitHub
      (github.com/MishaTikh/overhead-clock), unzip it and double-click <span style="font-weight:600">Install-Overhead.bat</span>. It finds the clock and
      installs Overhead in about 30 seconds. On a Mac, Linux or a phone: open <span class="mono">http://clock.local/update</span>,
      choose <span class="mono">overhead-tc001-app.bin</span> and upload.</div></div>
    <div class="step"><div><b>Sign in and set your address</b>Hold the clock's middle button for 3 seconds, until the display changes: it scrolls its settings
      address, then the 12-character password four characters at a time (<span class="mono">K7PR</span>,
      <span class="mono">3XQ9</span>, <span class="mono">TMWA</span>), each standing still. The left and right buttons step
      back and forward. Open that address, sign in as <span class="mono">admin</span> and type your street address.</div></div>
  </div>
</section>"""


def page2():
    fmts = [
        ("City", "DENVER", "Where a landing plane came from, or where a departing plane is going. The default."),
        ("Codes", "DEN > MSP", "Airport codes, from and to."),
        ("Airline", "DELTA DEN > MSP", "Airline name with the codes."),
        ("Full", "DELTA 1902  DENVER > MINNEAPOLIS  A321", "Everything: flight, cities and aircraft type."),
    ]
    rows = "".join(f'<div class="fmt"><b>{n}</b><div>{d}</div>{led(t, "#ff1a1a")}</div>' for n, t, d in fmts)
    return f"""
<section class="page">
  <div class="eyebrow">Using it</div>
  <h2>Three buttons</h2>
  <div class="device">{led("", cols=clock_cols(9, 41), color="#ffaa6e")}</div>
  <div class="btnmap">
    <div><div class="k"></div><b>Left</b>Press: what to show for each plane (cycles the four styles below).<br>
      Hold: replay recent planes.<br><span class="caption">Held while plugging in: forget Wi-Fi.</span></div>
    <div><div class="k"></div><b>Middle</b>Press: switch between <i>Clock + planes</i> and <i>Planes only</i>.<br>
      Hold: show the settings address and password.</div>
    <div><div class="k"></div><b>Right</b>Press: brightness, cycling Auto, Low, Medium, High.<br>
      <span class="caption">Auto follows the room light.</span></div>
  </div>

  <h2 style="margin-top:18pt">What it shows for each plane</h2>
  {rows}
  <p class="caption" style="margin-top:6pt">Long text scrolls. If a flight's route isn't known, every style shows the
  flight number. Colors follow the airline (Delta red, United blue, Sun Country orange&hellip;).</p>

  <h2 style="margin-top:14pt">Two modes</h2>
  <div class="grid2">
    <div class="card"><b>Clock + planes</b> (default)<br>Shows the time; switches to the flight while a plane is
      audible, then back.</div>
    <div class="card"><b>Planes only</b><br>Stays dark until a plane is audible. Nice for a bedroom or a shelf you
      don't want glowing.</div>
  </div>
</section>"""


def page3():
    return f"""
<section class="page">
  <div class="eyebrow">Settings page</div>
  <h2>Everything else is on your phone</h2>
  <p>Open <span class="mono">http://overhead.local</span> (user <span class="mono">admin</span>). Changes save as you
  make them. Some phones don't open <span class="mono">.local</span> names: hold the middle button to see the clock's
  number instead.</p>
  <table>
    <tr><th style="width:26%">Section</th><th>What you can do</th></tr>
    <tr><td><b>Today</b></td><td>Planes heard today, the farthest origin, the loudest plane, rare visitors, and an
      hour-by-hour chart against a usual day (learned over the first week).</td></tr>
    <tr><td><b>Airport weather</b></td><td>Temperature and wind at your airport, and whether the wind has planes
      <i>landing over you</i>, <i>taking off over you</i> or going elsewhere today.</td></tr>
    <tr><td><b>Home location</b></td><td>Street address (looked up once) or coordinates if you'd rather not send an
      address anywhere. Airport (nearest by default) and time zone (automatic).</td></tr>
    <tr><td><b>What the sign shows</b></td><td>Mode, display style, 12 or 24-hour clock.</td></tr>
    <tr><td><b>Alerts</b></td><td>Rare aircraft in gold with a sparkle; an optional chime; an inbound countdown with a
      little plane icon; an evening recap at the hour you choose.</td></tr>
    <tr><td><b>Clock screen extras</b></td><td>Every so often, briefly show: indoor temperature and humidity, outdoor
      weather, date, sunrise and sunset, whether planes are over you today, planes heard today. All off by default.</td></tr>
    <tr><td><b>Colors</b></td><td>Clock color, airline colors on or off, and a color for other planes.</td></tr>
    <tr><td><b>Brightness and text</b></td><td>Auto, low, medium or high; dark-room and daylight levels; scroll speed;
      try any text on the sign.</td></tr>
    <tr><td><b>Quiet hours</b></td><td>No plane alerts or data requests overnight (11 pm&ndash;6 am by default). The clock
      keeps showing the time.</td></tr>
    <tr><td><b>Sensitivity</b></td><td>How loud a plane must be to show (60 dBA is clearly noticeable over a quiet
      street); daily data budget; saving requests when planes are elsewhere.</td></tr>
    <tr><td><b>Wi-Fi and security</b></td><td>Network and signal, change the admin password, forget Wi-Fi.</td></tr>
  </table>

  <h3>Getting the sensitivity right</h3>
  <p>For the first few days, compare the <i>Recent</i> list with what you hear. If the sign lights before a plane is
  clearly audible, raise the threshold a few dBA; if it misses planes you notice, lower it.</p>
  <div class="grid2" style="margin-top:8pt">
    <div>{led("A350! AMSTERDAM", "#ffc800")}<p class="caption">A rare visitor: gold, with a sparkle.</p></div>
    <div>{led("", cols=inbound_cols(12), color="#ff7800")}
      <p class="caption">Inbound countdown: a plane is 12 seconds from audible.</p></div>
  </div>
</section>"""


def page4():
    return """
<section class="page">
  <div class="eyebrow">Good to know</div>
  <h2>Troubleshooting</h2>
  <table>
    <tr><th style="width:36%">If&hellip;</th><th>Try this</th></tr>
    <tr><td>The installer can't find the clock</td><td>Make sure the PC and clock are on the same Wi-Fi. Type the clock's
      address when the installer asks (hold the middle button to see it).</td></tr>
    <tr><td>Updating later</td><td>Run the installer again, or use <i>Update firmware</i> on the settings page.</td></tr>
    <tr><td>Your phone doesn't open the setup page</td><td>Stay on the clock's Wi-Fi and browse to
      <span class="mono">192.168.4.1</span>.</td></tr>
    <tr><td>You changed Wi-Fi or moved</td><td>Hold the left button while plugging the clock in. It forgets the old
      network and shows the setup Wi-Fi again.</td></tr>
    <tr><td><span class="mono">overhead.local</span> doesn't open</td><td>Hold the middle button; open the number it
      scrolls, like <span class="mono">192.168.1.42</span>.</td></tr>
    <tr><td>Forgot the admin password</td><td>Hold the middle button: it shows the password four characters at a
      time; left and right step through it.</td></tr>
    <tr><td>It shows flight numbers, not cities</td><td>The route for that flight isn't known yet. Common at airports
      without a supported flight board; add one for yours (see CONTRIBUTING on GitHub).</td></tr>
    <tr><td>Nothing ever shows</td><td>Check the settings page: address found? Airport nearby? Inside quiet hours?
      Planes may be using other runways today (see Airport weather).</td></tr>
    <tr><td>Wi-Fi or the internet is down</td><td>Nothing to do: it keeps working as a clock and picks up planes again
      when the connection returns.</td></tr>
  </table>

  <h2 style="margin-top:16pt">Privacy and security</h2>
  <div class="grid2">
    <div class="card"><b>Stays at home</b><br>No account, cloud or remote access, and no internet port opened. The
      setup Wi-Fi and the settings page are password protected. Updates need a USB cable at the clock.</div>
    <div class="card"><b>What leaves the house</b><br>Your address once (or nothing, if you enter coordinates), your
      location rounded to about 1 km when asking for nearby planes, and flight numbers when looking up routes. Every
      connection is verified HTTPS.</div>
  </div>
  <p class="note" style="margin-top:12pt">Overhead is an independent open-source project, MIT licensed. It isn't
  affiliated with Ulanzi, any airline or airport, and it's for fun, not navigation or safety. Data from adsb.lol (ODbL),
  OurAirports, &copy; OpenStreetMap contributors, Open-Meteo (CC BY 4.0), NOAA aviationweather.gov and the US Census
  Geocoder.</p>
  <p class="foot">github.com/MishaTikh/overhead-clock &middot; firmware 0.1.1</p>
</section>"""


def main():
    doc = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Overhead Quick Start</title><style>{CSS}</style></head>
<body>{page1()}{page2()}{page3()}{page4()}</body></html>"""
    open(OUT_HTML, "w", encoding="utf-8", newline="\n").write(doc)
    browsers = [
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        shutil.which("google-chrome") or "", shutil.which("chromium") or "", shutil.which("msedge") or "",
    ]
    exe = next((b for b in browsers if b and os.path.exists(b)), None)
    if not exe:
        sys.exit("Wrote the HTML; install Edge or Chrome to print the PDF.")
    profile = os.path.join(os.environ.get("TEMP", "/tmp"), "overhead-pdf-profile")  # separate from any open browser
    subprocess.run([exe, "--headless=new", "--disable-gpu", "--no-pdf-header-footer", f"--user-data-dir={profile}",
                    f"--print-to-pdf={OUT_PDF}", "file:///" + OUT_HTML.replace("\\", "/")], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    print("wrote", os.path.relpath(OUT_PDF, ROOT))


if __name__ == "__main__":
    main()
