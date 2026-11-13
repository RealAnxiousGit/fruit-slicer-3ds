"""HOME Menu banner and icon, plus larger copies for store listings.

The fruit are the game's own 3D models, drawn by the small software renderer below, so the art on the
HOME Menu matches what is in the game.

Writes meta/icon.png, meta/banner.png, docs/store/icon.png and docs/store/banner.png.
"""
import math
import os
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APPLE, ORANGE, LEMON, PEAR, PLUM, MELON, KIWI, BERRY, PEACH, PINEAPPLE = range(10)
WHOLE, LEFT, RIGHT = 0, 1, 2


def load_meshes():
    """Reads romfs/models/fruit_hi.bin, in the format tools/gen_models.py writes."""
    with open(os.path.join(ROOT, "romfs", "models", "fruit_hi.bin"), "rb") as f:
        data = f.read()
    assert data[:4] == b"FSM1"
    count = struct.unpack_from("<I", data, 4)[0]
    heads = struct.unpack_from(f"<{count * 3 * 5}I", data, 8)
    cur = 8 + 20 * 3 * count
    meshes = {}
    for m in range(count):
        for p in range(3):
            nv, ni = heads[(m * 3 + p) * 5], heads[(m * 3 + p) * 5 + 1]
            v = np.frombuffer(data, dtype=[("p", "<3f4"), ("n", "<3i1"), ("pad", "i1"), ("c", "<4u1")], count=nv, offset=cur)
            cur += nv * 20
            i = np.frombuffer(data, dtype="<u2", count=ni, offset=cur).reshape(-1, 3)
            cur += (ni * 2 + 3) & ~3
            meshes[(m, p)] = (v, i)
    return meshes


def rotation(rx, ry, rz):
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    x = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    y = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    z = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return z @ x @ y


def render(mesh, size, rx, ry, rz=0.0, zoom=1.0):
    """Draws one mesh into a square RGBA image, 4x oversampled. +y is down and +z faces the viewer."""
    verts, tris = mesh
    ss = size * 4
    r = rotation(rx, ry, rz)
    p = verts["p"] @ r.T
    n = (verts["n"] / 127.0) @ r.T
    # The same light as the game's shader: from the upper left, towards the viewer.
    light = np.array([-0.46, -0.62, 0.64])
    half = np.array([-0.25, -0.34, 0.9])
    shade = 0.62 + 0.5 * np.clip(n @ light, 0, 1)
    spec = 0.32 * np.clip(n @ half, 0, 1) ** 24
    col = np.clip(verts["c"][:, :3] * shade[:, None] + 255 * spec[:, None], 0, 255)
    fit = ss * 0.5 / (np.max(np.abs(verts["p"])) * 1.08) * zoom
    sx = p[:, 0] * fit + ss / 2
    sy = p[:, 1] * fit + ss / 2
    zbuf = np.full((ss, ss), -1e9)
    out = np.zeros((ss, ss, 4))
    for a, b, c in tris:
        x0, x1 = int(max(min(sx[a], sx[b], sx[c]), 0)), int(min(max(sx[a], sx[b], sx[c]) + 1, ss - 1))
        y0, y1 = int(max(min(sy[a], sy[b], sy[c]), 0)), int(min(max(sy[a], sy[b], sy[c]) + 1, ss - 1))
        if x1 < x0 or y1 < y0:
            continue
        d = (sy[b] - sy[c]) * (sx[a] - sx[c]) + (sx[c] - sx[b]) * (sy[a] - sy[c])
        if abs(d) < 1e-6:
            continue
        ys, xs = np.mgrid[y0:y1 + 1, x0:x1 + 1]
        w0 = ((sy[b] - sy[c]) * (xs - sx[c]) + (sx[c] - sx[b]) * (ys - sy[c])) / d
        w1 = ((sy[c] - sy[a]) * (xs - sx[c]) + (sx[a] - sx[c]) * (ys - sy[c])) / d
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue
        z = w0 * p[a, 2] + w1 * p[b, 2] + w2 * p[c, 2]
        front = inside & (z > zbuf[y0:y1 + 1, x0:x1 + 1])
        zbuf[y0:y1 + 1, x0:x1 + 1][front] = z[front]
        region = out[y0:y1 + 1, x0:x1 + 1]
        for k in range(3):
            region[..., k][front] = (w0 * col[a, k] + w1 * col[b, k] + w2 * col[c, k])[front]
        region[..., 3][front] = 255
    # Average in premultiplied form, so edges don't pick up a dark fringe.
    premul = out[..., :3] * (out[..., 3:] / 255)
    img = np.dstack((premul, out[..., 3]))
    small = img.reshape(size, 4, size, 4, 4).mean(axis=(1, 3))
    alpha = small[..., 3:]
    rgb = np.where(alpha > 0, small[..., :3] * 255 / np.maximum(alpha, 1e-6), 0)
    return Image.fromarray(np.clip(np.dstack((rgb, alpha)), 0, 255).astype("uint8"), "RGBA")


def tilt(img, swing=0.13, lift=0.04):
    """Turns a flat picture a little away from the viewer about its vertical axis."""
    w, h = img.size
    dx, dy = w * lift, h * swing
    # Corners of the result: the right edge recedes, so it gets shorter.
    quad = [(0, 0), (w - dx, dy * 0.8), (w - dx, h - dy * 0.8), (0, h)]
    a = np.zeros((8, 8))
    b = np.zeros(8)
    src = [(0, 0), (w, 0), (w, h), (0, h)]
    for i, ((x, y), (u, v)) in enumerate(zip(quad, src)):
        a[i * 2] = [x, y, 1, 0, 0, 0, -u * x, -u * y]
        a[i * 2 + 1] = [0, 0, 0, x, y, 1, -v * x, -v * y]
        b[i * 2], b[i * 2 + 1] = u, v
    return img.transform((w, h), Image.PERSPECTIVE, np.linalg.solve(a, b), Image.BICUBIC)


def shadow(img, blur, opacity=0.45):
    a = img.getchannel("A").filter(ImageFilter.GaussianBlur(blur)).point(lambda v: int(v * opacity))
    s = Image.new("RGBA", img.size, (24, 12, 40, 0))
    s.putalpha(a)
    return s


def put(canvas, meshes, mesh, part, cx, cy, size, rx, ry, rz=0.0, zoom=1.0):
    img = render(meshes[(mesh, part)], size, rx, ry, rz, zoom)
    pos = (int(cx - size / 2), int(cy - size / 2))
    canvas.alpha_composite(shadow(img, size * 0.05), (pos[0] + int(size * 0.05), pos[1] + int(size * 0.08)))
    canvas.alpha_composite(img, pos)


def blade_streak(canvas, x0, y0, x1, y1, width):
    """A tapered bright slash, the one mark that says what this game is about."""
    s = 4
    w, h = canvas.size
    layer = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    nx, ny = -(y1 - y0), x1 - x0
    ln = math.hypot(nx, ny) or 1
    nx, ny = nx / ln * width * s, ny / ln * width * s
    mx, my = (x0 + x1) * s / 2, (y0 + y1) * s / 2
    d.polygon([(x0 * s, y0 * s), (mx + nx, my + ny), (x1 * s, y1 * s), (mx - nx, my - ny)], fill=(255, 250, 225, 255))
    layer = layer.resize((w, h), Image.LANCZOS)
    glow = layer.filter(ImageFilter.GaussianBlur(max(1.0, width * 1.6)))
    canvas.alpha_composite(glow)
    canvas.alpha_composite(layer)


def make_icon(meshes, size):
    k = size / 48
    img = Image.new("RGBA", (size, size), (44, 34, 92, 255))
    d = ImageDraw.Draw(img)
    for y in range(size):
        t = y / max(1, size - 1)
        d.line([(0, y), (size, y)], fill=(int(58 + 150 * t), int(36 + 40 * t), int(104 + 20 * t), 255))
    d.ellipse([12 * k, 19 * k, 36 * k, 43 * k], fill=(255, 226, 150, 255))
    d.rectangle([0, 40 * k, size, size], fill=(60, 34, 86, 255))
    # A melon, split along the slash, with its faces turned to the viewer.
    put(img, meshes, MELON, LEFT, 15 * k, 18 * k, int(34 * k), 0.1, -0.85, 0.75)
    put(img, meshes, MELON, RIGHT, 33 * k, 30 * k, int(34 * k), 0.1, 0.85, 0.75)
    blade_streak(img, 45 * k, 3 * k, 3 * k, 45 * k, 1.2 * k)
    return img


def make_banner(meshes, sky, far, mid, logo, s):
    W, H = 256 * s, 128 * s
    box = (84, 56, 84 + 256, 56 + 128)
    banner = sky.crop(box)
    banner.alpha_composite(far.crop((84, 0, 84 + 256, 120)), (0, 62))
    banner.alpha_composite(mid.crop((84, 0, 84 + 256, 90)), (0, 80))
    banner = banner.resize((W, H), Image.LANCZOS)
    # Whole fruit caught turning at different angles, and a melon that has just been cut.
    put(banner, meshes, APPLE, WHOLE, 30 * s, 92 * s, 58 * s, -0.25, 0.6, -0.2)
    put(banner, meshes, MELON, LEFT, 104 * s, 94 * s, 56 * s, 0.1, -0.85, 0.65)
    put(banner, meshes, MELON, RIGHT, 140 * s, 104 * s, 56 * s, 0.1, 0.85, 0.65)
    put(banner, meshes, PINEAPPLE, WHOLE, 194 * s, 92 * s, 60 * s, -0.1, -0.5, 0.25)
    put(banner, meshes, ORANGE, WHOLE, 234 * s, 98 * s, 42 * s, -0.3, 0.8, 0.1)
    blade_streak(banner, 152 * s, 66 * s, 92 * s, 126 * s, 1.3 * s)
    lw = 196 * s
    lh = logo.height * lw // logo.width
    tilted = tilt(logo.resize((lw, lh), Image.LANCZOS))
    px, py = (W - lw) // 2 + 3 * s, 3 * s
    banner.alpha_composite(shadow(tilted, 2 * s, 0.5), (px + 3 * s, py + 4 * s))
    banner.alpha_composite(tilted, (px, py))
    return banner


def make_logo_banner(logo, w=256, h=128, margin=6):
    """The HOME Menu banner: the game's logo alone, on a transparent background, as big as fits."""
    logo = logo.convert("RGBA")
    box = logo.getbbox()
    if box:
        logo = logo.crop(box)
    k = min((w - 2 * margin) / logo.width, (h - 2 * margin) / logo.height)
    logo = logo.resize((max(1, round(logo.width * k)), max(1, round(logo.height * k))), Image.LANCZOS)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    out.alpha_composite(logo, ((w - logo.width) // 2, (h - logo.height) // 2))
    return out


def build_meta(sky, far, mid, logo):
    meshes = load_meshes()
    store = os.path.join(ROOT, "docs", "store")
    os.makedirs(store, exist_ok=True)
    make_icon(meshes, 48).save(os.path.join(ROOT, "meta", "icon.png"))
    make_icon(meshes, 192).save(os.path.join(store, "icon.png"))
    make_logo_banner(logo).save(os.path.join(ROOT, "meta", "banner.png"))  # the logo alone (RealAnxiousGit, 2026-11-13)
    make_banner(meshes, sky, far, mid, logo, 2).convert("RGB").save(os.path.join(store, "banner.png"), optimize=True)
