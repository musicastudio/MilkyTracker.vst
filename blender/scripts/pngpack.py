# PNGs as the skin wants them, written with numpy and zlib alone (so Blender's Python can write them): every
# row filtered the way libpng's heuristic picks (the filter with the smallest sum of absolute bytes), deflated
# at level 9, RGB when the image is opaque. A quarter smaller than Blender's own PNGs and lossless.
#   python blender/scripts/pngpack.py <png or folder> ...   repacks those files in place (needs Pillow to read)
import os, struct, sys, zlib
import numpy as np


def write_png(path, a):
    """a: uint8 rows top first, with 3 or 4 channels; 4 with every alpha 255 is written as 3."""
    if a.shape[2] == 4 and (a[..., 3] == 255).all():
        a = a[..., :3]
    h, w, c = a.shape
    x = a.astype(np.int16).reshape(h, w * c)
    up = np.vstack([np.zeros((1, w * c), np.int16), x[:-1]])
    left = np.hstack([np.zeros((h, c), np.int16), x[:, :-c]])
    ul = np.hstack([np.zeros((h, c), np.int16), up[:, :-c]])
    p = left + up - ul
    pa, pb, pc = abs(p - left), abs(p - up), abs(p - ul)
    paeth = np.where((pa <= pb) & (pa <= pc), left, np.where(pb <= pc, up, ul))
    cands = [((f) & 0xff).astype(np.uint8) for f in (x, x - left, x - up, x - (left + up) // 2, x - paeth)]
    best = np.stack([np.abs(f.astype(np.int8).astype(np.int16)).sum(1) for f in cands]).argmin(0)
    rows = np.empty((h, w * c + 1), np.uint8)
    rows[:, 0] = best
    for k in range(5):
        rows[best == k, 1:] = cands[k][best == k]

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6 if c == 4 else 2, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(rows.tobytes(), 9)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    from PIL import Image
    files = []
    for arg in sys.argv[1:]:
        if os.path.isdir(arg):
            files += [os.path.join(d, f) for d, _, fs in os.walk(arg) for f in fs if f.endswith('.png')]
        else:
            files.append(arg)
    before = after = 0
    for f in files:
        before += os.path.getsize(f)
        write_png(f, np.asarray(Image.open(f).convert('RGBA')))
        after += os.path.getsize(f)
    print('%d files, %.1f MB -> %.1f MB' % (len(files), before / 1048576, after / 1048576))
