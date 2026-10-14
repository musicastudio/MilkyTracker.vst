# The violet chrome: FSVR's aqua textures turned to MilkyTracker's lavender violet, into
#   blender/textures/violet_env.png     from aqua_env.png
#   blender/textures/violet_mottle.png  from aqua_mottle.png
# (aqua_grain.png is grey, so the violet chrome uses it as it is). Every cyan or teal colour (an sRGB hue from
# BAND[0] to BAND[1] degrees) turns SHIFT degrees, keeping its HLS lightness and saturation, so the aqua's
# 180-190 lands at 260-270; the LCD's green (hue 142), the orange caps, the greys and blacks keep their colour.
# skin.json's UI colours follow the same rule (#73888a became #7d738a), and kit.violet() applies it to the
# materials' colours.
#   python -I blender/scripts/violet_textures.py   (numpy and Pillow; Blender's Python imports rotate() alone)
import os
import numpy as np

SHIFT = 80.0
BAND = (150.0, 235.0)


def rotate(rgb):
    """sRGB floats (..., 3) with every hue in BAND turned SHIFT degrees, the max and min channel kept (so the HLS
    lightness and saturation stay). Greys and hues outside BAND come back unchanged."""
    a = np.asarray(rgb, dtype=np.float64)
    mx, mn = a.max(-1), a.min(-1)
    d = mx - mn
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    safe = np.where(d > 0, d, 1)
    h = np.where(mx == r, ((g - b) / safe) % 6, np.where(mx == g, (b - r) / safe + 2, (r - g) / safe + 4)) * 60
    turn = (d > 1e-9) & (h >= BAND[0]) & (h <= BAND[1])
    h2 = (h + SHIFT) % 360
    out = a.copy()
    for c, n in enumerate((5, 3, 1)):   # the HSV form: channel = max - d * clamp(min(k, 4 - k), 0, 1)
        k = (n + h2 / 60) % 6
        out[..., c] = np.where(turn, mx - d * np.clip(np.minimum(k, 4 - k), 0, 1), a[..., c])
    return out


if __name__ == '__main__':
    from PIL import Image
    tex = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'textures')
    for src, dst in (('aqua_env.png', 'violet_env.png'), ('aqua_mottle.png', 'violet_mottle.png')):
        a = np.asarray(Image.open(os.path.join(tex, src)).convert('RGB'), dtype=np.float64) / 255
        v = np.clip(np.floor(rotate(a) * 255 + 0.5), 0, 255).astype(np.uint8)
        Image.fromarray(v).save(os.path.normpath(os.path.join(tex, dst)))
        print('wrote', os.path.normpath(os.path.join(tex, dst)), v.shape[1], 'x', v.shape[0])
