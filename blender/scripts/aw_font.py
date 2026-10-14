# The title's and the wordmark's Analog Whispers, the musica.studio installer's typeface, as Hollow glyph strips (hollow/docs/skin-format.md,
# Fonts) at every scale the skin ships: fonts/aw_<ink>_<px>.png at 1x and scales/<s>/fonts/ at the others, each
# rasterized at its own size, so the title is as sharp at 2x as at 1x. The font files stay beside the Installer
# (../../Installer, not in this repository): the free edition, with the few marks it lacks (BORROWED) taken from
# the licensed one; every other code in the strips is blank.
#   python blender/scripts/aw_font.py       (from the repo root, needs Pillow)
from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SKIN = ROOT / 'plugin' / 'skin'
TTF = ROOT.parent / 'Installer' / 'Analog Whispers FREE.ttf'
LICENSED = ROOT.parent / 'Installer' / 'Analog Whispers.ttf'
BORROWED = "'+;:/()"
BLACK, GREY = (0, 0, 0), (0x78, 0x74, 0x78)   # the installer's colours (Installer/ui.go)
SCALES = [1, 1.5, 2]   # kit.SCALES; kit itself needs Blender


def lround(v):   # kit.lround: std::lround, as the runtime rounds a scaled size
    return int(math.floor(v + 0.5))


LIGHT, PALE = (0xe6, 0xe0, 0xee), (0xa4, 0x9c, 0xb0)   # the installer's black and grey, turned for the dark chrome
STRIPS = [('light', LIGHT, 24),   # "MilkyTracker.vst" at the top of the right column
          ('light', LIGHT, 16), ('pale', PALE, 16)]   # the musica.studio wordmark: "musica." and "studio"


def glyph_strip(px, ink):
    free, lic = ImageFont.truetype(str(TTF), px), ImageFont.truetype(str(LICENSED), px)
    asc, desc = free.getmetrics()
    h = asc + desc
    cells = []
    for c in range(256):
        ch = chr(c)
        f = lic if ch in BORROWED else free
        printable = 32 <= c < 127 or c >= 160
        w = max(1, round(f.getlength(ch))) if printable else 1
        im = Image.new('L', (w, h), 0)
        if printable and c != 32:
            ImageDraw.Draw(im).text((0, asc - f.getmetrics()[0]), ch, font=f, fill=255)   # on the free edition's baseline
        cells.append(im)
    strip = Image.new('RGBA', (sum(i.width for i in cells), h + 1), (0, 0, 0, 0))
    x = 0
    for im in cells:
        strip.putpixel((x, 0), (255, 255, 255, 255))   # the marker row: a glyph starts here
        strip.paste(Image.new('RGBA', im.size, ink + (255,)), (x, 1), im)
        x += im.width
    return strip


if __name__ == '__main__':
    for name, ink, px in STRIPS:
        for s in SCALES:
            out = SKIN / 'fonts' if s == 1 else SKIN / 'scales' / ('%g' % s) / 'fonts'
            out.mkdir(parents=True, exist_ok=True)
            glyph_strip(lround(px * s), ink).save(out / ('aw_%s_%d.png' % (name, px)))
        print('aw_%s_%d' % (name, px))
