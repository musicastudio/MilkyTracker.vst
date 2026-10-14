# Writes plugin/skin/fonts/FSVR-LCD.ttf, the LCD's 5 x 7 dot font, as a TrueType font of square dots, so the
# runtime draws the LCD's text at any scale. At a 16 px em a dot is 2 px and a cell 12 x 16, the old strip's.
#   python blender/scripts/lcd_font.py      (from the repo root; the standard library only)
#
# GLYPHS is the old lcd_small strip's dots: per character code, seven rows top down, five bits each, the high
# bit the left column. The em is 8 dot rows: the glyph's 7 above the baseline and half a dot of margin at the
# top and the bottom, as the strip had a blank pixel row at each end.
import struct
from pathlib import Path

GLYPHS = """
33:04040404040004 34:0a0a0a00000000 35:0a0a1f0a1f0a0a 36:040f140e051e04 37:18190204081303 38:0c12140815120d
39:0c040800000000 40:02040808080402 41:08040202020408 42:0004150e150400 43:0004041f040400 44:000000000c0408
45:0000001f000000 46:00000000000c0c 47:00010204081000 48:0e11131519110e 49:040c040404040e 50:0e11010204081f
51:1f02040201110e 52:02060a121f0202 53:1f101e0101110e 54:0608101e11110e 55:1f010204080808 56:0e11110e11110e
57:0e11110f01020c 58:000c0c000c0c00 59:000c0c000c0408 60:02040810080402 61:00001f001f0000 62:08040201020408
63:0e110102040004 64:0e11010d15150e 65:0e1111111f1111 66:1e11111e11111e 67:0e11101010110e 68:1c12111111121c
69:1f10101e10101f 70:1f10101c101010 71:0e11101711110f 72:1111111f111111 73:0e04040404040e 74:0702020202120c
75:11121418141211 76:1010101010101f 77:111b1511111111 78:11111915131111 79:0e11111111110e 80:1e11111e101010
81:0e11111115120d 82:1e11111e141211 83:0f10100e01011e 84:1f040404040404 85:1111111111110e 86:11111111110a04
87:1111111515150a 88:11110a040a1111 89:1111110a040404 90:1f01020408101f 91:0e08080808080e 92:00100804020100
93:0e02020202020e 94:040a1100000000 95:0000000000001f 96:08040200000000 97:00000e010f110f 98:1010161911111e
99:00000e1010110e 100:01010d1311110f 101:00000e111f100e 102:0609081c080808 103:000f11110f010e 104:10101619111111
105:04000c0404040e 106:0200060202120c 107:10101214181412 108:0c04040404040e 109:00001a15151111 110:00001619111111
111:00000e1111110e 112:00001e111e1010 113:00000d130f0101 114:00001619101010 115:00000e100e011e 116:08081c08080906
117:0000111111130d 118:00001111110a04 119:0000111115150a 120:0000110a040a11 121:000011110f010e 122:00001f0204081f
123:02040408040402 124:04040404040404 125:08040402040408 126:00000815020000 127:1f1f1f1f1f1f1f
"""
EM, DOT = 1000, 125
ADVANCE, ASCENT, DESCENT = 6 * DOT, 7 * DOT + DOT // 2, DOT // 2
OUT = Path(__file__).resolve().parents[2] / 'plugin' / 'skin' / 'fonts' / 'FSVR-LCD.ttf'

glyphs = {int(c): [int(r[i:i + 2], 16) for i in range(0, 14, 2)] for c, r in (g.split(':') for g in GLYPHS.split())}
codes = [32] + sorted(glyphs)   # glyph 0 is .notdef, then space, then the dotted ones in code order


def outline(rows):
    """One clockwise square contour per lit dot, y up from the baseline."""
    contours = []
    for d, bits in enumerate(rows):
        for k in range(5):
            if bits >> (4 - k) & 1:
                x0, y0 = k * DOT, (6 - d) * DOT
                contours.append([(x0, y0), (x0, y0 + DOT), (x0 + DOT, y0 + DOT), (x0 + DOT, y0)])
    return contours


def glyf_entry(contours):
    if not contours:
        return b''
    pts = [p for c in contours for p in c]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    out = struct.pack('>hhhhh', len(contours), min(xs), min(ys), max(xs), max(ys))
    end = -1
    for c in contours:
        end += len(c)
        out += struct.pack('>H', end)
    out += struct.pack('>H', 0) + bytes([1]) * len(pts)   # no instructions; every point on the curve
    px = py = 0
    for x, _ in pts:
        out += struct.pack('>h', x - px)
        px = x
    for _, y in pts:
        out += struct.pack('>h', y - py)
        py = y
    return out + b'\0' * (-len(out) % 4)


bodies = [glyf_entry([])] + [glyf_entry(outline(glyphs.get(c, [0] * 7))) for c in codes]
loca, glyf = [], b''
for b in bodies:
    loca.append(len(glyf))
    glyf += b
loca.append(len(glyf))
n = len(bodies)
all_pts = [p for c in codes for cont in outline(glyphs.get(c, [0] * 7)) for p in cont]
max_contours = max(len(outline(r)) for r in glyphs.values())

head = struct.pack('>IIIIHHqqhhhhHHhhh', 0x10000, 0x10000, 0, 0x5F0F3CF5, 3, EM, 0, 0,
                   0, 0, 5 * DOT, 7 * DOT, 0, 8, 2, 1, 0)
hhea = struct.pack('>IhhhHhhhhhhhhhhhH', 0x10000, ASCENT, -DESCENT, 0, ADVANCE, 0, 0, 5 * DOT, 1, 0, 0, 0, 0, 0, 0, 0, n)
maxp = struct.pack('>IHHHHHHHHHHHHHH', 0x10000, n, max(4 * max_contours, 4), max_contours, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)
xmins = [0] + [min((p[0] for cont in outline(glyphs.get(c, [0] * 7)) for p in cont), default=0) for c in codes]
hmtx = b''.join(struct.pack('>Hh', ADVANCE, x) for x in xmins)   # each glyph's left side bearing is its xMin
os2 = struct.pack('>HhHHHhhhhhhhhhhh10sIIII4sHHHhhhHHIIhhHHH',
                  4, ADVANCE, 400, 5, 0, 650, 700, 0, 140, 650, 700, 0, 480, 50, 250, 0, b'\0' * 10,
                  3, 0, 0, 0, b'NONE', 0x40, 32, max(codes), ASCENT, -DESCENT, 0, ASCENT, DESCENT, 1, 0,
                  5 * DOT, 7 * DOT, 0, 32, 1)
post = struct.pack('>IIhhIIIII', 0x30000, 0, -DESCENT, DOT // 2, 1, 0, 0, 0, 0)

# cmap: one format 4 segment per run of consecutive codes, each glyph id one more than the last
segs, start = [], codes[0]
for i in range(1, len(codes) + 1):
    if i == len(codes) or codes[i] != codes[i - 1] + 1:
        segs.append((start, codes[i - 1], (i - (codes[i - 1] - start + 1) + 1 - start) & 0xFFFF))
        if i < len(codes):
            start = codes[i]
segs.append((0xFFFF, 0xFFFF, 1))
sc = len(segs)
sr = 2 * 2 ** (sc.bit_length() - 1)
fmt4 = struct.pack('>HHHHHH', 4, 16 + 8 * sc, 0, 2 * sc, sr, (sr // 2).bit_length() - 1) + struct.pack('>H', 2 * sc - sr)
fmt4 += b''.join(struct.pack('>H', e) for _, e, _ in segs) + b'\0\0'
fmt4 += b''.join(struct.pack('>H', s) for s, _, _ in segs)
fmt4 += b''.join(struct.pack('>H', d) for _, _, d in segs)
fmt4 += b'\0\0' * sc
cmap = struct.pack('>HHHHI', 0, 1, 3, 1, 12) + fmt4

names = {1: 'FSVR LCD', 2: 'Regular', 3: 'FSVR LCD Regular', 4: 'FSVR LCD', 5: 'Version 1.0', 6: 'FSVR-LCD'}
strs = [v.encode('utf-16-be') for v in names.values()]
name = struct.pack('>HHH', 0, len(names), 6 + 12 * len(names))
off = 0
for k, s in zip(names, strs):
    name += struct.pack('>HHHHHH', 3, 1, 0x409, k, len(s), off)
    off += len(s)
name += b''.join(strs)

tables = {b'OS/2': os2, b'cmap': cmap, b'glyf': glyf, b'head': head, b'hhea': hhea, b'hmtx': hmtx,
          b'loca': b''.join(struct.pack('>I', o) for o in loca), b'maxp': maxp, b'name': name, b'post': post}


def checksum(b):
    b += b'\0' * (-len(b) % 4)
    return sum(struct.unpack('>%dI' % (len(b) // 4), b)) & 0xFFFFFFFF


def build(tables):
    nt = len(tables)
    sr = 16 * 2 ** (nt.bit_length() - 1)
    out = struct.pack('>IHHHH', 0x10000, nt, sr, (sr // 16).bit_length() - 1, 16 * nt - sr)
    offset, data = 12 + 16 * nt, b''
    for tag in sorted(tables):
        t = tables[tag]
        out += struct.pack('>4sIII', tag, checksum(t), offset + len(data), len(t))
        data += t + b'\0' * (-len(t) % 4)
    return out + data


font = build(tables)
adjust = (0xB1B0AFBA - checksum(font)) & 0xFFFFFFFF
tables[b'head'] = head[:8] + struct.pack('>I', adjust) + head[12:]
OUT.write_bytes(build(tables))
print('wrote', OUT, len(codes), 'glyphs')
