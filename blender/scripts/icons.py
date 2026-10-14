# The skin's glyphs as strokes on a 24 x 24 grid, x right and y down: each a list of polylines (closed when the
# last point repeats the first) and filled dots, drawn by sprites.py as rounded tubes so they light and shade
# like everything else. Width is the stroke's in grid units. An icon with 'svg' is instead a Fontaudio glyph
# (blender/icons, CC BY 4.0, credited in NOTICE.md), its filled outline fitted to the square.
import math


def circle(cx, cy, r, n=24, a0=0, a1=360):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
            for i in range(n + 1)]


def keyboard(held=(), sounds=(), arrow=0):
    """Five white keys, a dot on each key held and the keys that sound filled; arrow 1 or -1 runs over the keys
    the way time does (the Last and First priorities)."""
    sounds = sounds if isinstance(sounds, (list, tuple)) else [sounds]
    k = {'width': 1.2, 'lines': [[(2, 8), (22, 8), (22, 20), (2, 20), (2, 8)]] + [[(2 + 4 * i, 8), (2 + 4 * i, 20)] for i in range(1, 5)],
         'fills': [[(2.9 + 4 * i, 12), (5.1 + 4 * i, 12), (5.1 + 4 * i, 19.1), (2.9 + 4 * i, 19.1)] for i in sounds],
         'dots': [(4 + 4 * i, 16.5, 1.1) for i in held if i not in sounds]}
    if arrow:
        tip, tail = (20, 4) if arrow > 0 else (4, 20)
        k['lines'] += [[(tail, 4), (tip, 4)], [(tip - 2.5 * arrow, 2), (tip, 4), (tip - 2.5 * arrow, 6)]]
    return k


ICONS = {
    # the top bar's strip buttons
    'save': {'width': 1.7, 'lines': [
        [(4, 3), (16, 3), (20, 7), (20, 21), (4, 21), (4, 3)],
        [(8, 3), (8, 9), (15, 9), (15, 3)],
        [(8, 21), (8, 14), (16, 14), (16, 21)]]},
    'folder': {'width': 1.7, 'lines': [
        [(3, 6), (9, 6), (11, 8), (21, 8), (21, 19), (3, 19), (3, 6)],
        [(3, 11), (21, 11)]]},
    'editor': {'width': 1.7, 'lines': [
        [(3, 4), (21, 4), (21, 20), (3, 20), (3, 4)],
        [(3, 8), (21, 8)],
        [(6, 12), (12, 12)], [(6, 16), (16, 16)]]},
    'keys': {'width': 1.6, 'lines': [
        [(3, 4), (21, 4), (21, 20), (3, 20), (3, 4)],
        [(9, 20), (9, 13)], [(15, 20), (15, 13)],
        [(7, 4), (7, 13), (11, 13), (11, 4)], [(13, 4), (13, 13), (17, 13), (17, 4)]]},
    # the bezel's two buttons: MIDI learn (a plug on its lead) and panic
    'learn': {'width': 1.7, 'lines': [
        [(5, 21), (5, 12)] + circle(9, 12, 4, 12, 180, 360)[1:] + [(13, 21)],
        [(16, 3), (16, 9)], [(20, 3), (20, 9)],
        [(14, 9), (22, 9), (22, 12), (18, 15), (14, 12), (14, 9)], [(18, 15), (18, 20)]]},
    'panic': {'width': 2.6, 'lines': [[(12, 4), (12, 14)]], 'dots': [(12, 19.5, 1.7)]},
    'up': {'width': 0, 'fill': [(12, 7), (19, 16), (5, 16)]},
    'down': {'width': 0, 'fill': [(5, 8), (19, 8), (12, 17)]},
    'chevron': {'width': 2.4, 'lines': [[(6, 9), (12, 15), (18, 9)]]},   # a header's menu
    # the expert pages
    'page_all_ops': {'svg': 'fad-cpu.svg'},
    'page_all_envs': {'svg': 'fad-ADSR.svg'},
    'page_mod': {'svg': 'fad-modsine.svg'},
    'page_keysc': {'svg': 'fad-keyboard.svg'},
    'page_filter': {'svg': 'fad-filter-lowpass.svg'},
    'page_pitch': {'svg': 'fad-arpup.svg'},
    'lock': {'width': 1.6, 'lines': [
        circle(12, 10, 4, 12, 180, 360) + [(16, 13)], [(8, 13), (8, 10)]],
        'fill': [(5, 13), (19, 13), (19, 22), (5, 22)]},
    'check': {'width': 2.2, 'lines': [[(5, 12.5), (10, 17.5), (19, 6)]]},
    'arrow': {'width': 0, 'fill': [(8, 5), (17, 12), (8, 19)]},
    'note': {'width': 1.8, 'lines': [[(13, 18), (13, 4), (18, 7)]], 'dots': [(10, 18, 3)]},
    'edit': {'width': 0, 'fill': [(6, 4), (16, 4), (16, 20), (6, 20)]},
    # the Parts page's Poly and Mono, and the mono priorities: which held key sounds
    'kbd_poly': keyboard(sounds=[0, 2, 4]),
    'kbd_mono': keyboard(sounds=2),
    'prio_last': keyboard((0, 2, 4), 2, 1),
    'prio_first': keyboard((0, 2, 4), 2, -1),
    'prio_top': keyboard((0, 2, 4), 4),
    'prio_bottom': keyboard((0, 2, 4), 0),
}
