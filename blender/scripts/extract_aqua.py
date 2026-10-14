# Isolates the blue mottled chrome the old skin had burnt into its four window backgrounds, into
#   blender/textures/aqua_mottle.png   the texture over the whole old window, 1422 x 843, as it was drawn
#   blender/textures/aqua_env.png      its soft part (the blurred room it reflects), for the Blender materials
#   blender/textures/aqua_grain.png    its hammered speckle, a tileable 256 x 256 normal-free height patch
# The old runtime drew each flat panel pixel as the texture's colour at its window point times the panel's own
# lightness (hollow/docs/skin-format.md, surface), so dividing each panel by its lightness gives the texture back.
# Panels meet at seams where the texture is continuous; the lightness ratios come from there.
#   python blender/scripts/extract_aqua.py [git revision]   (from the repo root; numpy and Pillow): the revision
#   holds the old backgrounds, d80f588 (the merge of PR 23) or any commit before the Blender chrome
import io, subprocess, sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
REV = sys.argv[1] if len(sys.argv) > 1 else 'd80f588'
OUT = ROOT / 'blender' / 'textures'
W, H = 1422, 843


def old(name):
    data = subprocess.run(['git', 'show', f'{REV}:plugin/skin/images/backgrounds/{name}.png'], cwd=ROOT,
                          capture_output=True, check=True).stdout
    return np.asarray(Image.open(io.BytesIO(data)).convert('RGB'), dtype=np.float64)


# where each background sat in the window, and the part of it that is flat chrome (inside its rim, clear of
# the keys and the wheel slots), in its own pixels
PANELS = {
    'topbar': ((0, 0), [(10, 10, 1412, 124)]),
    'sidebar': ((0, 134), [(10, 4, 178, 532)]),
    'page': ((188, 134), [(4, 4, 1222, 530)]),
    'keys': ((0, 669), [(10, 6, 52, 168), (90, 6, 122, 168), (160, 6, 196, 168)]),
}
win = np.zeros((H, W, 3))
mask = np.zeros((H, W), bool)
panel_of = np.full((H, W), -1)
for k, (name, ((ox, oy), rects)) in enumerate(PANELS.items()):
    img = old(name)
    for x0, y0, x1, y1 in rects:
        win[oy + y0:oy + y1, ox + x0:ox + x1] = img[y0:y1, x0:x1]
        mask[oy + y0:oy + y1, ox + x0:ox + x1] = True
        panel_of[oy + y0:oy + y1, ox + x0:ox + x1] = k

lum = win @ np.array([77, 150, 29]) / 256


def blur(a, m, r):
    """Mean of a over the masked pixels within r, by box sums (holes do not count)."""
    k = 2 * r + 1
    pad = lambda x: np.pad(x, r, mode='constant')
    def box(x):
        c = np.cumsum(np.cumsum(pad(x), 0), 1)
        c = np.pad(c, ((1, 0), (1, 0)))
        return c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]
    return box(a * m) / np.maximum(box(m.astype(float)), 1e-9)


# each panel's lightness relative to the page: the ratio of the texture's local mean on either side of a seam
names = list(PANELS)
gain = {names.index('page'): 1.0}
seams = [('sidebar', 'page', 'x', 188), ('topbar', 'page', 'y', 134), ('topbar', 'sidebar', 'y', 134), ('keys', 'sidebar', 'y', 669)]
for a, b, axis, at in seams:
    ia, ib = names.index(a), names.index(b)
    if ib not in gain:
        ia, ib, a, b = ib, ia, b, a
    ma, mb = panel_of == ia, panel_of == ib
    la, lb = blur(lum, ma, 24), blur(lum, mb, 24)
    band = (ma | mb)
    if axis == 'x':
        sel = np.zeros_like(band); sel[:, at - 30:at + 30] = True
    else:
        sel = np.zeros_like(band); sel[at - 30:at + 30, :] = True
    both = sel & (blur(ma.astype(float), np.ones_like(ma), 24) > 0.2) & (blur(mb.astype(float), np.ones_like(mb), 24) > 0.2)
    ratio = np.median(la[both] / np.maximum(lb[both], 1)) if both.any() else 1.0
    gain[ia] = gain[ib] * ratio
    print(f'{a} / {b}: {ratio:.3f}')
tex = win.copy()
for k, g in gain.items():
    tex[panel_of == k] /= g

# fill the holes: the keys' part of the window by mirroring the page above it, then the rims and slots by
# repeated masked blurs at shrinking radii, so each takes its surroundings' soft colour
filled, known = tex.copy(), mask.copy()
KEYS_TOP, KEYS_LEFT = 675, 196
src = 2 * KEYS_TOP - 1 - np.arange(KEYS_TOP, H)
filled[KEYS_TOP:, KEYS_LEFT:] = filled[src, KEYS_LEFT:]
known[KEYS_TOP:, KEYS_LEFT:] = known[src, KEYS_LEFT:]
r = 64
while not known.all():
    est = np.stack([blur(filled[..., c], known, r) for c in range(3)], -1)
    grow = blur(known.astype(float), np.ones_like(known), r) > 0
    take = ~known & grow
    if r > 2:
        r //= 2
    filled[take] = est[take]
    known = known | grow
env = np.stack([blur(filled[..., c], np.ones((H, W), bool), 6) for c in range(3)], -1)
env = np.stack([blur(env[..., c], np.ones((H, W), bool), 6) for c in range(3)], -1)
# the grain: the texture over its soft part where it was measured; a 256 x 256 patch of it from the middle
# of the page, made tileable by crossfading it with itself shifted half a tile
ratio = (filled / np.maximum(env, 1)).mean(-1)
y0, x0 = 300, 600
patch = ratio[y0:y0 + 256, x0:x0 + 256]
z = (patch - patch.mean()) / max(patch.std(), 1e-9)
t = np.linspace(0, np.pi, 256)
wgt = np.sin(t)[None, :] ** 2 * np.sin(t)[:, None] ** 2
z = z * wgt + np.roll(np.roll(z, 128, 0), 128, 1) * (1 - wgt)
z = (z - z.mean()) / z.std()
spread = float(ratio[mask].std())
tiled = 1 + spread * np.tile(z, (H // 256 + 1, W // 256 + 1))[:H, :W]
measured = mask.copy()
measured[KEYS_TOP:, KEYS_LEFT:] = mask[src, KEYS_LEFT:]   # the mirrored part keeps the grain it came with
mottle = np.where(measured[..., None], filled, env * tiled[..., None])
OUT.mkdir(parents=True, exist_ok=True)
Image.fromarray(np.clip(mottle, 0, 255).astype(np.uint8)).save(OUT / 'aqua_mottle.png')
Image.fromarray(np.clip(env, 0, 255).astype(np.uint8)).save(OUT / 'aqua_env.png')
Image.fromarray(np.clip(128 + 40 * z, 0, 255).astype(np.uint8), 'L').save(OUT / 'aqua_grain.png')
print('grain contrast (std of texture / soft part):', round(spread, 4), '- aqua_grain.png holds it at 40 levels per unit')
print('wrote', OUT / 'aqua_mottle.png', OUT / 'aqua_env.png', OUT / 'aqua_grain.png')
