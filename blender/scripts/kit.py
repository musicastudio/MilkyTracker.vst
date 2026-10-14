# The kit the component scripts share: the studio (camera, the Key and Rim lights and world the first
# components were lit with, Cycles), the materials, a few shapes, and rendering a rect of the window into the
# skin at every scale it ships at (plugin/skin/images at 1x, plugin/skin/scales/<s>/ at the others).
#
# Colours: FSVR's art was a cyan-tinted aqua; MilkyTracker's is the same art in lavender violet. violet() turns
# a colour's cyan or teal hue 80 degrees (violet_textures.py holds the rule and makes the violet textures), and
# plain(), brushed(), chrome() and the studio's sky all go through it, as do the colours a component's .blend
# was saved with (violet_nodes()).
#
# Units: a skin pixel at 1x is PX scene units (1/21, as pot, keys, nav, slider and wheel were built). Shapes are
# placed in window pixels, x right and y down, so a rect from a view's JSON goes in as it reads.
#
# In Blender, from a component's .blend:  exec(open(bpy.path.abspath("//scripts/kit.py")).read())
# or from a script beside it:              import kit  (blender/scripts on sys.path)
import bpy, bmesh, math, os, sys
import numpy as np
from mathutils import Vector

PX = 1 / 21
SCALES = [1, 1.5, 2]   # skin.json "scales"
HERE = os.path.dirname(os.path.abspath(__file__)) if '__file__' in globals() else bpy.path.abspath('//scripts')
if HERE not in sys.path:   # pngpack and violet_textures, beside it
    sys.path.insert(0, HERE)
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
SKIN = os.path.join(ROOT, 'plugin', 'skin')
TEXTURES = os.path.join(ROOT, 'blender', 'textures')
WINDOW = (1315, 949)   # the main view at 1x (layout.W, layout.H); the mottled chrome is mapped across it
SHADE = 0.32         # this skin's chrome and floors, darker than FSVR's, beside MilkyTracker's own dark screen
KEY_SUN, RIM_SUN = 2.85, 0.45   # W/m2, set so a flat chrome face shows its env texture at about its own brightness


def lround(v):
    """std::lround, as the runtime rounds a scaled size: halves away from zero (Python's round takes them to even)."""
    return int(math.floor(v + 0.5)) if v >= 0 else -int(math.floor(-v + 0.5))


def W(x, y, z=0.0):
    """A window point in pixels to scene units."""
    return Vector((x * PX, -y * PX, z * PX))


# ---- the studio -----------------------------------------------------------------------------------------

def studio(samples=64):
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    prefs = bpy.context.preferences.addons['cycles'].preferences
    try:
        prefs.compute_device_type = 'OPTIX'
        prefs.get_devices()
        for d in prefs.devices:
            d.use = d.type != 'CPU'
        sc.cycles.device = 'GPU'
    except Exception:
        sc.cycles.device = 'CPU'
    sc.cycles.samples = samples
    sc.cycles.use_denoising = False
    sc.cycles.filter_width = 1.0
    sc.cycles.max_bounces = 8
    sc.render.film_transparent = True
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    sc.render.image_settings.color_depth = '8'
    sc.view_settings.view_transform = 'Standard'
    sc.view_settings.look = 'None'
    sc.view_settings.exposure = 0
    sc.view_settings.gamma = 1
    cam = bpy.data.objects.get('Camera')
    if cam is None:
        cam = bpy.data.objects.new('Camera', bpy.data.cameras.new('Camera'))
        sc.collection.objects.link(cam)
    cam.data.type = 'ORTHO'
    cam.data.clip_start, cam.data.clip_end = 0.01, 100
    cam.rotation_euler = (0, 0, 0)
    sc.camera = cam
    # Sun lamps, not the first components' area lamps: an area lamp a few units over a frame lights a big panel
    # unevenly, and differently for every frame. These fall from the same directions (the Key from above the
    # window's top edge, the Rim low from its lower right) with the same strength over any frame.
    for name, strength, angle, rot in (('Key', KEY_SUN, 20, (-0.4363, 0, 0)), ('Rim', RIM_SUN, 30, (1.0821, 0, 0.5236))):
        o = bpy.data.objects.get(name)
        if o is not None and o.data.type != 'SUN':
            bpy.data.objects.remove(o, do_unlink=True)
            o = None
        if o is None:
            o = bpy.data.objects.new(name, bpy.data.lights.new(name, 'SUN'))
            sc.collection.objects.link(o)
        o.data.energy = strength
        o.data.angle = math.radians(angle)
        o.rotation_euler = rot
        o.visible_glossy = False   # reflections see the sky (below), never a lamp
    w = sc.world or bpy.data.worlds.new('World')
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputWorld')
    bg = nt.nodes.new('ShaderNodeBackground')
    nt.links.new(bg.outputs[0], out.inputs[0])
    # the sky: FSVR's (0.8, 0.9, 0.95) turned violet, at 0.35 as before, brighter towards the window's top
    # (scene +y) so that what tilts up catches light and what tilts down falls dark; reflections see it, the
    # camera never does
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Generated'], sep.inputs[0])
    k = nt.nodes.new('ShaderNodeMapRange')
    k.inputs['From Min'].default_value, k.inputs['From Max'].default_value = -1, 1
    k.inputs['To Min'].default_value, k.inputs['To Max'].default_value = 0.12, 0.9
    nt.links.new(sep.outputs['Y'], k.inputs['Value'])
    bg.inputs[0].default_value = (*violet((0.8, 0.9, 0.95)), 1)
    nt.links.new(k.outputs['Result'], bg.inputs[1])
    return sc


def frame(x, y, w, h, scale):
    """Points the camera at window rect (x, y, w, h) for a render at scale; returns the pixel size. The rect is
    widened to whole pixels at the scale, so every output pixel is 1/scale of a 1x pixel."""
    sc = bpy.context.scene
    rw, rh = max(1, lround(w * scale)), max(1, lround(h * scale))
    fw, fh = rw / scale, rh / scale
    cx, cy = x + fw / 2, y + fh / 2
    cam = sc.camera
    cam.location = W(cx, cy, 10 / PX)
    cam.data.ortho_scale = max(fw, fh) * PX
    sc.render.resolution_x, sc.render.resolution_y = rw, rh
    return rw, rh


def render(x, y, w, h, scale):
    """The window rect rendered at scale, as float RGBA rows top first. Its edges snap to whole pixels as the
    runtime rounds a widget's rect (each edge by lround), so a rect starting between pixels at a scale (x 6 at
    0.75x) lines up with the background instead of drawing half a pixel off."""
    X0, Y0 = lround(x * scale), lround(y * scale)
    x, y, w, h = X0 / scale, Y0 / scale, (lround((x + w) * scale) - X0) / scale, (lround((y + h) * scale) - Y0) / scale
    want = (max(1, lround(w * scale)), max(1, lround(h * scale)))
    if min(want) < 8:   # Blender renders nothing under a few pixels: a bigger frame from the same corner, cropped
        a = render(x, y, max(w, 8 / scale), max(h, 8 / scale), scale)
        return a[:want[1], :want[0]].copy()
    rw, rh = frame(x, y, w, h, scale)
    sc = bpy.context.scene
    tmp = os.path.join(bpy.app.tempdir or os.environ.get('TEMP', '.'), 'kit_render.png')
    sc.render.filepath = tmp
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(tmp, check_existing=False)
    a = np.array(img.pixels[:], dtype=np.float32).reshape(rh, rw, 4)[::-1].copy()
    bpy.data.images.remove(img)
    return a


def out_path(name, scale):
    if scale == 1:
        return os.path.join(SKIN, 'images', name + '.png')
    return os.path.join(SKIN, 'scales', ('%g' % scale), name + '.png')


def save(a, name, scale):
    """Writes rows-top-first RGBA (floats 0..1) as the skin image name at scale (pngpack: RGB when opaque)."""
    from pngpack import write_png
    path = out_path(name, scale)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    write_png(path, np.clip(np.floor(a * 255 + 0.5), 0, 255).astype(np.uint8))
    return path


def meta(name, **fields):
    """Sets the image's entry in skin.json "images" (tiles, axis, slice, surface ...): a field of None removes it,
    and an entry left empty goes. The file keeps its form: json.dumps(indent=1), CRLF."""
    import json
    path = os.path.join(SKIN, 'skin.json')
    sk = json.loads(open(path, encoding='utf8').read())
    e = dict(sk['images'].get(name, {}))
    for k, v in fields.items():
        if v is None:
            e.pop(k, None)
        else:
            e[k] = v
    if e:
        sk['images'][name] = e
    else:
        sk['images'].pop(name, None)
    sk['images'] = dict(sorted(sk['images'].items()))
    open(path, 'w', encoding='utf8', newline='\r\n').write(json.dumps(sk, indent=1) + '\n')


def tiles(name, rect, states, axis='y', scales=SCALES, **fields):
    """Renders rect once per state (a callable that sets the scene up) and stacks the renders as the image's
    tiles, at every scale, and records the tiles (and any other fields, such as slice) in skin.json. Returns
    the paths written."""
    n = len(states)
    meta(name, tiles=n if n > 1 else None, axis='x' if axis == 'x' and n > 1 else None, surface=False, **fields)
    paths = []
    for s in scales:
        parts = []
        for st in states:
            if st:
                st()
            parts.append(render(*rect, s))
        paths.append(save(np.concatenate(parts, axis=0 if axis == 'y' else 1), name, s))
    return paths


# ---- materials ------------------------------------------------------------------------------------------

def _to_srgb(v):
    return v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055


def _to_linear(v):
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def violet(rgb):
    """A linear RGB colour (3 values, or 4 with alpha) with a cyan or teal sRGB hue turned violet, keeping its
    HLS lightness and saturation, by violet_textures.rotate (the textures' rule). Any other colour, such as the
    LCD's green, the orange caps, a grey or black, comes back as it was."""
    from violet_textures import rotate
    s = [_to_srgb(max(float(v), 0.0)) for v in rgb[:3]]
    t = rotate(s)
    if np.allclose(t, s):
        return tuple(rgb)
    return tuple(_to_linear(float(v)) for v in t) + tuple(rgb[3:])


def violet_nodes(idb):
    """Turns the colours a component's .blend saved in a material or world violet: every colour input left
    unlinked, every RGB node and every colour ramp stop. Once only: the ID is marked, as plain() and brushed()
    mark theirs, so a second render leaves it alone."""
    if idb.get('violet') or not getattr(idb, 'node_tree', None):
        return
    for n in idb.node_tree.nodes:
        for i in n.inputs:
            if i.type == 'RGBA' and not i.is_linked:
                i.default_value = violet(tuple(i.default_value))
        if n.type == 'RGB':
            n.outputs[0].default_value = violet(tuple(n.outputs[0].default_value))
        if n.type == 'VALTORGB':
            for e in n.color_ramp.elements:
                e.color = violet(tuple(e.color))
    idb['violet'] = True


def _image(path, colour=True):
    img = bpy.data.images.get(os.path.basename(path))
    if img is None:
        img = bpy.data.images.load(path)
    img.colorspace_settings.name = 'sRGB' if colour else 'Non-Color'
    return img


def _principled(m):
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    b = nt.nodes.new('ShaderNodeBsdfPrincipled')
    nt.links.new(b.outputs[0], out.inputs[0])
    return nt, b


def plain(name, colour, metallic=0.0, roughness=0.5, alpha=1.0, emission=None, strength=1.0):
    """A flat material; its colour and emission (linear RGB) turned violet if they are cyan or teal."""
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    nt, b = _principled(m)
    b.inputs['Base Color'].default_value = (*violet(colour), 1)
    b.inputs['Metallic'].default_value = metallic
    b.inputs['Roughness'].default_value = roughness
    b.inputs['Alpha'].default_value = alpha
    if emission:
        b.inputs['Emission Color'].default_value = (*violet(emission), 1)
        b.inputs['Emission Strength'].default_value = strength
    m['violet'] = True
    return m


def chrome(name='Chrome', tint=(1, 1, 1), bend=6.0, grain=1.0):
    """The mottled violet chrome: textures/violet_env.png laid across the window by window position (bent by
    the surface's slope, `bend` pixels at full slope, the way the old runtime bent its reflection), times the
    hammered grain of textures/aqua_grain.png (grey, so shared with FSVR's aqua), which also dents the
    surface."""
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    nt, b = _principled(m)
    L = nt.links
    geo = nt.nodes.new('ShaderNodeNewGeometry')
    # window position in pixels: (x / PX, -y / PX), plus the normal's tilt
    pos = nt.nodes.new('ShaderNodeVectorMath'); pos.operation = 'SCALE'; pos.inputs[3].default_value = 1 / PX
    L.new(geo.outputs['Position'], pos.inputs[0])
    tilt = nt.nodes.new('ShaderNodeVectorMath'); tilt.operation = 'SCALE'; tilt.inputs[3].default_value = bend
    L.new(geo.outputs['Normal'], tilt.inputs[0])
    at = nt.nodes.new('ShaderNodeVectorMath'); at.operation = 'ADD'
    L.new(pos.outputs[0], at.inputs[0]); L.new(tilt.outputs[0], at.inputs[1])
    # uv = (x / WIDTH, 1 + y' / HEIGHT) where y' is the (negative) scene y in pixels
    uv = nt.nodes.new('ShaderNodeMapping'); uv.vector_type = 'POINT'
    uv.inputs['Scale'].default_value = (1 / WINDOW[0], 1 / WINDOW[1], 1)
    uv.inputs['Location'].default_value = (0, 1, 0)
    L.new(at.outputs[0], uv.inputs['Vector'])
    env = nt.nodes.new('ShaderNodeTexImage'); env.image = _image(os.path.join(TEXTURES, 'violet_env.png'))
    env.extension = 'EXTEND'; env.interpolation = 'Cubic'
    L.new(uv.outputs[0], env.inputs[0])
    # grain: one 256 px tile per 256 window pixels
    guv = nt.nodes.new('ShaderNodeMapping'); guv.inputs['Scale'].default_value = (1 / 256, 1 / 256, 1)
    L.new(pos.outputs[0], guv.inputs['Vector'])
    gr = nt.nodes.new('ShaderNodeTexImage'); gr.image = _image(os.path.join(TEXTURES, 'aqua_grain.png'), False)
    gr.extension = 'REPEAT'; gr.interpolation = 'Cubic'
    L.new(guv.outputs[0], gr.inputs[0])
    # colour: env * tint * (1 + 0.043 z), z = (g * 255 - 128) / 40
    z = nt.nodes.new('ShaderNodeMath'); z.operation = 'MULTIPLY_ADD'
    z.inputs[1].default_value = 255 / 40 * 0.0429 * grain; z.inputs[2].default_value = 1 - 128 / 40 * 0.0429 * grain
    L.new(gr.outputs['Color'], z.inputs[0])
    mul = nt.nodes.new('ShaderNodeMix'); mul.data_type = 'RGBA'; mul.blend_type = 'MULTIPLY'
    mul.inputs['Factor'].default_value = 1
    L.new(env.outputs['Color'], mul.inputs['A'])
    tn = nt.nodes.new('ShaderNodeCombineColor'); L.new(z.outputs[0], tn.inputs[0]); L.new(z.outputs[0], tn.inputs[1]); L.new(z.outputs[0], tn.inputs[2])
    L.new(tn.outputs[0], mul.inputs['B'])
    tint_n = nt.nodes.new('ShaderNodeMix'); tint_n.data_type = 'RGBA'; tint_n.blend_type = 'MULTIPLY'
    tint_n.inputs['Factor'].default_value = 1; tint_n.inputs['B'].default_value = (*(t * SHADE for t in tint), 1)
    L.new(mul.outputs['Result'], tint_n.inputs['A'])
    L.new(tint_n.outputs['Result'], b.inputs['Base Color'])
    bump = nt.nodes.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = 0.25 * grain
    bump.inputs['Distance'].default_value = 0.02 * PX
    L.new(gr.outputs['Color'], bump.inputs['Height'])
    L.new(bump.outputs['Normal'], b.inputs['Normal'])
    b.inputs['Metallic'].default_value = 0.25
    b.inputs['Roughness'].default_value = 0.45
    m['violet'] = True
    return m


aqua = chrome   # FSVR's name for it, for scripts that still call it so


def brushed(name='Brushed45', colour=(0.36, 0.42, 0.43), streak=0.10, angle=45):
    """Brushed steel, brushed at `angle` degrees from the window's x axis: fine streaks of a noise stretched
    along the stroke, an anisotropic sheen along it. Its colour (linear RGB) turned violet if cyan or teal."""
    colour = violet(colour)
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m['violet'] = True
    nt, b = _principled(m)
    L = nt.links
    geo = nt.nodes.new('ShaderNodeNewGeometry')
    pos = nt.nodes.new('ShaderNodeVectorMath'); pos.operation = 'SCALE'; pos.inputs[3].default_value = 1 / PX
    L.new(geo.outputs['Position'], pos.inputs[0])
    rot = nt.nodes.new('ShaderNodeMapping')   # x along the stroke (scene y is up, so +angle runs up-right)
    rot.inputs['Rotation'].default_value = (0, 0, math.radians(-angle))
    L.new(pos.outputs[0], rot.inputs['Vector'])
    st = nt.nodes.new('ShaderNodeMapping')
    st.inputs['Scale'].default_value = (0.004, 0.7, 1)   # long along the stroke, fine across it
    L.new(rot.outputs[0], st.inputs['Vector'])
    no = nt.nodes.new('ShaderNodeTexNoise'); no.inputs['Scale'].default_value = 1.0
    no.inputs['Detail'].default_value = 8; no.inputs['Roughness'].default_value = 0.75
    L.new(st.outputs[0], no.inputs['Vector'])
    s = nt.nodes.new('ShaderNodeMath'); s.operation = 'MULTIPLY_ADD'
    s.inputs[1].default_value = 2 * streak; s.inputs[2].default_value = 1 - streak
    L.new(no.outputs['Fac'], s.inputs[0])
    mul = nt.nodes.new('ShaderNodeMix'); mul.data_type = 'RGBA'; mul.blend_type = 'MULTIPLY'
    mul.inputs['Factor'].default_value = 1; mul.inputs['A'].default_value = (*(v * SHADE for v in colour), 1)
    cc = nt.nodes.new('ShaderNodeCombineColor')
    for i in range(3):
        L.new(s.outputs[0], cc.inputs[i])
    L.new(cc.outputs[0], mul.inputs['B'])
    L.new(mul.outputs['Result'], b.inputs['Base Color'])
    bump = nt.nodes.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = 0.15
    bump.inputs['Distance'].default_value = 0.01 * PX
    L.new(no.outputs['Fac'], bump.inputs['Height'])
    L.new(bump.outputs['Normal'], b.inputs['Normal'])
    b.inputs['Metallic'].default_value = 0.85
    b.inputs['Roughness'].default_value = 0.38
    b.inputs['Anisotropic'].default_value = 0.7
    tg = nt.nodes.new('ShaderNodeCombineXYZ')   # one tangent everywhere, so no face's own tangent shows a seam
    tg.inputs[0].default_value, tg.inputs[1].default_value = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    L.new(tg.outputs[0], b.inputs['Tangent'])
    return m


# ---- shapes ---------------------------------------------------------------------------------------------

def rounded_rect(x, y, w, h, r, segs=8):
    """The outline of a window rect with corner radius r, as window points, clockwise on screen from the top left."""
    r = max(0.0, min(r, w / 2, h / 2))
    if r <= 0:
        return [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
    pts = []
    for cx, cy, a0 in ((x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)):
        for i in range(segs + 1):
            a = math.radians(a0 + 90 * i / segs)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def prism(name, outline, z0, z1, material, inset=0.0, collection=None, top_outline=None):
    """A solid from z0 to z1 (pixels) over a window outline; with inset, the top face is the outline pulled in by
    `inset` pixels, so the sides lean (inset = z1 - z0 gives 45 degree sides); or the top is top_outline, an
    outline with as many points."""
    bm = bmesh.new()
    bottom = [bm.verts.new(W(px, py, z0)) for px, py in outline]
    top_pts = top_outline or (_offset(outline, -inset) if inset else outline)
    top = [bm.verts.new(W(px, py, z1)) for px, py in top_pts]
    n = len(outline)
    bm.faces.new(bottom[::-1])
    bm.faces.new(top)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((bottom[i], bottom[j], top[j], top[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    (collection or bpy.context.scene.collection).objects.link(o)
    o.data.materials.append(material)
    return o


def _offset(outline, d):
    """The outline moved outward by d pixels (inward for d < 0), corner by corner along the bisectors."""
    n = len(outline)
    area = sum(outline[i][0] * outline[(i + 1) % n][1] - outline[(i + 1) % n][0] * outline[i][1] for i in range(n))
    sgn = 1 if area > 0 else -1   # window y is down, so a positive area is clockwise on screen
    out = []
    for i in range(n):
        p0, p1, p2 = Vector((*outline[i - 1], 0)), Vector((*outline[i], 0)), Vector((*outline[(i + 1) % n], 0))
        e0, e1 = (p1 - p0).normalized(), (p2 - p1).normalized()
        n0 = Vector((e0.y, -e0.x, 0)) * sgn
        n1 = Vector((e1.y, -e1.x, 0)) * sgn
        bis = (n0 + n1)
        if bis.length < 1e-9:
            bis = n0
        bis.normalize()
        k = d / max(bis.dot(n0), 0.2)
        q = p1 + bis * k
        out.append((q.x, q.y))
    return out


def rounded_poly(pts, r, segs=6):
    """A closed window polygon with every corner rounded by up to r, as window points."""
    out = []
    n = len(pts)
    for i in range(n):
        p0, p1, p2 = Vector((*pts[i - 1], 0)), Vector((*pts[i], 0)), Vector((*pts[(i + 1) % n], 0))
        a, b = (p0 - p1), (p2 - p1)
        rr = min(r, a.length / 2, b.length / 2)
        if rr <= 1e-6:
            out.append((p1.x, p1.y))
            continue
        a.normalize(); b.normalize()
        s0, s1 = p1 + a * rr, p1 + b * rr   # a quadratic from s0 through the corner to s1
        for k in range(segs + 1):
            t = k / segs
            q = s0 * (1 - t) ** 2 + p1 * 2 * t * (1 - t) + s1 * t * t
            out.append((q.x, q.y))
    return out


def pocket(name, outline_top, outline_floor, depth, material, collection):
    """A cutter for a pocket depth pixels deep: its floor outline at -depth, its top outline at the surface (and
    a pixel above it, so it opens cleanly). The outlines need as many points; their difference is the wall."""
    return prism(name, outline_floor, -depth, 1.0, material, collection=collection, top_outline=_offset(outline_top, 1.0))


def bevel(o, width_px, segments=3):
    md = o.modifiers.new('Bevel', 'BEVEL')
    md.width = width_px * PX
    md.segments = segments
    md.limit_method = 'ANGLE'
    md.harden_normals = False
    return md


def clear(collection_name):
    c = bpy.data.collections.get(collection_name)
    if c is None:
        c = bpy.data.collections.new(collection_name)
        bpy.context.scene.collection.children.link(c)
    for o in list(c.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    return c
