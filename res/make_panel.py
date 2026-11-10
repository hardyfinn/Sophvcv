#!/usr/bin/env python3
# Draws res/Sophie.svg. VCV Rack can't draw SVG <text>, so every label is
# turned into outline paths with fontTools. Positions are in mm and must match
# src/Sophie.cpp.
#   pip install fonttools
#   python3 res/make_panel.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 243.84, 128.5  # 48 HP

BG = "#1A1320"
BG_DARK = "#140E19"
INK = "#F4EEF7"
SOFT = "#A99BB6"
PINK = "#FF4FA3"
BLUE = "#7FC8FF"
CHROME = "#D9D4E0"
OUTBOX = "#0B080E"
CELL = "#231A2B"

# keep in sync with Sophie.cpp
PAD_X = [16.0, 36.0, 56.0, 76.0]
PAD_ROW_Y = [46.0, 66.0, 86.0, 106.0]
PAD_BTN_DY, PAD_JACK_DY = -2.6, 5.6
KX = [99.0, 116.0, 133.0, 150.0, 167.0]
KY = [27.0, 51.0, 75.0, 99.0]
NOTE_X = [101.0, 118.0, 135.0]
NOTE_Y = 115.0
CV_JX = [188.0, 214.0]
CV_TX = [197.5, 223.5]
CV_Y = [27.0, 41.0, 55.0, 69.0, 83.0]
OUT_X = [192.0, 208.0, 224.0]
OUT_Y = 109.0

PAD_NAMES = ["KICK", "RIM", "SNARE", "CLAP", "SNARE 2", "LOW TOM", "CLOSED HH", "FLOOR TOM",
             "PEDAL HH", "MID TOM", "OPEN HH", "LO-MID TOM", "HI-MID TOM", "CRASH", "HIGH TOM", "RIDE"]

fonts = {}


def text(s, x, y, size, font=FONT_MED, fill=INK, anchor="middle", spacing=0.04):
    """Return an SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    if anchor == "middle":
        x0 = x - width * scale / 2
    elif anchor == "end":
        x0 = x - width * scale
    else:
        x0 = x
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        tp = TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y))
        gs[n].draw(tp)
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


parts = []
add = parts.append


def rect(x, y, w, h, fill, rx=1.2, extra=""):
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" {extra}/>')


def line(x1, y1, x2, y2, stroke, w=0.3, extra=""):
    add(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{stroke}" stroke-width="{w}" {extra}/>')


def header(s, x, y, w):
    """Section title with a thin pink rule after it."""
    add(text(s, x, y, 2.9, FONT_BOLD, PINK, anchor="start", spacing=0.10))
    tw = len(s) * 2.2 + 1.5
    line(x + tw, y - 0.8, x + w, y - 0.8, PINK, 0.25, 'opacity="0.55"')


# background with a soft glossy band
add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
for i, op in enumerate([0.10, 0.07, 0.05, 0.03]):
    add(f'<rect x="0" y="{6 + i * 2.5}" width="{W}" height="2.5" fill="{CHROME}" opacity="{op}"/>')
add(f'<rect x="0" y="{H - 5}" width="{W}" height="5" fill="{BG_DARK}"/>')

# title
add(text("SOPHIE", 5.0, 10.2, 4.6, FONT_BOLD, INK, anchor="start", spacing=0.16))
add(text("metallic FM percussion", 31.0, 10.0, 2.8, FONT_MED, SOFT, anchor="start"))
add(text("a port of schwung-sophie by mestela (MIT)", W / 2, H - 1.4, 2.2, FONT_MED, SOFT))

# display window
rect(4.4, 11.9, 80.2, 22.2, "#06040A", 1.6)

# ---- pads ----
for r in range(4):
    for c in range(4):
        pad = (3 - r) * 4 + c
        x, y = PAD_X[c], PAD_ROW_Y[r]
        rect(x - 9.5, y - 10.0, 19.0, 19.6, CELL, 1.4)
        add(text(f"{pad + 1}", x - 8.6, y - 6.8, 2.9, FONT_BOLD, PINK, anchor="start"))
        add(text(PAD_NAMES[pad], x + 8.7, y - 6.8, 2.0, FONT_MED, SOFT, anchor="end", spacing=0.0))
add(text("press a pad = select + play  ·  jack = trigger", 46.0, 118.9, 2.2, FONT_MED, SOFT))
add(text("pad 1 is bottom left, like a Drum Rack", 46.0, 121.9, 2.2, FONT_MED, SOFT))

# divider between pads and knobs
line(88.0, 13.0, 88.0, 122.0, PINK, 0.25, 'opacity="0.35"')
line(180.0, 13.0, 180.0, 122.0, PINK, 0.25, 'opacity="0.35"')

# ---- knobs for the selected pad ----
rows = [
    ("VOICE", ["MODEL", "TUNE", "SWEEP", "DECAY", "LEVEL"]),
    ("TIMBRE", ["COLOR", "METAL", "FEEDBK", "M.DECAY", "FB"]),
    ("DIRT + FILTER", ["CRUSH", "DRIVE", "CUTOFF", "RESO", "TYPE"]),
    ("RING", ["TIME", "FDBK", "TONE", "MIX", "TAIL"]),
]
for (title, names), y in zip(rows, KY):
    header(title, 90.5, y - 11.0, 87.0)
    for x, n in zip(KX, names):
        add(text(n, x, y - 6.4, 3.2, FONT_BOLD, INK, spacing=0.03))
# small hints
add(text("all knobs edit the selected pad", 177.5, 13.0, 2.2, FONT_MED, SOFT, anchor="end"))
# switch legends
for y, (top, bot) in ((KY[1], ("smooth", "raw")), (KY[3], ("live", "hit"))):
    add(text(top, KX[4] + 3.4, y - 2.0, 2.2, FONT_MED, SOFT, anchor="start"))
    add(text(bot, KX[4] + 3.4, y + 3.8, 2.2, FONT_MED, SOFT, anchor="start"))
# model and filter type positions
add(text("fuse stack split shard pipe boom", KX[0] - 7.0, KY[0] + 7.8, 2.0, FONT_MED, SOFT, anchor="start"))
add(text("LP HP NOTCH DJ BP", KX[4], KY[2] + 7.8, 2.0, FONT_MED, SOFT))
add(text("0 = auto", KX[3], KY[1] + 7.8, 2.2, FONT_MED, SOFT))

# notes in
header("NOTES IN", 90.5, NOTE_Y - 7.0, 87.0)
for x, n in zip(NOTE_X, ["V/OCT", "GATE", "VEL"]):
    add(text(n, x, NOTE_Y + 7.7, 2.9, FONT_BOLD, INK))
add(text("poly", 158.0, NOTE_Y - 1.6, 2.4, FONT_BOLD, SOFT))
add(text("C2–D#3", 158.0, NOTE_Y + 1.4, 2.2, FONT_MED, SOFT))
add(text("= pads 1–16", 158.0, NOTE_Y + 4.4, 2.2, FONT_MED, SOFT))

# ---- CV ----
header("CV IN", 183.0, 13.6, 56.0)
add(text("mono = all pads · poly = pad N", 206.0, 17.8, 2.2, FONT_MED, SOFT))
cv_names = ["TUNE", "DECAY", "MODEL", "COLOR", "METAL", "FEEDBK", "SWEEP", "CRUSH", "DRIVE", "CUTOFF"]
for i, n in enumerate(cv_names):
    col, row = i % 2, i // 2
    cx = (CV_JX[col] + CV_TX[col]) / 2
    add(text(n, cx, CV_Y[row] - 5.6, 2.9, FONT_BOLD, INK))

# ---- outputs ----
rect(183.0, 96.0, 50.0, 25.0, OUTBOX, 1.6)
for x, n in zip(OUT_X, ["VOL", "MIX", "PADS"]):
    add(text(n, x, OUT_Y - 6.6, 2.9, FONT_BOLD, PINK if n != "VOL" else INK))
add(text("PADS = 16-ch poly, one per pad", 208.0, OUT_Y + 9.0, 2.0, FONT_MED, SOFT))

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" '
       f'viewBox="0 0 {W} {H}">\n' + "\n".join(parts) + "\n</svg>\n")
with open(os.path.join(HERE, "Sophie.svg"), "w") as f:
    f.write(svg)
print("wrote", os.path.join(HERE, "Sophie.svg"))
