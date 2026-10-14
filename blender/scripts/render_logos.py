# Builds blender/logos.blend and renders the wordmarks into the skin at every scale:
#   branding/fsvr_logo           the FSVR wordmark (logos/fsvr_wordmark.svg, the Installer's: the FS1R logo's F,
#                                S and R and a V drawn to match) in the panel's blue, x2 tiles: rest, hover
#   branding/fsvr_logo_black     the same in black, for the About box
#   branding/musica_studio       "musica." black and "studio" grey in Analog Whispers, x2 tiles: rest, and
#   branding/musica_studio_small hover all grey; the smaller for the top bar
# The musica.studio wordmark is set from the font kept beside the Installer (../Installer, not in this repo) and
# turned into outlines, so the .blend holds its shapes and not the font.
#   In Blender (blender/scripts on sys.path): import render_logos; render_logos.run()
#   or: blender -b -P blender/scripts/render_logos.py -- [branding/musica_studio_small ...]
# Named wordmarks are rendered alone (the FSVR wordmark is not built unless one of its two is named), and
# logos.blend is then left as it is.
import os, sys
import bpy
from mathutils import Matrix
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import kit
from sprites import srgb

SVG = os.path.join(kit.ROOT, 'blender', 'logos', 'fsvr_wordmark.svg')
FONT = os.path.join(kit.ROOT, '..', 'Installer', 'Analog Whispers FREE.ttf')
BLUE = (0x01, 0x5b, 0x8e)
GREY = (0x78, 0x74, 0x78)   # the installer's grey (Installer/ui.go)


def fit(objs, x, y, w, h):
    """Scales and moves objs (flat in the scene's XY) to fill window rect (x, y, w, h), keeping their aspect,
    centred."""
    bpy.context.view_layer.update()
    pts = [o.matrix_world @ v.co for o in objs for v in o.data.vertices]
    x0, x1 = min(p.x for p in pts), max(p.x for p in pts)
    y0, y1 = min(p.y for p in pts), max(p.y for p in pts)
    k = min(w * kit.PX / (x1 - x0), h * kit.PX / (y1 - y0))
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    tx, ty = (x + w / 2) * kit.PX, -(y + h / 2) * kit.PX
    for o in objs:
        o.matrix_world = __import__('mathutils').Matrix.Translation((tx, ty, 0)) @ __import__('mathutils').Matrix.Scale(k, 4) @ \
            __import__('mathutils').Matrix.Translation((-cx, -cy, 0)) @ o.matrix_world


def meshed(o):
    """A curve or text object as a mesh object in its place (its filled shape), the original removed."""
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(o.evaluated_get(dg))
    m = bpy.data.objects.new(o.name + '_mesh', me)
    m.matrix_world = o.matrix_world.copy()
    for c in o.users_collection:
        c.objects.link(m)
    bpy.data.objects.remove(o, do_unlink=True)
    return m


def flat(curve_objs):
    """Imported outlines as flat filled meshes."""
    for o in curve_objs:
        o.data.dimensions = '2D'
        o.data.fill_mode = 'BOTH'
    return [meshed(o) for o in curve_objs]


def wordmark(c, rect):
    before = set(bpy.data.objects)
    bpy.ops.import_curve.svg(filepath=SVG)
    objs = [o for o in bpy.data.objects if o not in before and o.type == 'CURVE']
    for o in objs:
        for cc in o.users_collection:
            cc.objects.unlink(o)
        c.objects.link(o)
    objs = flat(objs)
    fit(objs, *rect)
    for o in objs:   # now in window units: give it its depth and a bevel to catch the light
        sol = o.modifiers.new('Solid', 'SOLIDIFY')
        sol.thickness = 2.0 * kit.PX
        sol.offset = 1
        bev = o.modifiers.new('Bevel', 'BEVEL')
        bev.width = 0.45 * kit.PX
        bev.segments = 2
        bev.limit_method = 'ANGLE'
    return objs


def text(c, body):
    cu = bpy.data.curves.new('Txt', 'FONT')
    cu.body = body
    cu.font = bpy.data.fonts.load(FONT, check_existing=True)
    cu.size = 1.0
    o = bpy.data.objects.new('Txt_' + body, cu)
    c.objects.link(o)
    m = meshed(o)
    bpy.data.curves.remove(cu)
    return m


def musica(c, w, h):
    a = text(c, 'musica.')
    b = text(c, 'studio')
    bpy.context.view_layer.update()
    ax1 = max((a.matrix_world @ v.co).x for v in a.data.vertices)
    bx0 = min((b.matrix_world @ v.co).x for v in b.data.vertices)
    for f in list(bpy.data.fonts):   # the shapes are all the file keeps of the font
        bpy.data.fonts.remove(f)
    b.location.x += ax1 - bx0 + 0.04   # "studio" right after the full stop, as the installer sets it
    fit([a, b], 1, 1, w - 2, h - 2)
    for o in (a, b):
        sol = o.modifiers.new('Solid', 'SOLIDIFY')
        sol.thickness = 1.0 * kit.PX
        sol.offset = 1
    return a, b


def run(scales=kit.SCALES, save=True, only=None):
    """Renders the wordmarks; with only (a list of their names) just those."""
    want = lambda n: not only or n in only
    bpy.ops.wm.read_homefile(use_empty=True)
    kit.studio(samples=96)
    blue = kit.plain('LogoBlue', srgb(BLUE), metallic=0.6, roughness=0.3)
    blue_hover = kit.plain('LogoBlueHover', srgb((0x2a, 0x86, 0xc0)), metallic=0.6, roughness=0.3)
    black = kit.plain('LogoBlack', (0.0, 0.0, 0.0), roughness=0.5)
    grey = kit.plain('LogoGrey', srgb(GREY), roughness=0.6)
    def paint(objs, m):
        for o in objs:
            o.data.materials.clear()
            o.data.materials.append(m)
    fl = None
    if want('branding/fsvr_logo') or want('branding/fsvr_logo_black'):
        fl = kit.clear('FsvrLogo')
        logo = wordmark(fl, (2, 2, 158, 22))
        if want('branding/fsvr_logo'):
            states = [lambda: paint(logo, blue), lambda: paint(logo, blue_hover)]
            kit.tiles('branding/fsvr_logo', (0, 0, 162, 26), states, scales=scales)
        if want('branding/fsvr_logo_black'):
            kit.tiles('branding/fsvr_logo_black', (0, 0, 162, 26), [lambda: paint(logo, black)], scales=scales)
        paint(logo, blue)
        fl.hide_render = True
    for name, (w, h) in (('branding/musica_studio', (151, 21)), ('branding/musica_studio_small', (128, 17))):
        if not want(name):
            continue
        mc = kit.clear('Musica')
        a, b = musica(mc, w, h)
        kit.tiles(name, (0, 0, w, h), [lambda: (paint([a], black), paint([b], grey)), lambda: paint([a, b], grey)], scales=scales)
    if fl:
        fl.hide_render = False
    if save and 'Musica' in bpy.data.collections:   # the smaller musica.studio under the FSVR logo, as the file opens
        for o in bpy.data.collections['Musica'].objects:
            o.location.y -= 30 * kit.PX
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(kit.ROOT, 'blender', 'logos.blend'))


if __name__ == '__main__':
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    run(only=args or None, save=not args)
