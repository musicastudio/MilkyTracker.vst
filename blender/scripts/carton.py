# MilkyTracker's milk carton (resources/pictures/carton.png, its application icon) beside the title, as
# branding/carton at every scale the skin ships, each resampled from the 128 px original rather than from 1x.
#   python blender/scripts/carton.py       (from the repo root, needs Pillow)
from pathlib import Path
import math
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SKIN = ROOT / 'plugin' / 'skin'
SIZE = 28            # px at 1x, the title's height
SCALES = [1, 1.5, 2]   # kit.SCALES


def lround(v):   # kit.lround: std::lround, as the runtime rounds a scaled size
    return int(math.floor(v + 0.5))


if __name__ == '__main__':
    src = Image.open(ROOT / 'resources' / 'pictures' / 'carton.png').convert('RGBA')
    for s in SCALES:
        out = SKIN / 'images' if s == 1 else SKIN / 'scales' / ('%g' % s)
        (out / 'branding').mkdir(parents=True, exist_ok=True)
        n = lround(SIZE * s)
        src.resize((n, n), Image.LANCZOS).save(out / 'branding' / 'carton.png')
    print('branding/carton')
