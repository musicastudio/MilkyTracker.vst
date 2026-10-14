# MilkyTracker's own bitmap fonts as Hollow glyph strips (hollow/docs/skin-format.md, Fonts), so the column reads
# like the tracker beside it: its default System (8x8) and Large (12x12) faces, read out of src/ppui/Font.cpp
# (PPFont's FontFamilyDescriptions name them by number: "J-FLUX" and "ATHENA"), one strip per ink. Each is the 256
# Latin-1 cells side by side, fixed width, under a blank row so the runtime never takes its top row for markers; the
# other scales are the 1x strip's pixels repeated (nearest), as the tracker's screen is.
#   python blender/scripts/mt_fonts.py       (from the repo root, needs Pillow)
from pathlib import Path
import math, re
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SKIN = ROOT / 'plugin' / 'skin'
SCALES = [1, 1.5, 2]   # kit.SCALES
SYSTEM, LARGE = 23, 1  # Font.cpp's "Font no." of FONT_SYSTEM's and FONT_LARGE's default faces
STRIPS = [   # name, face, ink
    ('mt_text', SYSTEM, (0xc4, 0xbc, 0xcf)),    # captions
    ('mt_value', SYSTEM, (0xf2, 0xef, 0xf7)),   # values, and text on a lit or coloured ground
    ('mt_dim', SYSTEM, (0x8f, 0x87, 0x9b)),     # hints
    ('mt_title', LARGE, (0xe4, 0xdc, 0xef)),    # the dialog's title
]


def lround(v):   # kit.lround: std::lround, as the runtime rounds a scaled size
    return int(math.floor(v + 0.5))


def faces():
    """Font.cpp's entries by number: (name, width, height, bits), the bits read as PPFont's Bitstream does."""
    src = (ROOT / 'src' / 'ppui' / 'Font.cpp').read_text(encoding='latin-1')
    out = {}
    for m in re.finditer(r'// Font no\. (\d+)\s*FontEntry\("([^"]+)", (\d+), (\d+),((?:\s*"(?:[^"\\]|\\.)*")+)\s*\)', src):
        data = bytearray()
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(5)):
            for esc, ch in re.findall(r'\\x([0-9a-fA-F]+)|(\\.|.)', lit):
                data.append(int(esc, 16) & 0xff if esc else ord(ch[-1]) if ch.startswith('\\') else ord(ch))
        out[int(m.group(1))] = (m.group(2), int(m.group(3)), int(m.group(4)), bytes(data))
    return out


def strip(face, ink):
    name, w, h, bits = face
    im = Image.new('RGBA', (256 * w, h + 2), (0, 0, 0, 0))
    px = im.load()
    for c in range(256):
        for y in range(h):
            for x in range(w):
                o = c * w * h + y * w + x
                if o >> 3 < len(bits) and (bits[o >> 3] >> (o & 7)) & 1:
                    px[c * w + x, y + 1] = ink + (255,)
    return im


if __name__ == '__main__':
    found = faces()
    for name, number, ink in STRIPS:
        base = strip(found[number], ink)
        for s in SCALES:
            out = SKIN / 'fonts' if s == 1 else SKIN / 'scales' / ('%g' % s) / 'fonts'
            out.mkdir(parents=True, exist_ok=True)
            img = base if s == 1 else base.resize((lround(base.width * s), lround(base.height * s)), Image.NEAREST)
            img.save(out / (name + '.png'))
        print(name, found[number][0])
