# Builds blender/chrome.blend: the main window's chrome from layout.py. One sheet of the mottled violet chrome
# with three wells cut into it, each with 45 degree walls DEPTH deep and corner RADIUS: the tracker's (its
# screen covers the whole floor, which is brushed all the same), the right column's (brushed floor) and the
# keyboard's (the dark keybed).
#   In Blender (blender/scripts on sys.path): import build_chrome; build_chrome.build()
#   or: blender -b -P blender/scripts/build_chrome.py
import os, sys
import bpy
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import kit, layout as L


def well(name, rect, material, cutters, radius=L.RADIUS, depth=L.DEPTH):
    """A well's cutter: rect at the surface, its floor depth in from every edge and depth down."""
    x, y, w, h = rect
    rf = max(radius - depth, 0.5)
    top = kit.rounded_rect(x, y, w, h, radius, 8)
    floor = kit.rounded_rect(x + depth, y + depth, w - 2 * depth, h - 2 * depth, rf, 8)
    return kit.pocket(name, top, floor, depth, material, cutters)


def build(save=True):
    bpy.ops.wm.read_homefile(use_empty=True)
    sc = kit.studio(samples=48)
    sc.name = 'Chrome'
    chrome = kit.chrome()
    floor = kit.brushed('Brushed45', colour=(0.42, 0.50, 0.52), streak=0.14)
    keybed = kit.brushed('Keybed', colour=(0.16, 0.19, 0.20), streak=0.10)
    sheet_c = kit.clear('Sheet')
    cut_c = kit.clear('Cutters')
    cut_c.hide_render = True
    cut_c.hide_viewport = True

    sheet = kit.prism('Sheet', kit.rounded_rect(0, 0, L.W, L.H, 0), -14, 0, chrome, collection=sheet_c)
    kit.bevel(sheet, 1.5, 2)
    well('TrackWell', L.TRACKWELL, floor, cut_c)
    well('ColumnWell', L.COLUMN, floor, cut_c)
    well('KeyWell', L.KEYWELL, keybed, cut_c)
    md = sheet.modifiers.new('Wells', 'BOOLEAN')
    md.operation, md.operand_type, md.collection = 'DIFFERENCE', 'COLLECTION', cut_c
    md.solver, md.material_mode = 'EXACT', 'TRANSFER'
    sheet.modifiers.move(len(sheet.modifiers) - 1, 0)   # cut first, then bevel the outer edge

    if save:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(kit.ROOT, 'blender', 'chrome.blend'))
    return sc


if __name__ == '__main__':
    build()
