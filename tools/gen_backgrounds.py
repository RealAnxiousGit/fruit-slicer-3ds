"""Procedural backgrounds: top-screen scenes and bottom-screen slicing boards."""
import math
import random

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# The field a board covers, in game units, and how many pixels each unit gets.
BOARD_W, BOARD_H, BOARD_K = 320, 240, 1


def set_board_size(w, h, k):
    global BOARD_W, BOARD_H, BOARD_K
    BOARD_W, BOARD_H, BOARD_K = w, h, k


def board_grid():
    """Pixel rows and columns, in game units."""
    y, x = np.mgrid[0:BOARD_H * BOARD_K, 0:BOARD_W * BOARD_K].astype(np.float32)
    return y / BOARD_K, x / BOARD_K


def board_fractal(scale, seed, octaves=4, stretch=1.0):
    return fractal(BOARD_W * BOARD_K, BOARD_H * BOARD_K, scale * BOARD_K, seed, octaves, stretch)


def smooth_noise(w, h, scale_x, scale_y, seed):
    """Value noise: random grid resized with bicubic smoothing, range 0..1."""
    rng = np.random.default_rng(seed)
    grid = rng.random((max(2, int(h / scale_y)) + 2, max(2, int(w / scale_x)) + 2))
    img = Image.fromarray((grid * 255).astype(np.uint8), "L").resize((w, h), Image.BICUBIC)
    return np.asarray(img, np.float32) / 255.0


def fractal(w, h, scale, seed, octaves=4, stretch=1.0):
    total = np.zeros((h, w), np.float32)
    amp = 1.0
    norm = 0.0
    for o in range(octaves):
        total += smooth_noise(w, h, max(1, scale * stretch), max(1, scale), seed + o) * amp
        norm += amp
        amp *= 0.5
        scale /= 2
    return total / norm


def vignette(arr, strength=0.85, floor=0.35):
    y, x = np.mgrid[0:arr.shape[0], 0:arr.shape[1]]
    d = np.sqrt(((x - arr.shape[1] / 2) / (arr.shape[1] * 0.72)) ** 2 + ((y - arr.shape[0] / 2) / (arr.shape[0] * 0.72)) ** 2)
    return arr * np.clip(1.05 - d * d * strength, floor, 1.05)[..., None]


def finish(arr):
    rgb = np.clip(arr, 0, 255).astype(np.uint8)
    return Image.fromarray(np.dstack([rgb, np.full(rgb.shape[:2], 255, np.uint8)]), "RGBA")


def mix(a, b, t):
    return a[None, None, :] * (1 - t[..., None]) + b[None, None, :] * t[..., None]


def board_blanket():
    H, W = BOARD_H * BOARD_K, BOARD_W * BOARD_K
    y, x = np.mgrid[0:H, 0:W]
    size = 32 * BOARD_K
    a, b = (x // size) % 2, (y // size) % 2
    arr = np.zeros((H, W, 3), np.float32)
    arr[:] = np.array([38, 62, 92], np.float32)
    arr[(a == 1) ^ (b == 1)] = np.array([58, 92, 122], np.float32)
    arr[(a == 1) & (b == 1)] = np.array([84, 124, 142], np.float32)
    arr += (((x + y) % 3 == 0) * 5.0 - ((x - y) % 4 == 0) * 4.0)[..., None]
    arr += np.random.default_rng(11).normal(0, 1.8, (H, W, 1))
    return finish(vignette(arr))


def board_wood():
    """Kitchen cutting board: one slab with curved end grain, knife marks and a juice groove."""
    W, H, K = BOARD_W, BOARD_H, BOARD_K
    y, x = board_grid()
    warp = board_fractal(90, 41, 3, stretch=3.0)
    rings = np.sin((y + warp * 70) * 0.42 + board_fractal(40, 44, 3, stretch=4.0) * 5)
    t = np.clip(0.5 + rings * 0.38 + (board_fractal(8, 47, 2, stretch=9.0) - 0.5) * 0.7, 0, 1)
    arr = mix(np.array([118, 72, 40], np.float32), np.array([176, 120, 72], np.float32), t)
    img = finish(arr)
    marks = Image.new("RGBA", (W * K * 2, H * K * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(marks)
    rng = random.Random(5)
    for _ in range(46):
        px, py = rng.uniform(30, W * 2 - 30), rng.uniform(30, H * 2 - 30)
        a, ln = rng.uniform(-0.9, 0.9), rng.uniform(30, 120)
        d.line([px * K, py * K, (px + math.cos(a) * ln) * K, (py + math.sin(a) * ln) * K], fill=(60, 32, 16, 70), width=K)
        d.line([px * K, (py + 1.5) * K, (px + math.cos(a) * ln) * K, (py + 1.5 + math.sin(a) * ln) * K], fill=(235, 190, 140, 34), width=K)
    d.rounded_rectangle([20 * K, 20 * K, (W * 2 - 20) * K, (H * 2 - 20) * K], 34 * K, outline=(70, 38, 18, 170), width=6 * K)
    d.rounded_rectangle([24 * K, 25 * K, (W * 2 - 24) * K, (H * 2 - 18) * K], 30 * K, outline=(226, 170, 116, 60), width=2 * K)
    img.alpha_composite(marks.resize((W * K, H * K), Image.LANCZOS))
    return finish(vignette(np.asarray(img.convert("RGB"), np.float32), 0.55, 0.55))


def board_plate():
    """Ceramic plate on a soft linen tablecloth."""
    W, H = BOARD_W, BOARD_H
    y, x = board_grid()
    linen = 150 + 8 * np.sin(x * 2.1) + 8 * np.sin(y * 2.1) + (board_fractal(30, 61, 3) - 0.5) * 22
    arr = np.dstack([linen * 0.88, linen * 0.66, linen * 0.54]).astype(np.float32)
    r = np.sqrt((x - W / 2) ** 2 + (y - H / 2) ** 2) / 116.0
    shadow = np.clip(1 - np.sqrt((x - W / 2 - 2) ** 2 + (y - H / 2 - 7) ** 2) / 124.0, 0, 1)
    arr *= (1 - np.clip(shadow * 5, 0, 1) * 0.36)[..., None]
    edge = np.clip((1.04 - r) * 36, 0, 1)[..., None]
    plate = np.full(x.shape + (3,), 0, np.float32)
    plate[:] = np.array([242, 240, 232], np.float32)
    # Raised rim, then a softly shaded well.
    shade = 1.0 - 0.13 * np.exp(-(((r - 0.70) / 0.05) ** 2)) + 0.05 * np.exp(-(((r - 0.76) / 0.05) ** 2))
    shade -= 0.05 * np.clip(0.68 - r, 0, 1)
    plate *= shade[..., None]
    ring = np.exp(-(((r - 0.93) / 0.012) ** 2))[..., None]
    plate = plate * (1 - ring) + np.array([58, 110, 164], np.float32) * ring
    ring2 = np.exp(-(((r - 0.88) / 0.005) ** 2))[..., None]
    plate = plate * (1 - ring2) + np.array([58, 110, 164], np.float32) * ring2
    arr = arr * (1 - edge) + plate * edge
    return finish(vignette(arr, 0.35, 0.7))


def board_marble():
    y, x = board_grid()
    f = board_fractal(110, 71, 5)
    veins = np.abs(np.sin((x * 0.007 + y * 0.012 + f * 2.6) * math.pi))
    v = np.clip(1 - veins * 7, 0, 1) ** 1.5
    v = np.asarray(Image.fromarray((v * 255).astype(np.uint8), "L").filter(ImageFilter.GaussianBlur(0.8 * BOARD_K)), np.float32) / 255
    f2 = board_fractal(70, 77, 4)
    veins2 = np.abs(np.sin((x * 0.012 - y * 0.006 + f2 * 3.2) * math.pi))
    v2 = np.clip(1 - veins2 * 12, 0, 1) ** 1.5
    base = 214 + (f - 0.5) * 34
    arr = np.dstack([base, base * 1.0, base * 1.03]).astype(np.float32)
    arr = arr * (1 - v[..., None] * 0.55) + np.array([88, 92, 108], np.float32) * v[..., None] * 0.55
    arr = arr * (1 - v2[..., None] * 0.3) + np.array([190, 160, 110], np.float32) * v2[..., None] * 0.3
    return finish(vignette(arr, 0.5, 0.55))


def board_sand():
    """Sandy beach with ripple lines and a little foam at the edge."""
    y, x = board_grid()
    f = board_fractal(70, 81, 4)
    ripples = np.sin((y + f * 80) * 0.5) * 0.5 + 0.5
    arr = mix(np.array([206, 170, 116], np.float32), np.array([236, 210, 160], np.float32), ripples * 0.55 + f * 0.45)
    arr += np.random.default_rng(83).normal(0, 4.5, x.shape + (1,))
    shore = 28 + 12 * np.sin(x * 0.03) + 8 * np.sin(x * 0.09 + 1)
    depth = np.clip((shore - y) / 22, 0, 1)[..., None]
    arr = arr * (1 - depth) + (np.array([86, 176, 186], np.float32) * (1 - depth * 0.4)) * depth
    foam = np.exp(-(((y - shore) / 3.2) ** 2))[..., None]
    arr = arr * (1 - foam * 0.85) + np.array([250, 250, 244], np.float32) * foam * 0.85
    return finish(vignette(arr, 0.45, 0.6))


def board_starlight():
    """Deep night sky: nebula haze and stars, for a bright, high-contrast field."""
    W, H, K = BOARD_W, BOARD_H, BOARD_K
    f = board_fractal(120, 91, 5)
    g = board_fractal(80, 95, 4)
    arr = np.zeros((H * K, W * K, 3), np.float32)
    arr[:] = np.array([14, 14, 40], np.float32)
    arr += np.clip(f - 0.46, 0, 1)[..., None] * np.array([190, 70, 200], np.float32)
    arr += np.clip(g - 0.5, 0, 1)[..., None] * np.array([40, 130, 220], np.float32)
    img = finish(arr)
    d = ImageDraw.Draw(img)
    rng = random.Random(97)
    for _ in range(170 * W // 320):
        px, py, r = rng.uniform(0, W), rng.uniform(0, H), rng.choice((0.4, 0.5, 0.7, 1.0))
        d.ellipse([(px - r) * K, (py - r) * K, (px + r) * K, (py + r) * K], fill=(255, 250, 235, rng.randint(120, 255)))
    return img


def _glow(arr, cx, cy, radius, color, strength):
    """Adds a soft round light to a float board array; position and radius in game units."""
    K = BOARD_K
    h, w = arr.shape[:2]
    x0, x1 = max(0, int((cx - radius * 3) * K)), min(w, int((cx + radius * 3) * K) + 1)
    y0, y1 = max(0, int((cy - radius * 3) * K)), min(h, int((cy + radius * 3) * K) + 1)
    if x0 >= x1 or y0 >= y1:
        return
    yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32) / K
    g = np.exp(-((xx - cx) ** 2 + (yy - cy) ** 2) / (2 * radius * radius)) * strength
    arr[y0:y1, x0:x1] += g[..., None] * np.asarray(color, np.float32)


def board_night_picnic():
    """Gingham picnic blanket at night: moonlit folds and a few fireflies."""
    W, H = BOARD_W, BOARD_H
    y, x = board_grid()
    xs = x + (board_fractal(70, 101, 3) - 0.5) * 7
    ys = y + (board_fractal(70, 102, 3) - 0.5) * 7
    size = 22
    a = np.clip(0.5 + np.sin(xs * math.pi / size) * 5, 0, 1)
    b = np.clip(0.5 + np.sin(ys * math.pi / size) * 5, 0, 1)
    pale, band, cross = (np.array(c, np.float32) for c in ((64, 78, 138), (38, 46, 100), (18, 22, 60)))
    arr = (((1 - a) * (1 - b))[..., None] * pale + (a * (1 - b) + (1 - a) * b)[..., None] * band
           + (a * b)[..., None] * cross)
    # Over-under threads.
    over = ((np.floor(xs / 1.6) + np.floor(ys / 1.6)) % 2)[..., None]
    arr += (over * np.sin(xs * math.pi / 0.8)[..., None] + (1 - over) * np.sin(ys * math.pi / 0.8)[..., None]) * 3.5
    # Soft folds catch the moonlight from the top right.
    folds = board_fractal(85, 104, 3, stretch=2.2)
    moon = np.exp(-(((x - W * 0.86) / (W * 0.7)) ** 2 + ((y + H * 0.15) / (H * 0.85)) ** 2))
    arr *= (0.78 + (folds - 0.5) * 0.7)[..., None]
    arr += (moon * (0.45 + np.clip(folds - 0.45, 0, 1) * 1.6))[..., None] * np.array([58, 76, 118], np.float32)
    arr += np.random.default_rng(105).normal(0, 1.6, x.shape + (1,))
    arr = vignette(arr, 0.85, 0.35)
    rng = random.Random(107)
    placed = 0
    while placed < 11 * W // 320:
        cx, cy = rng.uniform(8, W - 8), rng.uniform(8, H - 8)
        if ((cx - W / 2) / (W * 0.34)) ** 2 + ((cy - H / 2) / (H * 0.34)) ** 2 < 1:
            continue
        placed += 1
        s = rng.uniform(0.7, 1.0)
        _glow(arr, cx, cy, 6 * s, (255, 190, 80), 0.13 * s)
        _glow(arr, cx, cy, 1.8 * s, (255, 214, 120), 0.45 * s)
        _glow(arr, cx, cy, 0.6, (255, 250, 210), 0.65 * s)
    return finish(arr)


def board_beach_towel():
    """Striped terry-cloth beach towel with a dusting of sand."""
    W, H, K = BOARD_W, BOARD_H, BOARD_K
    y, x = board_grid()
    coral, cream, sun, teal, navy = (np.array(c, np.float32) for c in
                                     ((206, 86, 74), (222, 206, 176), (226, 170, 58), (38, 140, 152), (38, 66, 128)))
    stripes = [(40, coral), (5, cream), (12, sun), (5, cream), (40, teal), (4, navy), (4, cream), (4, navy), (5, cream)]
    period = sum(w for w, _ in stripes)
    res = 10
    table = np.concatenate([np.repeat(c[None, :], w * res, 0) for w, c in stripes])
    # Soft dye edges: a circular blur of the stripe table.
    kern = np.exp(-np.linspace(-3, 3, 9 * res // 2) ** 2)
    kern /= kern.sum()
    pad = len(kern)
    wrapped = np.concatenate([table[-pad:], table, table[:pad]])
    table = np.stack([np.convolve(wrapped[:, i], kern, "same") for i in range(3)], 1)[pad:-pad]
    xs = x + (board_fractal(90, 201, 3) - 0.5) * 6 + (period - W / 2) % period + 20
    arr = table[((xs % period) * res).astype(np.int32) % len(table)].astype(np.float32)
    # Woven hems at the top and bottom, terry loops everywhere else: rows of small soft bumps.
    hem = np.clip(np.minimum(np.abs(y - 13), np.abs(y - (H - 13))) * -0.9 + 5.5, 0, 1)
    loops = board_fractal(1.0, 203, 2)
    loops = np.clip((loops - 0.5) * 2.6 + 0.5, 0, 1)
    rows = 0.5 + 0.5 * np.sin(y * math.pi / 1.1 + np.sin(x * 1.7) * 0.8)
    ribs = 0.5 + 0.5 * np.sin(y * math.pi / 0.75)
    tex = (1 - hem) * (0.8 + loops * 0.2 + rows * 0.08) + hem * (0.84 + ribs * 0.1)
    arr *= tex[..., None]
    arr *= (0.86 + (board_fractal(80, 205, 3, stretch=1.8) - 0.5) * 0.3)[..., None]
    # Sand: a light haze and loose grains, gathered toward two corners.
    sand = np.array([214, 184, 128], np.float32)
    clusters = [(W * 0.92, H * 0.88, 46), (W * 0.06, H * 0.1, 30)]
    haze = np.zeros(x.shape, np.float32)
    for cx, cy, r in clusters:
        haze += np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / (2 * r * r))
    haze = np.clip(haze * (0.4 + board_fractal(6, 209, 2) * 0.6), 0, 1) * 0.45
    arr = arr * (1 - haze[..., None]) + sand * haze[..., None]
    img = finish(vignette(arr, 0.65, 0.42))
    S = K * 2
    grains = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(grains)
    rng = random.Random(207)
    for i in range(420 * W // 320):
        if i % 12 == 0:
            px, py = rng.uniform(0, W), rng.uniform(0, H)
        else:
            cx, cy, r = clusters[i % 2]
            px, py = rng.gauss(cx, r * 0.8), rng.gauss(cy, r * 0.8)
        g, shade = rng.uniform(0.35, 0.8), rng.uniform(0.75, 1.1)
        d.ellipse([(px - g) * S, (py - g) * S, (px + g) * S, (py + g) * S],
                  fill=(int(min(255, 232 * shade)), int(min(255, 204 * shade)), int(min(255, 150 * shade)), rng.randint(150, 235)))
    img.alpha_composite(grains.resize((W * K, H * K), Image.LANCZOS))
    return img


def board_kitchen_counter():
    """Speckled terrazzo countertop with a tiled backsplash along the top and soft window glare."""
    W, H, K = BOARD_W, BOARD_H, BOARD_K
    y, x = board_grid()
    base = 142 + (board_fractal(70, 302, 4) - 0.5) * 24
    arr = np.dstack([base * 1.02, base * 0.98, base * 0.93]).astype(np.float32)
    arr += np.random.default_rng(301).normal(0, 2.2, x.shape + (1,))
    for scale, seed, cut, color, amt in ((1.6, 303, 0.7, (70, 66, 64), 0.6), (1.4, 305, 0.71, (220, 214, 204), 0.5),
                                         (2.0, 307, 0.73, (164, 110, 82), 0.5), (1.1, 309, 0.69, (120, 116, 112), 0.35)):
        m = np.clip((board_fractal(scale, seed, 2) - cut) * 14, 0, 1)[..., None] * amt
        arr = arr * (1 - m) + np.array(color, np.float32) * m
    img = finish(arr)
    # A few larger polished chips.
    S = K * 2
    chips = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(chips)
    rng = random.Random(311)
    tones = [(224, 218, 206), (92, 88, 86), (176, 116, 84), (142, 150, 132), (204, 190, 166)]
    for _ in range(46 * W // 320):
        cx, cy, r = rng.uniform(0, W), rng.uniform(26, H), rng.uniform(1.4, 3.4)
        n = rng.randint(4, 7)
        a0 = rng.uniform(0, math.tau)
        pts = [((cx + math.cos(a0 + i * math.tau / n) * r * rng.uniform(0.6, 1.1)) * S,
                (cy + math.sin(a0 + i * math.tau / n) * r * rng.uniform(0.6, 1.1)) * S) for i in range(n)]
        d.polygon(pts, fill=rng.choice(tones) + (rng.randint(190, 240),))
    img.alpha_composite(chips.resize((W * K, H * K), Image.LANCZOS))
    arr = np.asarray(img.convert("RGB"), np.float32)
    # Window glare: two slanted soft bands.
    u = x + y * 0.55
    glare = np.exp(-((u - W * 0.78) / 26) ** 2) * 0.9 + np.exp(-((u - W * 0.92) / 9) ** 2) * 0.6
    glare *= np.clip(1 - np.abs(y - H * 0.5) / (H * 0.62), 0, 1)
    arr += (glare * 30)[..., None]
    # Backsplash tiles and the shadow they cast on the counter.
    top, th, tw = 26, 12, 28
    yt = y + (th - top % th)
    row = np.floor(yt / th)
    tx = (x + (row % 2) * tw / 2) % tw
    ty = yt % th
    grout = np.clip(1.2 - np.minimum(np.minimum(tx, tw - tx), np.minimum(ty, th - ty)) * 1.4, 0, 1)
    gloss = 0.92 + 0.12 * np.exp(-((ty - 2.5) / 1.6) ** 2)
    tile = np.array([150, 166, 168], np.float32) * gloss[..., None]
    tile = tile * (1 - grout[..., None]) + np.array([92, 88, 84], np.float32) * grout[..., None]
    arr = np.where((y < top)[..., None], tile, arr)
    lip = np.exp(-((y - top) / 0.6) ** 2)[..., None]
    arr = arr * (1 - lip * 0.6) + np.array([210, 204, 196], np.float32) * lip * 0.6
    arr *= (1 - 0.38 * np.exp(-np.clip(y - top, 0, None) / 5) * (y >= top))[..., None]
    return finish(vignette(arr, 0.55, 0.5))


def board_snowfall():
    """Fresh snow: wind-shaped drifts, a trail of footprints, sparkle and a few falling flakes."""
    W, H, K = BOARD_W, BOARD_H, BOARD_K
    y, x = board_grid()
    h = board_fractal(120, 401, 4, stretch=2.2) * 12
    phase = (x * 0.012 + y * 0.03 + board_fractal(110, 403, 3, stretch=2.0) * 3.2) % 1.0
    # Wind ripples: a long gentle windward slope, then a short steep lee side.
    h += np.where(phase < 0.75, phase / 0.75, (1 - phase) / 0.25) ** 1.5 * 1.7
    # Footprints walking up the left side, and a short hopping trail at the right.
    prints = []
    for i in range(15):
        t = i / 14
        px = W * 0.08 + math.sin(t * 2.6) * W * 0.12
        py = H * 1.04 - t * H * 1.1
        ang = math.atan2(-H * 1.1, math.cos(t * 2.6) * 2.6 * W * 0.12)
        side = 1 if i % 2 else -1
        prints.append((px - math.sin(ang) * 3.4 * side, py + math.cos(ang) * 3.4 * side, ang, 2.0, 4.2, 3.0))
    for i in range(6):
        px, py = W * 0.8 + i * 9, H * 0.22 + i * 4.5
        for ox, oy in ((-1.6, 0), (1.6, 0), (0, 4.5)):
            prints.append((px + ox, py + oy, 0.45, 1.1, 1.6 if oy == 0 else 1.1, 1.6))
    for px, py, ang, rw, rl, depth in prints:
        ca, sa = math.cos(ang), math.sin(ang)
        x0, x1 = max(0, int((px - 9) * K)), min(x.shape[1], int((px + 9) * K) + 1)
        y0, y1 = max(0, int((py - 9) * K)), min(x.shape[0], int((py + 9) * K) + 1)
        if x0 >= x1 or y0 >= y1:
            continue
        dx, dy = x[y0:y1, x0:x1] - px, y[y0:y1, x0:x1] - py
        u, v = dx * ca + dy * sa, -dx * sa + dy * ca
        r = np.sqrt((u / rl) ** 2 + (v / rw) ** 2)
        h[y0:y1, x0:x1] += -depth * np.exp(-(r ** 4)) + depth * 0.25 * np.exp(-((r - 1.25) / 0.3) ** 2)
    gy, gx = np.gradient(h)
    gx, gy = gx * K, gy * K
    lx, ly, lz = -0.55, -0.6, 0.58
    shade = (-gx * lx - gy * ly + lz) / np.sqrt(gx * gx + gy * gy + 1)
    t = np.clip((shade - 0.25) * 1.25, 0, 1)
    arr = mix(np.array([92, 118, 172], np.float32), np.array([214, 226, 244], np.float32), t)
    arr += np.random.default_rng(405).normal(0, 1.5, x.shape + (1,))
    arr = vignette(arr, 0.7, 0.42) * np.array([0.94, 0.97, 1.0], np.float32)
    rng = random.Random(407)
    for _ in range(260 * W // 320):
        px, py = rng.uniform(0, W), rng.uniform(0, H)
        if t[min(int(py * K), x.shape[0] - 1), min(int(px * K), x.shape[1] - 1)] < 0.55:
            continue
        _glow(arr, px, py, rng.uniform(0.3, 0.55), (255, 255, 255), rng.uniform(0.25, 0.55))
    img = finish(arr)
    S = K * 2
    glints = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(glints)
    for _ in range(7 * W // 320):
        px, py, ln = rng.uniform(0, W), rng.uniform(0, H), rng.uniform(1.8, 3.2)
        for ddx, ddy in ((ln, 0), (0, ln)):
            d.line([(px - ddx) * S, (py - ddy) * S, (px + ddx) * S, (py + ddy) * S], fill=(255, 255, 255, 150), width=max(1, S // 2))
    flakes = Image.new("RGBA", (W * K, H * K), (0, 0, 0, 0))
    fd = ImageDraw.Draw(flakes)
    for _ in range(34 * W // 320):
        px, py, r = rng.uniform(0, W), rng.uniform(0, H), rng.uniform(0.8, 2.2)
        if ((px - W / 2) / (W * 0.3)) ** 2 + ((py - H / 2) / (H * 0.3)) ** 2 < 1:
            continue
        fd.ellipse([(px - r) * K, (py - r) * K, (px + r) * K, (py + r) * K], fill=(250, 252, 255, rng.randint(90, 170)))
    img.alpha_composite(glints.resize((W * K, H * K), Image.LANCZOS))
    img.alpha_composite(flakes.filter(ImageFilter.GaussianBlur(0.7 * K)))
    return img


def _maple_points(cx, cy, size, rot):
    pts = []
    for i in range(120):
        th = math.tau * i / 120 - math.pi
        s = th * 5 / math.tau
        lobe = 1 - 2 * abs(s - round(s))
        s3 = s * 3
        teeth = 1 - 2 * abs(s3 - round(s3))
        r = size * (0.42 + 0.58 * lobe ** 0.5 + 0.07 * teeth) * (1 + 0.14 * math.cos(th))
        r *= 1 - 0.35 * math.exp(-((abs(th) - math.pi) / 0.22) ** 2)
        pts.append((cx + r * math.cos(th + rot), cy + r * math.sin(th + rot)))
    return pts


def _oval_points(cx, cy, length, width, rot):
    ca, sa = math.cos(rot), math.sin(rot)
    upper, lower = [], []
    for i in range(25):
        t = i / 24
        u = (t - 0.5) * length
        v = width * 0.5 * math.sin(math.pi * t) ** 0.8 * (1 + 0.15 * math.sin(math.pi * t * 2 - 0.5))
        upper.append((cx + u * ca - v * sa, cy + u * sa + v * ca))
        lower.append((cx + u * ca + v * sa, cy + u * sa - v * ca))
    return upper + lower[::-1]


def board_autumn_leaves():
    """Plank table scattered with fallen leaves, thickest toward the edges."""
    W, H, K = BOARD_W, BOARD_H, BOARD_K
    y, x = board_grid()
    plank_h = 48
    yp = y + 10
    plank = np.floor(yp / plank_h)
    warp = board_fractal(70, 501, 3, stretch=4.0)
    grain = np.sin((yp + warp * 40 + plank * 17) * 0.55 + board_fractal(30, 503, 3, stretch=6.0) * 6)
    t = np.clip(0.5 + grain * 0.3 + (board_fractal(6, 505, 2, stretch=10.0) - 0.5) * 0.7, 0, 1)
    tone = 1 + ((plank * 0.37) % 1 - 0.5) * 0.18
    arr = mix(np.array([66, 40, 26], np.float32), np.array([120, 78, 48], np.float32), t) * tone[..., None]
    edge = np.abs(yp - (plank + 0.5) * plank_h) - plank_h / 2
    arr *= (1 - 0.7 * np.clip(edge + 1.4, 0, 1) - 0.18 * np.exp(-((edge + 3) / 1.6) ** 2))[..., None]
    img = finish(arr)
    S = K * 2
    shadow = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    leaves = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    sd, ld = ImageDraw.Draw(shadow), ImageDraw.Draw(leaves)
    rng = random.Random(507)
    palette = [(214, 104, 34), (186, 52, 34), (226, 168, 48), (138, 78, 36), (200, 76, 28), (160, 40, 40), (210, 140, 40)]
    placed = 0
    while placed < 40 * W // 320:
        cx, cy = rng.uniform(-8, W + 8), rng.uniform(-8, H + 8)
        if ((cx - W / 2) / (W * 0.38)) ** 2 + ((cy - H / 2) / (H * 0.38)) ** 2 < 1 and rng.random() < 0.92:
            continue
        placed += 1
        rot = rng.uniform(0, math.tau)
        jit = rng.uniform(0.9, 1.12)
        fill = tuple(int(min(255, c * jit)) for c in rng.choice(palette))
        vein = tuple(int(c * 0.7) for c in fill)
        if rng.random() < 0.5:
            size = rng.uniform(8, 16)
            pts = _maple_points(cx, cy, size, rot)
            ends = [(cx + size * 0.85 * math.cos(rot + a), cy + size * 0.85 * math.sin(rot + a))
                    for a in (0, 1.257, -1.257, 2.513, -2.513)]
            ends.append((cx - size * 0.75 * math.cos(rot), cy - size * 0.75 * math.sin(rot)))
            veins = [((cx, cy), p) for p in ends]
        else:
            ln = rng.uniform(12, 22)
            pts = _oval_points(cx, cy, ln, ln * rng.uniform(0.38, 0.5), rot)
            ca, sa = math.cos(rot), math.sin(rot)
            veins = [((cx - ca * ln * 0.72, cy - sa * ln * 0.72), (cx + ca * ln * 0.5, cy + sa * ln * 0.5))]
            for k in range(1, 5):
                u = -ln * 0.35 + k * ln * 0.15
                bx, by = cx + ca * u, cy + sa * u
                for s in (1, -1):
                    veins.append(((bx, by), (bx + ca * ln * 0.12 - s * sa * ln * 0.17, by + sa * ln * 0.12 + s * ca * ln * 0.17)))
        lift = rng.uniform(1.5, 3.0)
        sd.polygon([((px + lift) * S, (py + lift * 1.3) * S) for px, py in pts], fill=(0, 0, 0, 120))
        ld.polygon([(px * S, py * S) for px, py in pts], fill=fill + (255,))
        for (ax, ay), (bx, by) in veins:
            ld.line([ax * S, ay * S, bx * S, by * S], fill=vein + (200,), width=max(1, S // 2))
    # Mottle the leaves so each one has dry spots and colour shifts.
    lv = np.asarray(leaves, np.float32).copy()
    sh, sw = lv.shape[:2]
    lv[..., :3] *= (0.8 + fractal(sw, sh, 10 * S, 509, 3) * 0.4)[..., None]
    lv[..., 1] *= 0.9 + fractal(sw, sh, 24 * S, 511, 2) * 0.2
    leaves = Image.fromarray(np.clip(lv, 0, 255).astype(np.uint8), "RGBA")
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(2.2 * S)).resize((W * K, H * K), Image.LANCZOS))
    img.alpha_composite(leaves.resize((W * K, H * K), Image.LANCZOS))
    return finish(vignette(np.asarray(img.convert("RGB"), np.float32), 0.6, 0.45))


def bg_boards():
    return [board_blanket(), board_wood(), board_plate(), board_marble(), board_sand(), board_starlight(),
            board_night_picnic(), board_beach_towel(), board_kitchen_counter(), board_snowfall(), board_autumn_leaves()]


def add_stars(img, rng, n, limit):
    d = ImageDraw.Draw(img)
    for _ in range(n):
        x, y = rng.uniform(0, img.width), rng.uniform(0, limit)
        r = rng.choice((0.5, 0.8, 1.1))
        d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 244, 220, 220))


def add_glow(img, spr_soft, cx, cy, size, disc, disc_color, glow_color, power=1.3):
    glow = np.array(spr_soft(size, power))
    glow[..., 0], glow[..., 1], glow[..., 2] = glow_color
    img.alpha_composite(Image.fromarray(glow, "RGBA"), (int(cx - size / 2), int(cy - size / 2)))
    ImageDraw.Draw(img).ellipse([cx - disc, cy - disc, cx + disc, cy + disc], fill=disc_color)


def bg_top(scene, hill_layer, v_gradient, spr_soft):
    W, H = 424, 240
    rng = random.Random(21 + scene)
    if scene == 0:  # Dusk Orchard
        sky = v_gradient(W, H, [(0, (44, 34, 92)), (0.42, (150, 66, 128)), (0.72, (246, 136, 96)), (1, (255, 206, 130))])
        add_stars(sky, rng, 46, 96)
        add_glow(sky, spr_soft, W // 2 + 30, 140, 190, 30, (255, 238, 186, 255), (255, 226, 150))
        far = hill_layer(W, 120, 42, 16, 0.016, (120, 62, 124, 255), 3)
        mid = hill_layer(W, 110, 42, 14, 0.021, (76, 46, 104, 255), 5, trees=11)
        near = hill_layer(W, 70, 30, 9, 0.028, (42, 30, 74, 255), 8, trees=5)
    elif scene == 1:  # Morning Meadow
        sky = v_gradient(W, H, [(0, (96, 170, 232)), (0.55, (176, 222, 244)), (1, (252, 240, 206))])
        add_glow(sky, spr_soft, 96, 66, 170, 22, (255, 252, 226, 255), (255, 246, 196))
        d = ImageDraw.Draw(sky)
        cloud = Image.new("RGBA", sky.size, (255, 255, 255, 0))
        cd = ImageDraw.Draw(cloud)
        for _ in range(7):
            cx, cy = rng.uniform(0, W), rng.uniform(16, 96)
            for _ in range(7):
                ox, oy, r = rng.uniform(-30, 30), rng.uniform(-5, 6), rng.uniform(9, 17)
                cd.ellipse([cx + ox - r * 1.3, cy + oy - r * 0.7, cx + ox + r * 1.3, cy + oy + r * 0.7], fill=(255, 255, 255, 210))
        sky.alpha_composite(cloud.filter(ImageFilter.GaussianBlur(1.6)))
        del d
        far = hill_layer(W, 120, 44, 15, 0.017, (138, 196, 150, 255), 13)
        mid = hill_layer(W, 110, 44, 13, 0.022, (102, 174, 96, 255), 15, trees=9, tree_color=(62, 136, 84, 255), dots=(232, 70, 70, 255))
        near = hill_layer(W, 70, 30, 8, 0.03, (64, 138, 72, 255), 18, trees=4, tree_color=(38, 104, 62, 255), dots=(252, 186, 60, 255))
    else:  # Moonlit Bay
        sky = v_gradient(W, H, [(0, (10, 14, 44)), (0.55, (24, 42, 92)), (1, (66, 92, 148))])
        add_stars(sky, rng, 110, 130)
        add_glow(sky, spr_soft, W - 110, 62, 150, 24, (240, 240, 218, 255), (190, 206, 255), 1.7)
        d = ImageDraw.Draw(sky)
        for cx, cy, r in ((W - 118, 56, 4), (W - 102, 68, 3), (W - 108, 52, 2)):
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(208, 208, 192, 255))
        far = hill_layer(W, 120, 40, 12, 0.019, (34, 48, 96, 255), 23)
        # The middle layer is water with the moon's reflection.
        mid = Image.new("RGBA", (W, 110), (0, 0, 0, 0))
        md = ImageDraw.Draw(mid)
        md.rectangle([0, 30, W, 110], fill=(20, 38, 84, 255))
        for i in range(34):
            y = 32 + i * 2.2
            half = 8 + i * 0.9 + rng.uniform(-3, 3)
            x = W - 110 + rng.uniform(-4, 4)
            md.line([x - half, y, x + half, y], fill=(190, 206, 240, max(30, 190 - i * 5)), width=1)
        for _ in range(60):
            x, y = rng.uniform(0, W), rng.uniform(34, 108)
            md.line([x - rng.uniform(4, 16), y, x + rng.uniform(4, 16), y], fill=(70, 100, 160, 110), width=1)
        near = hill_layer(W, 70, 34, 10, 0.03, (10, 16, 40, 255), 28, trees=6, tree_color=(8, 12, 32, 255), dots=(250, 214, 110, 255))
    return sky, far, mid, near
