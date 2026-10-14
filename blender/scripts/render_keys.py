# Renders the keyboard from keys.blend into the skin at every scale: keyboard/key_{c,d,e,f,g,a,b,c_top,a_low,
# black}, each two tiles side by side, at rest and pressed.
#   In Blender, with keys.blend open (blender/scripts on sys.path): import render_keys; render_keys.run()
#   or: blender -b blender/keys.blend -P blender/scripts/render_keys.py
# keys.blend keeps FSVR's cyan-tinted black keys, rail, pressed white key and sky; the render turns them violet
# (kit.violet_nodes) and leaves the ivory white keys as they are.
#
# The piano kind (hollow/src/core/kinds.cpp) places the keys itself: a 130 px octave, white keys at 0 19 38 56
# 75 94 112 and black keys at 11 33 67 87 107, each times the skin's density and rounded, and the density is
# 1.5 times the scale. keys.blend models each key on its slot at 1x (density 1.5). So the octave is rendered
# once at 4x and every key is cut out at its slot there and box-filtered down to its slot at each scale,
# which is exactly as wide as the kind will place it. key_a_low is the A with no G sharp to its left, for an
# 88-key board's A0, so no black key's shadow falls on it.
import math, os, sys
import bpy
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import kit

DENSITY = 1.5
WHITE_AT = [0, 19, 38, 56, 75, 94, 112, 130]
BLACK_AT = [11, 33, 67, 87, 107]
WHITE_H, BLACK_W, BLACK_H = 161, 20, 99
PRESS_DEG = 3.0
PRESS_DROP = 12.0   # px a pressed key sinks, so its neighbours shade both its sides
MASTER = 4          # the scale the octave is rendered at before it is cut up
NAMES = ["C", "D", "E", "F", "G", "A", "B"]
whites = NAMES + ["prevB", "nextC"]
blacks = ["Cs", "Ds", "Fs", "Gs", "As", "prevAs"]
bed = ["Keybed"]
D = bpy.data


def dp(v, d=DENSITY):   # std::lround, as Skin::dp
    return int(math.floor(v * d + 0.5))


def box(a, x0, x1, tw, y0=0, y1=None, th=None):
    """Columns x0..x1 and rows y0..y1 of a (float edges, master pixels) box-filtered to tw x th pixels, in
    premultiplied alpha so a transparent edge does not darken."""
    y1 = a.shape[0] if y1 is None else y1
    th = th or a.shape[0]
    p = a.copy()
    p[..., :3] *= p[..., 3:4]

    def axis(img, lo, hi, n, ax):
        edges = np.linspace(lo, hi, n + 1)
        c = np.cumsum(np.concatenate([np.zeros_like(np.take(img, [0], ax)), img], ax), ax)
        def at(e):   # the cumulative sum at a fractional edge, linearly between whole pixels
            i = np.clip(np.floor(e).astype(int), 0, img.shape[ax] - 1)
            f = (e - i).reshape([-1 if k == ax else 1 for k in range(img.ndim)])
            return np.take(c, i, ax) + f * np.take(img, i, ax)
        return (at(edges[1:]) - at(edges[:-1])) / ((hi - lo) / n)
    out = axis(axis(p, x0, x1, tw, 1), y0, y1, th, 0)
    out[..., :3] /= np.maximum(out[..., 3:4], 1e-6)
    return np.clip(out, 0, 1)


def setup():
    sc = bpy.context.scene
    for idb in list(D.materials) + list(D.worlds):
        kit.violet_nodes(idb)
    try:
        prefs = bpy.context.preferences.addons['cycles'].preferences
        prefs.compute_device_type = 'OPTIX'
        prefs.get_devices()
        for d in prefs.devices:
            d.use = d.type != 'CPU'
        sc.cycles.device = 'GPU'
    except Exception:
        pass
    sc.cycles.samples = 128
    sc.render.resolution_x, sc.render.resolution_y = 224 * MASTER, WHITE_H * MASTER
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    sc.render.filepath = os.path.join(bpy.app.tempdir, 'keys_pass.png')
    return sc


def shot(white_cam, pressed, hidden=()):
    for n in whites + bed:
        D.objects[n].visible_camera = white_cam
    for n in blacks:
        D.objects[n].visible_camera = not white_cam
        D.objects[n].hide_render = n in hidden
    for n in whites + blacks:
        o = D.objects[n]
        o.rotation_euler.x = math.radians(PRESS_DEG) if n in pressed else 0
        o.location.z = o["z0"] - (PRESS_DROP * kit.PX if n in pressed else 0)
        base = "WhiteKey" if n in whites else "BlackKey"
        o.data.materials[0] = D.materials[base + ("Down" if n in pressed else "")]
    bpy.ops.render.render(write_still=True)
    img = D.images.load(bpy.context.scene.render.filepath)
    w, h = img.size
    a = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)[::-1].copy()
    D.images.remove(img)
    return a


def restore():
    for n in whites + blacks + bed:
        D.objects[n].visible_camera = True
        D.objects[n].rotation_euler.x = 0
    for n in blacks:
        D.objects[n].hide_render = False
    for n in whites + blacks:
        D.objects[n].location.z = D.objects[n]["z0"]
        D.objects[n].data.materials[0] = D.materials["WhiteKey" if n in whites else "BlackKey"]


def run(scales=kit.SCALES):
    sc = setup()
    M = MASTER
    rest = shot(True, set())
    down = shot(True, {"C", "E", "G", "B", "nextC"})
    down2 = shot(True, {"D", "F", "A"})
    alone = shot(True, set(), hidden={"Gs"})
    alone_down = shot(True, {"D", "F", "A"}, hidden={"Gs"})
    brest = shot(False, set())
    bdown = shot(False, set(blacks))
    restore()
    wx = [dp(a) for a in WHITE_AT]   # the slots keys.blend is modelled on, 1x pixels
    for s in scales:
        d = DENSITY * s
        tw = [dp(WHITE_AT[i + 1], d) - dp(WHITE_AT[i], d) for i in range(7)]
        th = max(1, kit.lround(WHITE_H * s))
        def cut(img, i, width=None):
            return box(img, wx[i] * M, wx[i + 1] * M, width or tw[i], 0, WHITE_H * M, th)
        for i, n in enumerate(NAMES):
            p = down if n in ("C", "E", "G", "B") else down2
            kit.save(np.concatenate([cut(rest, i), cut(p, i)], axis=1), 'keyboard/key_' + n.lower(), s)
        ia = NAMES.index("A")
        kit.save(np.concatenate([cut(alone, ia), cut(alone_down, ia)], axis=1), 'keyboard/key_a_low', s)
        # the last key of the keyboard: a C with no black key to its right (the next octave's C here)
        top = [box(img, wx[7] * M, (wx[7] + wx[1]) * M, tw[0], 0, WHITE_H * M, th) for img in (rest, down)]
        kit.save(np.concatenate(top, axis=1), 'keyboard/key_c_top', s)
        bx = dp(BLACK_AT[0])
        bw, bh = max(1, kit.lround(BLACK_W * s)), max(1, kit.lround(BLACK_H * s))
        black = [box(img, bx * M, (bx + BLACK_W) * M, bw, 0, BLACK_H * M, bh) for img in (brest, bdown)]
        kit.save(np.concatenate(black, axis=1), 'keyboard/key_black', s)
    for n in ('c', 'd', 'e', 'f', 'g', 'a', 'b', 'c_top', 'a_low', 'black'):
        kit.meta('keyboard/key_' + n, tiles=2, axis='x', surface=False)


if __name__ == '__main__':
    run()
