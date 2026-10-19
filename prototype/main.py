"""MSP Overhead: shows where the plane over your house came from, only while you can hear it.

    python main.py                       # live adsb.lol data, browser mock-up on :8080
    python main.py --feed sim            # simulated 12R/12L arrivals
    python main.py --feed sim --flow 30  # simulated departures off 30L/30R
    python main.py --analyze             # print the audibility numbers for your house
    python main.py --feed local --display max7219     # on the Pi with an RTL-SDR
"""
import argparse
import threading
import time

from overhead import config, displays, feeds, matrix
from overhead.analyze import report
from overhead.routes import RouteLookup
from overhead.tracker import Tracker


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config")
    ap.add_argument("--feed", choices=["adsblol", "local", "sim"])
    ap.add_argument("--display", help="web, console, max7219 (comma separated)")
    ap.add_argument("--flow", default="12", choices=["12", "30"], help="sim runway flow")
    ap.add_argument("--port", type=int)
    ap.add_argument("--analyze", action="store_true")
    args = ap.parse_args()

    cfg = config.load(args.config)
    if args.feed:
        cfg.feed = args.feed
    if cfg.feed == "sim" and cfg.pass_log_csv:
        cfg.pass_log_csv = cfg.pass_log_csv.replace(".csv", "_sim.csv")
    if args.display:
        cfg.display = args.display
    if args.port:
        cfg.web_port = args.port
    if args.analyze:
        print(report(cfg))
        return

    feed = feeds.make(cfg, sim_flow=args.flow)
    tracker = Tracker(cfg, RouteLookup(cfg))
    outs = displays.make(cfg)
    marquee = matrix.Marquee(cfg)
    lock = threading.Lock()
    status = {"feed": feed.name, "last_poll": 0.0, "next_poll": 0.0, "error": ""}

    def poller():
        next_t = 0.0
        while True:
            now = time.time()
            if now >= next_t:
                try:
                    ac = feed.fetch()
                    with lock:
                        tracker.ingest(ac, time.time())
                    status["error"] = ""
                except Exception as e:  # network hiccup: keep dead-reckoning
                    status["error"] = f"{type(e).__name__}: {e}"[:200]
                status["last_poll"] = now
                with lock:
                    want = tracker.want_poll_at(now)
                # poll at the idle rate, sooner if a plane is about to pass,
                # but never faster than the feed's minimum (or its remaining budget)
                next_t = max(now + feed.interval(True), min(now + feed.interval(False), want))
                status["next_poll"] = next_t
            time.sleep(0.2)

    threading.Thread(target=poller, daemon=True).start()

    period = 1.0 / cfg.fps
    last_state = 0.0
    while True:
        now = time.time()
        with lock:
            msg = tracker.tick(now)
            state = None
            if now - last_state > 0.5:
                last_state = now
                state = {
                    "planes": tracker.snapshot(now),
                    "current": None if not msg else {"text": msg.text, "cs": msg.callsign, "type": msg.type,
                                                     "db": round(msg.level, 1), "rgb": msg.rgb},
                    "passes": tracker.recent_passes,
                    "feed": dict(status, now=now, tokens=round(getattr(feed, "tokens", -1), 1),
                                 calls=getattr(feed, "calls", 0)),
                    "cfg": {"on_db": cfg.on_db, "off_db": cfg.off_db, "mode": cfg.mode},
                }
        rgb = msg.rgb if msg else (255, 120, 0)
        if msg:
            marquee.set_text(msg.text, now, cfg.font_mode)
            if cfg.brightness_follows_loudness:
                b = 0.25 + 0.75 * min(1.0, max(0.0, (msg.level - cfg.on_db) / 20.0))
            else:
                b = 1.0
            frame = marquee.frame(now)
        elif cfg.mode == "clock":
            marquee.set_text("", now)
            lt = time.localtime(now)
            frame = matrix.clock_frame(lt.tm_hour, lt.tm_min, int(now) % 2 == 0, cfg.clock_24h, cfg.width)
            b, rgb = 0.45, cfg.clock_rgb
        else:
            marquee.set_text("", now)
            frame, b = [0] * cfg.width, 0.0
        for o in outs:
            o.show(frame, b, state, rgb)
        time.sleep(max(0.0, period - (time.time() - now)))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
