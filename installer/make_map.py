# Draws map.png and icon.png (also written to the repo's root): the box the installer spins, which musica.studio
# shows too, and the product's icon. The box is a PC big box of 19 x 24 x 5 units at 40 px per unit, unfolded as a
# cross (the Installer's ui.go, sides):
#
#         [ top  ]
#   [L ][ front ][R ][ back ]
#         [bottom]
#
# The icon is MilkyTracker's milk carton (resources/pictures/carton.png) with a plug in its corner (plug.png, from
# https://pixabay.com/vectors/plug-connector-power-connection-27187/ under the Pixabay Content License, its segmented cord
# redrawn as one curve). The front
# carries the carton, the title and the README's screenshot (docs/milkytracker_vst.png), the back a description,
# crops of that screenshot and the patterns dialog (patterns.png), both spines the title. Needs Pillow and the
# licensed "Analog Whispers.ttf", looked for in the Installer checkout beside this repo.
#   python installer/make_map.py [path to Analog Whispers.ttf]
import os
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageOps

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
FONT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, '..', 'Installer', 'Analog Whispers.ttf')
U = 40
BW, BH, BD = 19 * U, 24 * U, 5 * U
BOX, INK, DARK, LIGHT = (0xb9, 0xaa, 0xd6), (0x3a, 0x2a, 0x66), (0x1c, 0x14, 0x30), (0xf0, 0xeb, 0xf8)
TITLE = 'MilkyTracker.vst'
TAG = 'The tracker, as a plug-in'
SHOT = Image.open(os.path.join(ROOT, 'docs', 'milkytracker_vst.png')).convert('RGB')


def font(px):
    return ImageFont.truetype(FONT, px)


def outlined(im, px, rgb):
    a = im.getchannel('A').filter(ImageFilter.MaxFilter(px * 2 + 1))
    o = Image.new('RGBA', im.size, rgb + (255,))
    o.putalpha(a)
    o.alpha_composite(im)
    return o


def plug(size):
    """plug.png in the box's violet, its shading kept, with a white rule round it."""
    p = Image.open(os.path.join(HERE, 'plug.png')).convert('RGBA')
    t = ImageOps.colorize(ImageOps.grayscale(p.convert('RGB')), black=INK, white=(0xe6, 0xdf, 0xf2)).convert('RGBA')
    t.putalpha(p.getchannel('A'))
    t.thumbnail((size, size), Image.LANCZOS)
    m = max(2, size // 30)
    padded = Image.new('RGBA', (t.width + 2 * m, t.height + 2 * m), (0, 0, 0, 0))
    padded.alpha_composite(t, (m, m))
    return outlined(padded, m * 5 // 8, (255, 255, 255))


def logo(px):
    """The carton with the plug in its lower right corner, px square, transparent."""
    im = Image.new('RGBA', (px, px), (0, 0, 0, 0))
    c = round(px * 352 / 484)
    im.alpha_composite(Image.open(os.path.join(ROOT, 'resources', 'pictures', 'carton.png')).convert('RGBA')
                       .resize((c, c), Image.NEAREST), (round(px * 30 / 484), round(px * 30 / 484)))
    p = plug(round(px * 250 / 484))
    im.alpha_composite(p, (px - p.width, px - p.height))
    return im


def icon():
    im = Image.new('RGBA', (512, 512), INK + (255,))
    ImageDraw.Draw(im).rectangle((10, 10, 501, 501), fill=BOX + (255,))
    im.alpha_composite(logo(484), (14, 14))
    im = im.convert('RGB')
    im.save(os.path.join(HERE, 'icon.png'), optimize=True)
    im.save(os.path.join(ROOT, 'icon.png'), optimize=True)


def title(width, rgb):
    """The title in Analog Whispers, cut to its ink and scaled to width."""
    f = font(120)
    im = Image.new('RGBA', (int(f.getlength(TITLE)) + 40, 200), (0, 0, 0, 0))
    ImageDraw.Draw(im).text((20, 20), TITLE, font=f, fill=rgb + (255,))
    im = im.crop(im.getchannel('A').getbbox())
    return im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)


def scaled(im, width):
    return im.convert('RGB').resize((width, round(im.height * width / im.width)), Image.LANCZOS)


def framed(dst, im, x, y):
    """A screenshot with a thin ink rule and a soft shadow below it."""
    sh = Image.new('RGBA', (im.width + 40, im.height + 40), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rectangle((20, 26, 20 + im.width, 26 + im.height), fill=(0, 0, 0, 90))
    dst.alpha_composite(sh.filter(ImageFilter.GaussianBlur(9)), (x - 20, y - 20))
    ImageDraw.Draw(dst).rectangle((x - 3, y - 3, x + im.width + 2, y + im.height + 2), fill=INK + (255,))
    dst.paste(im, (x, y))


def centre(d, text, f, cx, y, fill):
    d.text((cx - f.getlength(text) / 2, y), text, font=f, fill=fill)


def wrap(text, f, width):
    lines, line = [], ''
    for w in text.split():
        if line and f.getlength(line + ' ' + w) > width:
            lines.append(line)
            line = w
        else:
            line = (line + ' ' + w).strip()
    return lines + [line]


def publisher(d, f, x, y, a, b):
    d.text((x, y), 'musica.', font=f, fill=a)
    d.text((x + f.getlength('musica.'), y), 'studio', font=f, fill=b)


def spine(w, h):
    """A spine's art upright (width = the box's height), turned into place by the caller."""
    im = Image.new('RGBA', (h, w), BOX + (255,))
    lg = logo(w - 40).rotate(90)  # upright once the spine is turned into place
    im.alpha_composite(lg, (50, 20))
    t = title(round(h * 0.42), INK)
    im.alpha_composite(t, (80 + lg.width, (w - t.height) // 2))
    f = font(46)
    publisher(ImageDraw.Draw(im), f, h - 70 - f.getlength('musica.studio'), (w - 46) // 2 - 4, INK, DARK)
    return im


def main():
    icon()
    m = Image.new('RGBA', (2 * BD + 2 * BW, 2 * BD + BH), BOX + (255,))
    d = ImageDraw.Draw(m)

    # front
    fx, fy = BD, BD
    lg = logo(150)
    t = title(BW - 120 - lg.width, INK)
    m.alpha_composite(lg, (fx + 40, fy + 30))
    m.alpha_composite(t, (fx + 70 + lg.width, fy + 60))
    d.text((fx + 74 + lg.width, fy + 80 + t.height), TAG, font=font(36), fill=DARK)
    main_shot = scaled(SHOT, BW - 80)
    framed(m, main_shot, fx + 40, fy + 210)
    y = fy + 210 + main_shot.height + 40
    formats = ['vst3', 'clap', 'au', 'vst2', 'standalone']
    names = ['VST3', 'CLAP', 'AU', 'VST2', 'standalone']
    gap = (BW - 80) // len(formats)
    for i, (k, n) in enumerate(zip(formats, names)):
        icn = Image.open(os.path.join(ROOT, '..', 'Installer', 'icons', k + '.png')).convert('RGBA').resize((64, 64), Image.LANCZOS)
        tint = Image.new('RGBA', icn.size, INK + (255,))
        tint.putalpha(icn.getchannel('A'))
        cx = fx + 40 + gap * i + gap // 2
        m.alpha_composite(tint, (cx - 32, y + 8))
        centre(d, n, font(22), cx, y + 78, INK)
    d.rectangle((fx, fy + BH - 90, fx + BW - 1, fy + BH - 1), fill=INK)
    f = font(44)
    publisher(d, f, fx + BW // 2 - f.getlength('musica.studio') / 2, fy + BH - 68, (255, 255, 255), LIGHT)

    # back
    bx, by = 2 * BD + BW, BD
    t = title(420, INK)
    m.alpha_composite(t, (bx + 40, by + 44))
    f = font(25)
    text = ('MilkyTracker.vst runs the whole of MilkyTracker, as it is, inside a DAW. It follows the host\'s tempo '
            'and transport, or plays patterns from the keyboard: green keys move the pitch, blue keys start '
            'the patterns you list for them.')
    y = by + 70 + t.height
    for line in wrap(text, f, BW - 80):
        d.text((bx + 40, y), line, font=f, fill=DARK)
        y += 34
    y += 22
    tw = (BW - 80 - 24) // 2
    a = scaled(SHOT.crop((10, 10, 1040, 740)), tw)
    b = scaled(Image.open(os.path.join(HERE, 'patterns.png')), tw)
    framed(m, a, bx + 40, y)
    framed(m, b, bx + 40 + tw + 24, y)
    y += max(a.height, b.height) + 24
    k = scaled(SHOT.crop((10, 750, 1305, 945)), BW - 80)
    framed(m, k, bx + 40, y)
    y += k.height + 30
    f = font(24)
    for line in ('Free, DAW Sync and MIDI Trigger modes',
                 'The tracker at 1x or 2x, the window at 100 to 200%',
                 'The song saved inside the host\'s project',
                 'Every module MilkyTracker opens'):
        d.rectangle((bx + 44, y + 9, bx + 52, y + 17), fill=INK)
        d.text((bx + 66, y), line, font=f, fill=DARK)
        y += 34
    d.rectangle((bx, by + BH - 90, bx + BW - 1, by + BH - 1), fill=INK)
    f = font(24)
    d.text((bx + 40, by + BH - 72), 'Windows, macOS and Linux', font=f, fill=(255, 255, 255))
    d.text((bx + 40, by + BH - 42), 'Free software, GPLv3', font=f, fill=LIGHT)
    f = font(36)
    publisher(d, f, bx + BW - 40 - f.getlength('musica.studio'), by + BH - 64, (255, 255, 255), LIGHT)

    # spines: reading top to bottom on both, as on a shelf
    s = spine(BD, BH).rotate(-90, expand=True)
    m.alpha_composite(s, (0, BD))
    m.alpha_composite(s, (BD + BW, BD))

    # top, upright seen from the front
    t = title(360, INK)
    m.alpha_composite(t, (BD + (BW - t.width) // 2, (BD - t.height) // 2 - 14))
    centre(d, TAG, font(26), BD + BW // 2, (BD + t.height) // 2 + 4, DARK)

    m.convert('RGB').save(os.path.join(HERE, 'map.png'), optimize=True)
    print('map.png', m.size)


if __name__ == '__main__':
    main()
