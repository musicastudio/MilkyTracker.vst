# Renders the slider parts from slider.blend into the skin at every scale: faders/pot_cap (the grabber) and
# the tracks, faders/track_narrow, track_long_narrow, track_mid_narrow, track_short_narrow and track_parts (the
# Parts page's volume, as long as its fader travels), and faders/track_h, lying down for the Options page's
# Quality: the same track turned a quarter clockwise, under the scene's own lamps.
#   In Blender, with slider.blend open (blender/scripts on sys.path): import render_slider; render_slider.run()
import bmesh, math, os, sys
import bpy
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import kit

PX = kit.PX
CAP = 42           # the skin lays the sliders out around a 42 px grabber
CAP_PX = 18.5      # grabber radius in px, leaving room for its shadow
TRACK_W = 20
# The tracks are drawn 1:1 at their own size, so the sizes are the old art's, and so are the rows of the
# first, middle and last ticks (the heavier ones): a tick every 4 px on whole rows at 1x. Height, first tick
# row, ticks.
TRACKS = {"track_narrow": (173, 14, 37), "track_long_narrow": (188, 17, 39),
          "track_mid_narrow": (149, 14, 31), "track_short_narrow": (125, 14, 25), "track_parts": (170, 17, 35),
          "track_h": (240, 14, 54)}
LYING = {"track_h"}   # rendered turned, so its length runs across
TICK_X = (6, 14)        # tick span, px from the left edge
TICK_X_HEAVY = (4, 16)
WELL_R = 9.8            # half width of the slot, px (the WellMat shader's W_PX)
WELL_END = 0.2          # gap between the slot's round ends and the image ends, px
D = bpy.data


def render(sc, cam, w, h, s):
    rw, rh = kit.lround(w * s), kit.lround(h * s)
    sc.render.resolution_x, sc.render.resolution_y = rw, rh
    cam.data.ortho_scale = max(rw, rh) / s * PX
    bpy.ops.render.render(write_still=True)
    img = D.images.load(sc.render.filepath)
    a = np.array(img.pixels[:], dtype=np.float32).reshape(rh, rw, 4)[::-1].copy()
    D.images.remove(img)
    return a


def show(grabber):
    D.collections["GrabberSet"].hide_render = not grabber
    D.collections["TrackSet"].hide_render = grabber


def run(scales=kit.SCALES, only=None):
    sc = bpy.context.scene
    cam = sc.camera
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    sc.render.filepath = os.path.join(bpy.app.tempdir, "slider_part.png")
    show(True)
    for s in scales if not only or "pot_cap" in only else []:
        a = render(sc, cam, CAP, CAP, s)
        # the drop shadow runs past the frame: fade it to nothing at the edge so it never shows a square clip
        n = a.shape[0]
        yy, xx = (np.mgrid[0:n, 0:n] + 0.5) / s
        r = np.hypot(xx - CAP / 2, yy - CAP / 2)
        t = np.clip((CAP / 2 - r) / (CAP / 2 - CAP_PX - 0.5), 0, 1)
        a[..., 3] *= np.where(r > CAP_PX + 0.5, t * t * (3 - 2 * t), 1)
        kit.save(a, "faders/pot_cap", s)
    kit.meta("faders/pot_cap", surface=False)
    # tracks: the pot bezel's well drawn as a slot (WellMat), with flat ticks in the pot's tick colour
    show(False)
    well, ticks = D.objects["TrackWell"], D.objects["TrackTicks"]
    for name, (h, first, n) in TRACKS.items():
        if only and name not in only:
            continue
        half = h / 2 * PX
        bm = bmesh.new()   # the well covers the whole image; its shader draws the slot inside
        bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=1.0)
        bmesh.ops.scale(bm, vec=(TRACK_W / 2 * PX, half, 1), verts=bm.verts)
        bm.to_mesh(well.data)
        bm.free()
        D.materials["WellMat"].node_tree.nodes["HalfLen"].outputs[0].default_value = (h / 2 - WELL_END - WELL_R) * PX
        bm = bmesh.new()
        for i in range(n):
            heavy = i in (0, n // 2, n - 1)
            x0, x1 = TICK_X_HEAVY if heavy else TICK_X
            y = h / 2 - (first + 4 * i)                   # top edge of the tick's pixel row
            vs = [bm.verts.new(((x - TRACK_W / 2) * PX, yy * PX, 0)) for x, yy in ((x0, y), (x1, y), (x1, y - 1), (x0, y - 1))]
            f = bm.faces.new(vs)
            f.material_index = 1 if heavy else 0
        bm.to_mesh(ticks.data)
        bm.free()
        lying = name in LYING
        for o in (well, ticks):
            o.rotation_euler.z = -math.pi / 2 if lying else 0
        for s in scales:
            kit.save(render(sc, cam, h, TRACK_W, s) if lying else render(sc, cam, TRACK_W, h, s), "faders/" + name, s)
        kit.meta("faders/" + name, surface=False)
        for o in (well, ticks):
            o.rotation_euler.z = 0
    show(True)
    sc.render.resolution_x = sc.render.resolution_y = CAP
    cam.data.ortho_scale = CAP * PX


if __name__ == '__main__':
    run()
