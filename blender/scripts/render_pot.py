# Renders the pot filmstrips from pot.blend into the skin at every scale: knobs/rotary_large and rotary_small,
# and rotary_small_orange (the operator panel's, its cap a sunset orange so it reads off the grey panel, its pointer
# groove painted dark so the position reads at a glance), 128 frames each, frame 0 pointing at 7:30 and frame 127 at 4:30, as the skin's knob kind expects.
#   In Blender, with pot.blend open (blender/scripts on sys.path): import render_pot; render_pot.run()
#   or: blender -b blender/pot.blend -P blender/scripts/render_pot.py -- [rotary_large ...]
# The groove is shaped for the pot's size at 1x and the frame is only rendered larger or smaller, so every
# scale shows the same pot. pot.blend keeps FSVR's cyan-tinted graphite, pointer, bezel and sky; the render
# turns them violet (kit.violet_nodes) and leaves the orange cap as it is.
import bmesh, math, os, sys
import bpy
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import kit

FRAMES = 128
# px square at 1x (ortho_scale 2.0 fills the frame either way), then the groove's inner and outer end as a
# fraction of the half-width. The small pot draws a shorter pointer than a straight scale of the large one,
# and the same ~2.4 px width at both sizes; the groove keeps both.
SIZES = {"rotary_large": (56, 0.09, 0.52), "rotary_small": (44, 0.14, 0.50), "rotary_small_orange": (44, 0.14, 0.50)}
CAPS = {"rotary_small_orange": (0.93, 0.105, 0.022)}   # a cap colour other than the graphite, linear RGB (#f75b29)
GROOVE_INK = {"rotary_small_orange": (0.012, 0.012, 0.014)}   # the groove painted, linear RGB; else it shows the cap
GROOVE_PX = 2.6     # width across the top of the cap
GROOVE_DEPTH = 0.12 # scene units
CAP_TOP = 0.30


def shape_groove(cap, cut, px, r0, r1):
    # a capsule lying along the pointer, sunk so its chord at the cap top is GROOVE_PX wide; built in the cap's
    # local units since it is parented to the cap
    hw = GROOVE_PX / px                        # half width in scene units (half-width = px/2)
    rho = (hw ** 2 + GROOVE_DEPTH ** 2) / (2 * GROOVE_DEPTH)
    a, b = r0 + hw, r1 - hw
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=48, v_segments=24, radius=rho)
    s = cap.scale.x
    for v in bm.verts:
        v.co.y += b if v.co.y > 0 else a
        v.co.x /= s
        v.co.y /= s
        v.co.z += CAP_TOP + rho - GROOVE_DEPTH
    bm.to_mesh(cut.data)
    bm.free()


def run(scales=kit.SCALES, samples=96, only=None):
    sc = bpy.context.scene
    for idb in list(bpy.data.materials) + list(bpy.data.worlds):
        kit.violet_nodes(idb)
    cap = bpy.data.objects["Cap"]
    bsdf = next(n for n in bpy.data.materials["CapMat"].node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    graphite = tuple(bsdf.inputs['Base Color'].default_value)
    cut = bpy.data.objects["PointerGroove"]
    try:
        prefs = bpy.context.preferences.addons['cycles'].preferences
        prefs.compute_device_type = 'OPTIX'
        prefs.get_devices()
        for d in prefs.devices:
            d.use = d.type != 'CPU'
        sc.cycles.device = 'GPU'
    except Exception:
        pass
    sc.cycles.samples = samples
    tmp = os.path.join(bpy.app.tempdir, "pot_frame.png")
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    sc.render.filepath = tmp
    for name, (px, r0, r1) in SIZES.items():
        if only and name not in only:
            continue
        bsdf.inputs['Base Color'].default_value = (*CAPS[name], 1) if name in CAPS else graphite
        shape_groove(cap, cut, px, r0, r1)
        groove = cap.modifiers['Groove']   # its cut faces take the cutter's material (the ink) or the cap's
        cut.data.materials.clear()
        if name in GROOVE_INK:
            cut.data.materials.append(kit.plain('GrooveInk', GROOVE_INK[name], roughness=0.55))
        groove.material_mode = 'TRANSFER' if name in GROOVE_INK else 'INDEX'
        for s in scales:
            n = kit.lround(px * s)
            sc.render.resolution_x = sc.render.resolution_y = n
            strip = []
            for k in range(FRAMES):
                cap.rotation_euler.z = math.radians(135 - 270 * k / (FRAMES - 1))
                bpy.ops.render.render(write_still=True)
                img = bpy.data.images.load(tmp)
                strip.append(np.array(img.pixels[:], dtype=np.float32).reshape(n, n, 4)[::-1].copy())
                bpy.data.images.remove(img)
            kit.save(np.concatenate(strip), "knobs/" + name, s)
        kit.meta("knobs/" + name, tiles=FRAMES, surface=False)
    bsdf.inputs['Base Color'].default_value = graphite
    cap.modifiers['Groove'].material_mode = 'INDEX'
    cut.data.materials.clear()
    cap.rotation_euler.z = 0
    sc.render.resolution_x = sc.render.resolution_y = 56


if __name__ == '__main__':
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    run(only=args or None)
