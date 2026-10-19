"""Text -> 8x32 LED frames. A frame is a list of `width` column bytes, bit 0 = top row
(the same layout a MAX7219 chain wants, so the Pi backend just copies it out)."""

# Classic 5x7 font (Adafruit GFX glcdfont), ASCII 32..90 plus a few extras. Column-major, LSB at top.
_F = """
20 0000000000|21 00005F0000|22 0007000700|23 147F147F14|24 242A7F2A12|25 2313086462
26 3649562050|27 0000070000|28 001C224100|29 0041221C00|2A 2A1C7F1C2A|2B 08083E0808
2C 0080603000|2D 0808080808|2E 0000606000|2F 2010080402|30 3E5149453E|31 00427F4000
32 7249494946|33 2141494D33|34 1814127F10|35 2745454539|36 3C4A494931|37 4121110907
38 3649494936|39 464949291E|3A 0000140000|3B 0040340000|3C 0008142241|3D 1414141414
3E 0041221408|3F 0201590906|40 3E415D594E|41 7C1211127C|42 7F49494936|43 3E41414122
44 7F4141413E|45 7F49494941|46 7F09090901|47 3E41415173|48 7F0808087F|49 00417F4100
4A 2040413F01|4B 7F08142241|4C 7F40404040|4D 7F021C027F|4E 7F0408107F|4F 3E4141413E
50 7F09090906|51 3E4151215E|52 7F09192946|53 2649494932|54 03017F0103|55 3F4040403F
56 1F2040201F|57 3F4038403F|58 6314081463|59 0304780403|5A 61594D4543|5F 4040404040
"""
FONT = {}
for tok in _F.replace("\n", "|").split("|"):
    tok = tok.strip()
    if tok:
        code, cols = tok.split()
        FONT[chr(int(code, 16))] = [int(cols[i:i + 2], 16) for i in range(0, 10, 2)]

# A tiny airplane icon (7 wide), used optionally before the city.
PLANE = [0x08, 0x08, 0x1C, 0x7F, 0x1C, 0x08, 0x1C]

ACCENTS = str.maketrans("ÁÀÂÄÃÅÉÈÊËÍÌÎÏÓÒÔÖÕÚÙÛÜÇÑ", "AAAAAAEEEEIIIIOOOOOUUUUCN")


# 3x5 font, same as the TC001 firmware: five row digits per glyph, bit 2 = left column; drawn on rows 1-5.
_SMALL_ROWS = {
    "!": "22202", '"': "55000", "#": "57575", "&": "25253", "'": "22000", "(": "12221", ")": "42224",
    "+": "02720", ",": "00024", "-": "00700", ".": "00002", "/": "11244", ":": "02020", ">": "42124", "?": "61202",
    "0": "75557", "1": "26227", "2": "61247", "3": "61216", "4": "55711", "5": "74616", "6": "34757", "7": "71222",
    "8": "75757", "9": "75716", "A": "25755", "B": "65656", "C": "34443", "D": "65556", "E": "74647", "F": "74644",
    "G": "34553", "H": "55755", "I": "72227", "J": "11152", "K": "55655", "L": "44447", "M": "57755", "N": "65555",
    "O": "25552", "P": "65644", "Q": "25573", "R": "65655", "S": "34216", "T": "72222", "U": "55557", "V": "55552",
    "W": "55775", "X": "55255", "Y": "55222", "Z": "71247",
}
SMALL = {k: [sum(1 << (y + 1) for y in range(5) if int(v[y]) & (4 >> x)) for x in range(3)]
         for k, v in _SMALL_ROWS.items()}


def text_columns(text, small=False):
    """Proportional rendering: trims blank glyph columns, 1 px gap, 3 px spaces (2 in the small font)."""
    font = SMALL if small else FONT
    cols = []
    for ch in text.upper().translate(ACCENTS):
        if ch == " ":
            cols += [0, 0] if small else [0, 0, 0]
            continue
        g = font.get(ch, font["?"])
        lo = next((i for i, c in enumerate(g) if c), 0)
        hi = max((i for i, c in enumerate(g) if c), default=len(g) - 1)
        cols += g[lo:hi + 1] + [0]
    return cols[:-1] if cols and cols[-1] == 0 else cols


class Marquee:
    """Static and centered if it fits; otherwise show the start, pause, then scroll."""
    GAP = 14
    HOLD_S = 1.2

    def __init__(self, cfg):
        self.cfg = cfg
        self.text = ""
        self.cols = []
        self.t0 = 0.0

    def set_text(self, text, now, font_mode=0):
        """font_mode: 0 auto (big if it fits, else small if it fits, else scroll big), 1 big, 2 small."""
        if text != self.text:
            self.text, self.t0 = text, now
            self.cols = text_columns(text, small=font_mode == 2)
            if font_mode == 0 and len(self.cols) > self.cfg.width:
                small = text_columns(text, small=True)
                if len(small) <= self.cfg.width:
                    self.cols = small

    def frame(self, now):
        w = self.cfg.width
        if not self.cols:
            return [0] * w
        n = len(self.cols)
        if n <= w:
            pad = (w - n) // 2
            return [0] * pad + self.cols + [0] * (w - n - pad)
        cycle = n + self.GAP
        t = max(0.0, now - self.t0 - self.HOLD_S)
        off = int(t * self.cfg.scroll_px_per_s) % cycle
        strip = self.cols + [0] * self.GAP
        return [strip[(off + i) % cycle] for i in range(w)]


def clock_frame(hour24, minute, colon_on=True, h24=False, width=32):
    """Classic clock face in the big font, centered: "9:45" or "21:45"."""
    h = hour24 if h24 else (hour24 + 11) % 12 + 1
    cols = text_columns(("%02d" if h24 else "%d") % h) + [0, 0x14 if colon_on else 0, 0] + text_columns("%02d" % minute)
    pad = (width - len(cols)) // 2
    return ([0] * pad + cols + [0] * width)[:width]


def to_rows(frame, height=8):
    return ["".join("#" if (c >> r) & 1 else "." for c in frame) for r in range(height)]
