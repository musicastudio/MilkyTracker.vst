# The skin's sprites that are not the window's chrome: buttons, plates, displays, glyphs. Each is built in a
# collection of its own at the window's origin, at its size at 1x, and rendered by kit.tiles at every scale,
# one tile per state. A shadow catcher under it puts its soft shadow into the sprite's alpha.
#   In Blender (blender/scripts on sys.path): import sprites; sprites.run('buttons')   (or a list of names)
#   or: blender -b blender/buttons.blend -P blender/scripts/sprites.py -- buttons
import os, sys, math
import bpy
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import kit
from icons import ICONS

LIT = (0x82, 0xca, 0x9c)   # whatever is lit, lights in the LCD's green: the browser's chosen row


def srgb(c):
    """An sRGB byte triple as linear floats, for a colour the render should show as it reads."""
    return tuple(((v / 255 + 0.055) / 1.055) ** 2.4 if v / 255 > 0.04045 else v / 255 / 12.92 for v in c)


def materials():
    m = {}
    m['slate'] = kit.plain('Slate', (0.030, 0.036, 0.042), metallic=0.45, roughness=0.36)
    m['slate_hover'] = kit.plain('SlateHover', (0.045, 0.054, 0.062), metallic=0.45, roughness=0.36)
    m['silver'] = kit.plain('Silver', (0.34, 0.40, 0.43), metallic=0.55, roughness=0.34)
    m['silver_hover'] = kit.plain('SilverHover', (0.42, 0.48, 0.51), metallic=0.55, roughness=0.34)
    m['silver_down'] = kit.plain('SilverDown', (0.15, 0.18, 0.20), metallic=0.5, roughness=0.38)
    m['pale'] = kit.plain('Pale', (0.80, 0.80, 0.80), roughness=0.6)
    m['pale_hover'] = kit.plain('PaleHover', (0.69, 0.69, 0.69), roughness=0.6)
    m['glass'] = kit.plain('Glass', (0.9, 0.93, 0.94), roughness=0.3, alpha=0.45)
    m['socket'] = kit.plain('Socket', (0.008, 0.01, 0.013), metallic=0.1, roughness=0.6)
    for name, c, s in (('lit', LIT, 1.0), ('glyph', (232, 238, 240), 1.0), ('glyph_dim', (150, 160, 165), 1.0),
                       ('ink', (40, 46, 50), 1.0)):
        mat = kit.plain('E_' + name, (0, 0, 0), roughness=1, emission=srgb(c), strength=s)
        m[name] = mat
    return m


M = {}


# ---- shapes ---------------------------------------------------------------------------------------------

def body(c, x, y, w, h, r, height, mat, bevel=1.0, name='Body', square=False):
    """A rounded slab; square leaves its top left corner square, for a box with a group tab standing there."""
    pts = kit.rounded_rect(x, y, w, h, r, 8)
    if square and r > 0:
        pts = pts[:-9] + [(x, y)]   # the top left arc is the outline's last 9 points
    o = kit.prism(name, pts, 0, height, mat, collection=c)
    if bevel:
        kit.bevel(o, bevel, 3)
    return o


def catcher(c, w, h):
    o = kit.prism('Catcher', kit.rounded_rect(-20, -20, w + 40, h + 40, 0), -0.5, 0, M['silver'], collection=c)
    o.is_shadow_catcher = True
    return o


def glyph(c, name, x, y, size, mat, z=1.0):
    """Icon `name` from icons.py in the size x size square at (x, y), standing z px off the surface."""
    spec = ICONS[name]
    if 'svg' in spec:
        return svg_glyph(c, spec['svg'], x, y, size, mat, z)
    k = size / 24
    objs = []
    for i, line in enumerate(spec.get('lines', [])):
        cu = bpy.data.curves.new('%s_%d' % (name, i), 'CURVE')
        cu.dimensions = '3D'
        cu.bevel_depth = spec['width'] * k / 2 * kit.PX
        cu.bevel_resolution = 3
        cu.use_fill_caps = True
        sp = cu.splines.new('POLY')
        closed = line[0] == line[-1]
        pts = line[:-1] if closed else line
        sp.points.add(len(pts) - 1)
        for p, (px, py) in zip(sp.points, pts):
            v = kit.W(x + px * k, y + py * k, z)
            p.co = (v.x, v.y, v.z, 1)
        sp.use_cyclic_u = closed
        o = bpy.data.objects.new(cu.name, cu)
        c.objects.link(o)
        o.data.materials.append(mat)
        objs.append(o)
    for poly in ([spec['fill']] if spec.get('fill') else []) + spec.get('fills', []):
        objs.append(kit.prism(name + '_fill', [(x + px * k, y + py * k) for px, py in poly], z - 0.5, z + 0.5, mat, collection=c))
    for (cx, cy, r) in spec.get('dots', []):
        objs.append(kit.prism(name + '_dot', [(x + (cx + r * math.cos(a / 8 * math.pi)) * k, y + (cy + r * math.sin(a / 8 * math.pi)) * k)
                                              for a in range(16)], z - 0.5, z + 0.5, mat, collection=c))
    return objs


def svg_glyph(c, file, x, y, size, mat, z):
    """An SVG's filled outline fitted to the size x size square at (x, y), 1 px thick about z px up, as the
    stroke icons' fills are."""
    from render_logos import flat, fit
    before = set(bpy.data.objects)
    bpy.ops.import_curve.svg(filepath=os.path.join(kit.ROOT, 'blender', 'icons', file))
    objs = [o for o in bpy.data.objects if o not in before and o.type == 'CURVE']
    for o in objs:
        for cc in o.users_collection:
            cc.objects.unlink(o)
        c.objects.link(o)
    objs = flat(objs)
    fit(objs, x, y, size, size)
    for o in objs:
        o.location.z += z * kit.PX
        sol = o.modifiers.new('Solid', 'SOLIDIFY')
        sol.thickness = 1.0 * kit.PX
        sol.offset = 0   # centred, whichever way the import's normals face
        o.data.materials.clear()
        o.data.materials.append(mat)
    return objs


def use(objs, mat):
    for o in objs:
        o.data.materials[0] = mat


def show(objs, on):
    for o in objs:
        o.hide_render = not on


# ---- the buttons ----------------------------------------------------------------------------------------
# name: (w, h, builder, skin.json fields beyond tiles). A builder makes the sprite in collection c and returns
# its states in tile order.

def dark(w, h, r=4, underline=False, icon=None, icon_x=None, icon_size=16, outline=False):
    """The top bar's dark buttons: a slate key, lit by a green bar under it, a green glyph, or a green rim."""
    def build(c):
        b = body(c, 1, 1, w - 2, h - 3, r, 3, M['slate'], 1.2)
        rim = body(c, 0.5, 0.5, w - 1, h - 2, r + 0.5, 1.5, M['lit'], 0, 'Rim') if outline else None
        bar = body(c, 8, h - 5, w - 16, 1.6, 0.8, 3.6, M['lit'], 0, 'Bar') if underline else None   # under the text, not through it
        g = glyph(c, icon, icon_x if icon_x is not None else (w - icon_size) / 2, (h - 2 - icon_size) / 2, icon_size, M['glyph'], 3.5) if icon else []
        def st(on, hover):
            def f():
                b.data.materials[0] = M['slate_hover' if hover else 'slate']
                if rim: show([rim], on)
                if bar: show([bar], on)
                if g: use(g, M['lit' if on else 'glyph'])
            return f
        return [st(False, False), st(True, False), st(False, True), st(True, True)]
    return build


def dark_step(w, h, icon):
    def build(c):
        b = body(c, 1, 1, w - 2, h - 3, 3, 3, M['slate'], 1.0)
        g = glyph(c, icon, (w - 12) / 2, (h - 2 - 12) / 2, 12, M['glyph'], 3.5)
        def st(down):
            def f():
                b.data.materials[0] = M['slate_hover' if down else 'slate']
                use(g, M['lit' if down else 'glyph'])
            return f
        return [st(False), st(True)]
    return build


def silver(w, h, r=4):
    """The small silver keys (the operator panel's 1 to 8, V and N): sunk and lit green while on."""
    def build(c):
        b = body(c, 1, 1, w - 2, h - 3, r, 3, M['silver'], 1.2)
        down = body(c, 2.5, 2.5, w - 5, h - 6, r - 1, 1.6, M['silver_down'], 1.0, 'Down')
        bar = body(c, 5, h - 6.5, w - 10, 1.6, 0.8, 2.2, M['lit'], 0, 'Bar')
        def st(on, hover):
            def f():
                show([b], not on)
                show([down, bar], on)
                b.data.materials[0] = M['silver_hover' if hover else 'silver']
            return f
        return [st(False, False), st(True, False), st(False, True), st(True, True)]
    return build


def page_button(w, h):
    """The expert pages' keys: a silver plate with a dark socket for the glyph at its left, lit green while
    its page shows."""
    def build(c):
        b = body(c, 1, 1, w - 2, h - 3, 5, 3, M['silver'], 1.4)
        sock = body(c, 5, 5, 28, h - 11, 4, 3.4, M['socket'], 1.0, 'Socket')
        bar = body(c, 38, h - 7, w - 46, 2, 1, 3.6, M['lit'], 0, 'Bar')
        def st(on, hover):
            def f():
                b.data.materials[0] = M['silver_hover' if hover else 'silver']
                show([bar], on)
            return f
        return [st(False, False), st(True, False), st(False, True), st(True, True)]
    return build


def pill(w, h):
    def build(c):
        b = body(c, 1, 1, w - 2, h - 2, h / 2 - 1, 2, M['glass'], 1.0)
        def st(on):
            return lambda: b.data.materials.__setitem__(0, M['lit' if on else 'glass'])
        return [st(False), st(True), st(True), st(False)]
    return build


def radio(w, h):
    def build(c):
        ring = body(c, (w - 12) / 2, (h - 12) / 2, 12, 12, 6, 2, M['slate'], 1.0)
        dot = body(c, (w - 6) / 2, (h - 6) / 2, 6, 6, 3, 2.6, M['lit'], 0.6, 'Dot')
        return [lambda: show([dot], False), lambda: show([dot], True)]
    return build


def toggle(w, h, box_x, plate=True):
    """A row's lamp: a translucent plate with a small square lamp, green while on."""
    def build(c):
        objs = [body(c, 0, 0, w, h, 2, 0.6, M['glass'], 0.4, 'Plate')] if plate else []
        lamp = body(c, box_x, (h - 12) / 2, 16, 12, 2, 2, M['silver'], 0.8, 'Lamp')
        def st(on):
            return lambda: lamp.data.materials.__setitem__(0, M['lit' if on else 'silver'])
        return [st(False), st(True)]
    return build


def blank(w, h):
    def build(c):
        b = body(c, 1, 1, w - 2, h - 3, 3, 2.5, M['silver'], 1.0)
        return [lambda: b.data.materials.__setitem__(0, M['silver']), lambda: b.data.materials.__setitem__(0, M['silver_down']),
                lambda: b.data.materials.__setitem__(0, M['silver_hover'])]
    return build


def dropdown(w, h, inv=False):
    def build(c):
        body(c, 0, 0, w, h, 2, 0.8, M['glass'] if not inv else M['silver_down'], 0.5, 'Plate')
        glyph(c, 'down', w - 19, (h - 14) / 2, 14, M['ink'] if not inv else M['glyph_dim'], 1.4)
        return [None]
    return build


def cell(w, h):
    """An algorithm thumbnail's frame: dark, a green rim when it is the voice's."""
    def build(c):
        b = body(c, 1, 1, w - 2, h - 2, 2, 1.5, M['slate'], 0.6)
        rim = body(c, 0, 0, w, h, 3, 1.0, M['lit'], 0, 'Rim')
        def st(on, hover):
            def f():
                show([rim], on)
                b.data.materials[0] = M['slate_hover' if hover else 'slate']
            return f
        return [st(False, False), st(True, False), st(False, True), st(True, True)]
    return build


def op_box(w, h):
    """An operator's box on the algorithm matrix, blank: its number is the widget's text. On, lit; with the
    operator switched off, dim."""
    def build(c):
        b = body(c, 1, 1, w - 2, h - 3, 3, 3, M['silver'], 1.0)
        def st(mat):
            return lambda: b.data.materials.__setitem__(0, M[mat])
        return [st('silver'), st('lit'), st('silver_down'), st('lit')]
    return build


def header(w, h, menu=True):
    """A dark screen's header bar: a slate band; as a dropdown, with a chevron at its right end and lighter
    under the pointer."""
    def build(c):
        b = body(c, 0, 0, w, h, 3, 1.5, M['slate'], 0.6)
        if not menu:
            return [None]
        glyph(c, 'chevron', w - 20, (h - 14) / 2, 14, M['glyph_dim'], 2.0)
        return [lambda: b.data.materials.__setitem__(0, M['slate']), lambda: b.data.materials.__setitem__(0, M['slate_hover'])]
    return build


def flat(w, h, mats):
    def build(c):
        b = kit.prism('Plate', kit.rounded_rect(0, 0, w, h, 0), 0, 0.2, M[mats[0]], collection=c)
        return [(lambda m=m: b.data.materials.__setitem__(0, M[m])) for m in mats]
    return build


BUTTONS = {
    'buttons/dark_part': (56, 26, dark(56, 26, underline=True), {'slice': [0, 8, 0, 8]}),
    'buttons/dark_wide': (112, 24, dark(112, 24, underline=True), {'slice': [0, 10, 0, 10]}),
    'buttons/dark_knobs': (44, 26, dark(44, 26, underline=True), {}),
    'buttons/dark_square': (26, 32, dark(26, 32, outline=True), {'slice': [0, 8, 0, 8]}),
    'buttons/strip_save': (72, 26, dark(72, 26, icon='save', icon_x=6), {}),
    'buttons/strip_file': (72, 26, dark(72, 26, icon='folder', icon_x=6), {}),
    'buttons/strip_editor': (72, 26, dark(72, 26, icon='editor', icon_x=6), {}),
    'buttons/strip_keys': (72, 26, dark(72, 26, icon='keys', icon_x=6), {}),
    'buttons/dark_learn': (31, 25, dark(31, 25, icon='learn', icon_size=17), {}),
    'buttons/dark_panic': (31, 25, dark(31, 25, icon='panic', icon_size=17), {}),
    'buttons/dark_up': (17, 17, dark_step(17, 17, 'up'), {}),
    'buttons/dark_down': (17, 17, dark_step(17, 17, 'down'), {}),
    'buttons/nav_small': (26, 26, silver(26, 26), {'slice': [0, 8, 0, 8]}),
    'buttons/page_button': (116, 38, page_button(116, 38), {}),
    'buttons/pill': (38, 18, pill(38, 18), {}),
    'buttons/part_radio': (16, 20, radio(16, 20), {}),
    'buttons/toggle_row': (50, 18, toggle(50, 18, 31), {'slice': [0, 21, 0, 2]}),
    'buttons/led_toggle_inv': (71, 18, toggle(71, 18, 27, plate=False), {}),
    'buttons/library_blank_narrow': (21, 29, blank(21, 29), {'slice': [0, 3, 0, 3]}),
    'buttons/dropdown_small': (50, 18, dropdown(50, 18), {'slice': [0, 23, 0, 2]}),
    'buttons/dropdown_small_inv': (50, 18, dropdown(50, 18, True), {'slice': [0, 23, 0, 2]}),
    'buttons/dropdown_header': (60, 22, header(60, 22), {'slice': [0, 24, 0, 6]}),
    'buttons/alg_cell': (24, 24, cell(24, 24), {'slice': [3, 3, 3, 3]}),
    'buttons/matrix_op': (41, 29, op_box(41, 29), {}),
    'buttons/about_button': (196, 45, flat(196, 45, ['pale', 'pale_hover']), {}),
    'buttons/about_close': (32, 32, flat(32, 32, ['pale', 'pale_hover']), {}),
}

# ---- plates, displays and glyphs ------------------------------------------------------------------------

LCD = (125, 195, 150)    # the LCD's green, and its ink
INK = (23, 48, 31)


def lcd_mats():
    M['lcd'] = kit.plain('Lcd', srgb(LCD), roughness=0.55)
    M['lcd_dot'] = kit.plain('LcdDot', srgb((116, 184, 140)), roughness=0.55)
    M['lcd_ink'] = kit.plain('LcdInk', srgb(INK), roughness=0.6)
    M['bezel'] = kit.plain('Bezel', (0.012, 0.014, 0.016), metallic=0.3, roughness=0.45)
    M['graphite'] = kit.plain('Graphite', (0.020, 0.024, 0.027), metallic=0.4, roughness=0.4)
    M['plate_glass'] = kit.plain('PlateGlass', (0.85, 0.92, 0.93), roughness=0.5, alpha=0.22)
    M['plate_glass_light'] = kit.plain('PlateGlassLight', (0.90, 0.95, 0.96), roughness=0.5, alpha=0.36)
    M['plate_dark'] = kit.plain('PlateDark', (0.025, 0.035, 0.038), roughness=0.5, alpha=0.62)
    M['well_dark'] = kit.plain('WellDark', (0.030, 0.040, 0.043), metallic=0.2, roughness=0.5)
    M['slot'] = kit.plain('Slot', (0.055, 0.072, 0.076), metallic=0.2, roughness=0.5)
    M['screen'] = kit.plain('Screen', (0.006, 0.009, 0.011), metallic=0.25, roughness=0.3)


def screen(w, h, dots=None, right=0):
    """An LCD behind its bezel: the green glass inset 3 px, and with dots (columns, rows, x, y) a faint 5 x 7
    dot cell for every character the LCD can show; `right` px at the right are the bezel's dark panel."""
    def build(c):
        lcd_mats()
        body(c, 0, 0, w, h, 4, 2.5, M['bezel'], 0.8, 'Bezel')
        kit.prism('Glass', kit.rounded_rect(3, 3, w - 6 - right, h - 6, 1.5), 0, 2.6, M['lcd'], collection=c)
        if dots:
            import bmesh
            cols, rows, x0, y0 = dots
            bm = bmesh.new()
            for r in range(rows):
                for col in range(cols):
                    for d in range(7):
                        for k in range(5):
                            x, y = x0 + 12 * col + 2 * k, y0 + 16 * r + 1 + 2 * d
                            v = [bm.verts.new(kit.W(px, py, 2.62)) for px, py in
                                 ((x + .15, y + .15), (x + 1.85, y + .15), (x + 1.85, y + 1.85), (x + .15, y + 1.85))]
                            bm.faces.new(v[::-1])
            me = bpy.data.meshes.new('Dots')
            bm.to_mesh(me)
            bm.free()
            o = bpy.data.objects.new('Dots', me)
            c.objects.link(o)
            o.data.materials.append(M['lcd_dot'])
        return [None]
    return build


def meter(w, h, segs=13):
    def build(c):
        lcd_mats()
        bars = [kit.prism('Seg%d' % i, kit.rounded_rect(0, h - 4 * (i + 1) + 1, w, 2, 0), 0, 0.3, M['lcd_dot'], collection=c)
                for i in range(segs)]
        def st(n):
            def f():
                for i, b in enumerate(bars):
                    b.data.materials[0] = M['lcd_ink' if i < n else 'lcd_dot']
            return f
        return [st(n) for n in range(segs + 1)]
    return build


def lcd_mark(w, h, icon):
    def build(c):
        lcd_mats()
        g = glyph(c, icon, (w - 12) / 2, (h - 12) / 2, 12, M['lcd_ink'], 0.4)
        return [lambda: show(g, False), lambda: show(g, True)]
    return build


def tag(w, h):
    def build(c):
        lcd_mats()
        body(c, 1, 2, w - 2, h - 5, 4, 2.4, M['plate_dark'], 0.8)
        return [None]
    return build


def plate(w, h, mat, r=3, height=1.2, bevel=0.6, inset=False, square=False):
    def build(c):
        lcd_mats()
        if inset:   # sunk: walls sloping 2 px down from the edge to a face
            import bmesh
            outer = kit.rounded_rect(0, 0, w, h, r, 8)
            inner = kit.rounded_rect(2, 2, w - 4, h - 4, max(r - 2, .5), 8)
            bm = bmesh.new()
            ov = [bm.verts.new(kit.W(x, y, 2)) for x, y in outer]   # above the shadow catcher, which hides what is under it
            iv = [bm.verts.new(kit.W(x, y, 0)) for x, y in inner]
            n = len(ov)
            for i in range(n):
                bm.faces.new((ov[i], ov[(i + 1) % n], iv[(i + 1) % n], iv[i]))
            bm.faces.new(iv)
            me = bpy.data.meshes.new('Inset')
            bm.to_mesh(me)
            bm.free()
            o = bpy.data.objects.new('Inset', me)
            c.objects.link(o)
            o.data.materials.append(M[mat])
        else:
            body(c, 0, 0, w, h, r, height, M[mat], bevel, square=square)
        return [None]
    return build


def trapezoid_tab(w, h, mat):
    def build(c):
        lcd_mats()
        o = kit.prism('Tab', kit.rounded_poly([(0, h), (h, 0), (w - h, 0), (w, h)], 2, 4), 0, 1.2, M[mat], collection=c)
        kit.bevel(o, 0.5, 2)
        return [None]
    return build


def matrix_bg(w, h):
    """The algorithm matrix's ground: a dark panel with a slot under each place an amount can stand, eight
    sources by eight destinations (op n's box at 17 + 42 (n - 1), 61 + 42 (n - 1)), and the output row."""
    def build(c):
        lcd_mats()
        body(c, 0, 0, w, h, 6, 1.5, M['well_dark'], 1.0, square=True)   # under the Algorithm tab
        for col in range(8):
            for row in range(8):
                body(c, 16 + 42 * col, 60 + 42 * row, 42, 31, 3, 1.8, M['slot'], 0.5, 'Slot')
            body(c, 16 + 42 * col, 438, 42, 31, 3, 1.8, M['slot'], 0.5, 'Out')
        return [None]
    return build


def flat_glyph(w, h, icon, mats, size=None):
    def build(c):
        lcd_mats()
        sz = size or min(w, h)
        g = glyph(c, icon, (w - sz) / 2, (h - sz) / 2, sz, M[mats[0]], 0.6)
        return [(lambda m=m: use(g, M[m])) for m in mats]
    return build


PANELS = {
    'panels/graphite_pod': (30, 59, plate(30, 59, 'graphite', 8, 2.0, 1.2), {'slice': [10, 14, 10, 14]}),
    'panels/group_box': (48, 48, plate(48, 48, 'plate_glass', 6, 1.2, 0.8), {'slice': [8, 8, 8, 8]}),
    'panels/group_box_light': (48, 48, plate(48, 48, 'plate_glass_light', 6, 1.2, 0.8), {'slice': [8, 8, 8, 8]}),
    # a box with its title's tab on its top left corner, which is square so the tab's foot runs into its edge
    'panels/group_box_tab': (48, 48, plate(48, 48, 'plate_glass', 6, 1.2, 0.8, square=True), {'slice': [8, 8, 8, 8]}),
    'panels/group_box_light_tab': (48, 48, plate(48, 48, 'plate_glass_light', 6, 1.2, 0.8, square=True), {'slice': [8, 8, 8, 8]}),
    # MilkyTracker.vst's groups: dark glass, for the tracker's light text on the darker chrome
    'panels/group_box_dark_tab': (48, 48, plate(48, 48, 'plate_dark', 6, 1.2, 0.8, square=True), {'slice': [8, 8, 8, 8]}),
    'panels/group_tab': (44, 20, trapezoid_tab(44, 20, 'plate_glass'), {'slice': [0, 20, 0, 20]}),
    'panels/group_tab_light': (44, 20, trapezoid_tab(44, 20, 'plate_glass_light'), {'slice': [0, 20, 0, 20]}),
    'panels/group_tab_dark': (44, 23, trapezoid_tab(44, 23, 'graphite'), {'slice': [0, 20, 0, 20]}),
    'panels/inset_light': (183, 110, plate(183, 110, 'plate_glass_light', 4, inset=True), {'slice': [4, 4, 4, 4]}),
    'panels/inset_light_inv': (183, 110, plate(183, 110, 'plate_dark', 4, inset=True), {'slice': [15, 15, 15, 15]}),
    'panels/label_plate': (50, 18, plate(50, 18, 'plate_glass_light', 3, 0.8, 0.4), {'slice': [3, 3, 3, 3]}),
    'panels/label_plate_inv': (50, 18, plate(50, 18, 'plate_dark', 3, 0.8, 0.4), {'slice': [3, 3, 3, 3]}),
    'panels/plate_slate': (15, 15, plate(15, 15, 'graphite', 3, 1.0, 0.5), {'slice': [4, 4, 4, 4]}),
    'panels/header_band': (60, 22, header(60, 22, menu=False), {'slice': [0, 6, 0, 6]}),   # buttons/dropdown_header with no menu
    'panels/mesh_dark_large': (408, 473, plate(408, 473, 'well_dark', 6, 1.5, 1.0, square=True), {}),   # under the Algorithm tab
    'backgrounds/fm_matrix': (408, 473, matrix_bg(408, 473), {}),
    'backgrounds/library_stripes': (48, 473, plate(48, 473, 'well_dark', 0, 0.5, 0), {'slice': [0, 0, 0, 15]}),
    'library/column_frame': (48, 68, plate(48, 68, 'plate_dark', 3, 0.8, 0.4), {'slice': [23, 3, 3, 3]}),
    'library/toolbar_bar': (24, 38, plate(24, 38, 'silver', 0, 1.5, 0.8), {'slice': [0, 6, 0, 6]}),
    'scrollbars/library_thumb_flat': (15, 30, plate(15, 30, 'silver', 3, 1.5, 0.6), {'slice': [5, 0, 5, 0]}),
    'scrollbars/list_track': (2, 2, plate(2, 2, 'well_dark', 0, 0.5, 0), {}),
    'scrollbars/morph_thumb': (20, 35, plate(20, 35, 'silver', 4, 2, 0.8), {'slice': [5, 0, 5, 0]}),
    'scrollbars/morph_track': (20, 338, plate(20, 338, 'well_dark', 6, 0.5, 0, inset=True), {'slice': [12, 0, 12, 0]}),
    'faders/wheel_slot': (100, 25, plate(100, 25, 'silver', 5, inset=True), {}),
    'faders/wheel_slot_v': (25, 100, plate(25, 100, 'silver', 5, inset=True), {}),   # the compact keyboard's, standing
}
DISPLAYS = {
    'displays/lcd_window': (330, 66, screen(330, 66, dots=(25, 3, 9, 9)), {}),   # the grid and its marks column centred in the glass
    'displays/lcd_monitor': (286, 66, screen(286, 66, right=38), {}),
    'displays/op_screen': (231, 84, plate(231, 84, 'screen', 5, inset=True), {'slice': [6, 6, 6, 6]}),   # the graphs' dark glass
    'displays/lcd_edit': (12, 16, lcd_mark(12, 16, 'edit'), {}),
    'displays/lcd_midi': (12, 16, lcd_mark(12, 16, 'note'), {}),
    'meters/lcd_level': (8, 52, meter(8, 52), {}),
    'displays/amount_tag': (40, 29, tag(40, 29), {}),
}
GLYPHS = {
    'icons/lock': (10, 12, flat_glyph(10, 12, 'lock', ['glyph_dim', 'ink'], 12), {}),
    'icons/menu_arrow': (6, 11, flat_glyph(6, 11, 'arrow', ['ink'], 11), {}),
    'icons/menu_arrow_plain': (6, 11, flat_glyph(6, 11, 'arrow', ['ink'], 11), {}),
    'icons/menu_check': (8, 8, flat_glyph(8, 8, 'check', ['ink'], 8), {}),
    'icons/menu_check_plain': (8, 8, flat_glyph(8, 8, 'check', ['ink'], 8), {}),
}
for _n in ('all_ops', 'all_envs', 'mod', 'keysc', 'filter', 'pitch'):
    GLYPHS['icons/page_' + _n] = (24, 24, flat_glyph(24, 24, 'page_' + _n, ['glyph'], 22), {})
# a number box's steppers (skin.json "steppers"): down at its left end, up at its right, lit while held; the
# _inv pair for the dark boxes
for _n, _ink in (('', 'ink'), ('_inv', 'glyph_dim')):
    GLYPHS['buttons/step_down' + _n] = (9, 14, flat_glyph(9, 14, 'down', [_ink, 'lit'], 11), {})
    GLYPHS['buttons/step_up' + _n] = (9, 14, flat_glyph(9, 14, 'up', [_ink, 'lit'], 11), {})
for _n in ('kbd_poly', 'kbd_mono', 'prio_last', 'prio_first', 'prio_top', 'prio_bottom'):
    GLYPHS['icons/' + _n] = (22, 18, flat_glyph(22, 18, _n, ['ink'], 22), {})

FAMILIES = {'buttons': BUTTONS, 'panels': PANELS, 'displays': DISPLAYS, 'glyphs': GLYPHS}


def run(which='buttons', only=None, scales=kit.SCALES):
    kit.studio(samples=64)
    global M
    M = materials()
    table = {}
    for fam in ([which] if isinstance(which, str) else which):
        table.update(FAMILIES[fam])
    for name, (w, h, build, fields) in table.items():
        if only and name not in only:
            continue
        c = kit.clear('Sprite')
        catcher(c, w, h)
        states = build(c)
        kit.tiles(name, (0, 0, w, h), states, scales=scales, **fields)
        print('rendered', name)
    kit.clear('Sprite')


def sheet(family, width=600):
    """blender/<family>.blend: every sprite of the family built side by side, each in a collection named after
    it and in its first state, with the studio set up, so the file shows the component."""
    bpy.ops.wm.read_homefile(use_empty=True)
    kit.studio(samples=64)
    global M
    M = materials()
    x = y = row = 0
    for name, (w, h, build, fields) in FAMILIES[family].items():
        if x and x + w > width:
            x, y, row = 0, y + row + 16, 0
        c = kit.clear(name.replace('/', '.'))
        catcher(c, w, h)
        states = build(c)
        if states and states[0]:
            states[0]()
        for o in c.objects:
            o.location.x += x * kit.PX
            o.location.y -= y * kit.PX
        x, row = x + w + 16, max(row, h)
    kit.frame(0, 0, width, y + row, 1)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(kit.ROOT, 'blender', family + '.blend'))


if __name__ == '__main__':
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else ['buttons']
    run(args)
