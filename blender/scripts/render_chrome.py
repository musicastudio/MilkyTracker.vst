# Renders the main window's chrome from chrome.blend into the skin, at every scale (kit.SCALES):
#   backgrounds/main      the whole window, opaque to its edges: main.json's fill
#   backgrounds/dialog    a raised plate of the same chrome, the patterns dialog's ground
# In Blender (blender/scripts on sys.path): import render_chrome; render_chrome.run()
# or: blender -b blender/chrome.blend -P blender/scripts/render_chrome.py
# (without chrome.blend open it builds the chrome first, as build_chrome.py does, without saving it)
import os, sys
import bpy
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import kit, layout as L, build_chrome as B


def run(scales=kit.SCALES):
    if 'Cutters' not in bpy.data.collections:
        B.build(save=False)
    kit.studio(samples=48)
    for s in scales:   # the window's own ground: opaque to its edges
        a = kit.render(0, 0, L.W, L.H, s)
        a[..., 3] = 1
        kit.save(a, 'backgrounds/main', s)
    kit.meta('backgrounds/main', surface=False)
    dialog(scales)


def dialog(scales=kit.SCALES):
    """The patterns dialog's plate: the sheet's chrome where the runtime centres the dialog, its edge bevelled, the
    window's wells out of sight."""
    sheet = bpy.data.collections['Sheet']
    sheet.hide_render = True
    c = kit.clear('Dialog')
    x, y, w, h = L.DIALOG
    o = kit.prism('DialogPlate', kit.rounded_rect(x, y, w, h, L.RADIUS), -6, 0, bpy.data.materials['Chrome'], collection=c)
    kit.bevel(o, 1.5, 2)
    kit.tiles('backgrounds/dialog', L.DIALOG, [None], scales=scales)
    for ob in list(c.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    sheet.hide_render = False


if __name__ == '__main__':
    run()
