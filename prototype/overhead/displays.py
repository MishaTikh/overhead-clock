"""Output backends. Each gets show(frame, brightness) ~30x/sec.
frame = list of column bytes (bit 0 = top row); brightness 0..1 (0 = off)."""
import json
import os
import threading
import time
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

from .matrix import to_rows

WEB_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "web")


class ConsoleDisplay:
    def __init__(self, cfg):
        self.last = None

    def show(self, frame, brightness, state=None, rgb=(255, 120, 0)):
        key = (tuple(frame), brightness > 0)
        if key != self.last:
            self.last = key
            print("\x1b[H\x1b[2J" + "\n".join(to_rows(frame)).replace("#", "█").replace(".", " "), flush=True)


class Max7219Display:
    """4-in-1 MAX7219 8x32 module on SPI0 (DIN=GPIO10/MOSI, CLK=GPIO11, CS=GPIO8/CE0).
    pip install luma.led_matrix ; enable SPI in raspi-config."""

    def __init__(self, cfg):
        from luma.core.interface.serial import spi, noop
        from luma.led_matrix.device import max7219
        self.device = max7219(spi(port=0, device=0, gpio=noop()), cascaded=cfg.width // 8,
                              block_orientation=-90, rotate=0)
        self.last = None

    def show(self, frame, brightness, state=None, rgb=(255, 120, 0)):
        from luma.core.render import canvas
        key = (tuple(frame), round(brightness * 15))
        if key == self.last:
            return
        self.last = key
        if brightness <= 0:
            self.device.hide()
            return
        self.device.show()
        self.device.contrast(max(1, int(brightness * 255)))
        with canvas(self.device) as draw:
            for x, col in enumerate(frame):
                for y in range(8):
                    if (col >> y) & 1:
                        draw.point((x, y), fill="white")


class WebDisplay:
    """Browser mock-up of the sign + live radar. http://localhost:<port>/"""

    def __init__(self, cfg):
        self.cfg = cfg
        self.frame_msg = b""
        self.state_msg = b"{}"
        self.cond = threading.Condition()
        outer = self

        class H(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                if self.path in ("/", "/index.html"):
                    with open(os.path.join(WEB_DIR, "index.html"), "rb") as f:
                        body = f.read()
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html; charset=utf-8")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                elif self.path.startswith("/mode"):
                    m = self.path.partition("m=")[2]
                    if m in ("clock", "plane"):
                        outer.cfg.mode = m
                    self.send_response(204)
                    self.end_headers()
                elif self.path == "/stream":
                    self.send_response(200)
                    self.send_header("Content-Type", "text/event-stream")
                    self.send_header("Cache-Control", "no-cache")
                    self.end_headers()
                    last_state = 0.0
                    try:
                        while True:
                            with outer.cond:
                                outer.cond.wait(timeout=1.0)
                                fm, sm = outer.frame_msg, outer.state_msg
                            self.wfile.write(b"event: frame\ndata: " + fm + b"\n\n")
                            if time.time() - last_state > 0.5:
                                last_state = time.time()
                                self.wfile.write(b"event: state\ndata: " + sm + b"\n\n")
                            self.wfile.flush()
                    except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                        return
                else:
                    self.send_error(404)

        self.httpd = ThreadingHTTPServer(("0.0.0.0", cfg.web_port), H)
        self.httpd.daemon_threads = True
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        print(f"Mock-up: http://localhost:{cfg.web_port}/", flush=True)

    def show(self, frame, brightness, state=None, rgb=(255, 120, 0)):
        with self.cond:
            self.frame_msg = json.dumps({"f": bytes(frame).hex(), "b": round(brightness, 3),
                                         "c": "#%02x%02x%02x" % tuple(rgb)}).encode()
            if state is not None:
                self.state_msg = json.dumps(state).encode()
            self.cond.notify_all()


def make(cfg):
    kinds = {"web": WebDisplay, "console": ConsoleDisplay, "max7219": Max7219Display}
    return [kinds[k.strip()](cfg) for k in cfg.display.split(",") if k.strip()]
