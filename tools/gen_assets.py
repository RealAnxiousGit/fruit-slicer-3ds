#!/usr/bin/env python3
"""Generates every Fruit Slicer 3DS asset from code: no third-party art, fonts or samples.

Outputs (all committed, so building the game only needs devkitPro):
  gfx/*.t3s, gfx/png/**   sprite sheets for tex3ds
  romfs/audio/*.pcm       FSA1 mono PCM16 sound effects and music
  meta/icon.png, meta/banner.png, meta/banner.wav
  source/font_metrics.h   glyph lookup tables

Requires Pillow and numpy. Output is deterministic.
"""
import math
import os
import random
import shutil
import sys

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SS = 4  # supersampling factor for all vector drawing
CREAM = (255, 246, 226, 255)
INK = (38, 28, 54, 255)


def path(*parts):
    p = os.path.join(ROOT, *parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


# ---------------------------------------------------------------- image helpers
def canvas(w, h=None):
    h = h or w
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def shrink(img):
    return img.resize((img.width // SS, img.height // SS), Image.LANCZOS)


def mask_of(img):
    return img.getchannel("A")


def recolor(mask, color):
    out = Image.new("RGBA", mask.size, color[:3] + (255,))
    a = color[3] if len(color) > 3 else 255
    out.putalpha(mask.point(lambda v: v * a // 255))
    return out


def dilate(mask, radius):
    """Round dilation via blur + threshold; cheap at supersampled sizes."""
    blurred = mask.filter(ImageFilter.GaussianBlur(radius * 0.72))
    return blurred.point(lambda v: 255 if v > 12 else 0)


def sticker(img, border=2.2, shadow=True):
    """Die-cut sticker treatment: cream border and a soft ink drop shadow."""
    m = mask_of(img)
    edge = dilate(m, border * SS)
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    if shadow:
        sh = ImageChops.offset(edge, 0, int(1.4 * SS)).filter(ImageFilter.GaussianBlur(SS))
        out.alpha_composite(recolor(sh, INK[:3] + (120,)))
    out.alpha_composite(recolor(edge, CREAM))
    out.alpha_composite(img)
    return out


def shade(img, light=(255, 255, 255, 90), dark=(30, 10, 50, 70), size=None):
    """Adds a crescent shadow and a soft glossy highlight clipped to the silhouette."""
    m = mask_of(img)
    w, h = img.size
    box = m.getbbox() or (0, 0, w, h)
    bw, bh = box[2] - box[0], box[3] - box[1]
    moved = ImageChops.offset(m, -int(bw * 0.12), -int(bh * 0.12))
    crescent = ImageChops.subtract(m, moved).filter(ImageFilter.GaussianBlur(SS * 1.5))
    crescent = ImageChops.multiply(crescent, m)
    img = img.copy()
    img.alpha_composite(recolor(crescent, dark))
    gloss = Image.new("L", img.size, 0)
    d = ImageDraw.Draw(gloss)
    d.ellipse((box[0] + bw * 0.18, box[1] + bh * 0.14, box[0] + bw * 0.46, box[1] + bh * 0.34), fill=255)
    gloss = ImageChops.multiply(gloss.filter(ImageFilter.GaussianBlur(SS * 1.2)), m)
    img.alpha_composite(recolor(gloss, light))
    return img


# ---------------------------------------------------------------- fruit
CELL = 64


def el(d, cx, cy, rx, ry, fill):
    d.ellipse([(cx - rx) * SS, (cy - ry) * SS, (cx + rx) * SS, (cy + ry) * SS], fill=fill)


def poly(d, pts, fill):
    d.polygon([(x * SS, y * SS) for x, y in pts], fill=fill)


def leaf(d, x, y, angle, length, fill):
    a = math.radians(angle)
    tip = (x + math.cos(a) * length, y + math.sin(a) * length)
    n = (-math.sin(a) * length * 0.32, math.cos(a) * length * 0.32)
    mid = (x + math.cos(a) * length * 0.5, y + math.sin(a) * length * 0.5)
    poly(d, [(x, y), (mid[0] + n[0], mid[1] + n[1]), tip, (mid[0] - n[0], mid[1] - n[1])], fill)


def stem(d, x0, y0, x1, y1, w=3, fill=(102, 66, 44, 255)):
    d.line([x0 * SS, y0 * SS, x1 * SS, y1 * SS], fill=fill, width=w * SS)


def f_apple(d):
    el(d, 24, 35, 15, 18, (222, 48, 62, 255))
    el(d, 40, 35, 15, 18, (222, 48, 62, 255))
    el(d, 32, 40, 19, 15, (222, 48, 62, 255))


def f_apple_top(d):
    el(d, 32, 20, 6, 2.5, (150, 24, 44, 255))
    stem(d, 32, 21, 35, 11)
    leaf(d, 35, 14, -25, 14, (86, 170, 72, 255))


def f_orange(d):
    el(d, 32, 33, 21, 21, (247, 146, 38, 255))


def f_orange_top(d):
    rng = random.Random(4)
    for _ in range(26):
        a, r = rng.uniform(0, 6.28), rng.uniform(4, 18)
        el(d, 32 + math.cos(a) * r, 33 + math.sin(a) * r, 0.7, 0.7, (214, 112, 22, 255))
    leaf(d, 32, 13, -35, 11, (86, 170, 72, 255))


def f_lemon(d):
    el(d, 32, 33, 23, 17, (250, 220, 62, 255))
    el(d, 9, 33, 5, 5, (250, 220, 62, 255))
    el(d, 55, 33, 5, 5, (250, 220, 62, 255))


def f_lemon_top(d):
    rng = random.Random(7)
    for _ in range(16):
        a, r = rng.uniform(0, 6.28), rng.uniform(4, 13)
        el(d, 32 + math.cos(a) * r * 1.3, 33 + math.sin(a) * r, 0.6, 0.6, (218, 180, 34, 255))


def f_pear(d):
    el(d, 32, 40, 18, 17, (172, 204, 70, 255))
    el(d, 32, 22, 10, 13, (172, 204, 70, 255))
    poly(d, [(22, 24), (42, 24), (49, 36), (15, 36)], (172, 204, 70, 255))


def f_pear_top(d):
    stem(d, 32, 11, 35, 5)
    leaf(d, 34, 8, 10, 12, (86, 170, 72, 255))


def f_plum(d):
    el(d, 32, 33, 20, 22, (122, 58, 164, 255))


def f_plum_top(d):
    d.arc([18 * SS, 11 * SS, 42 * SS, 55 * SS], -80, 80, fill=(88, 36, 128, 255), width=int(1.6 * SS))
    stem(d, 32, 13, 30, 6, 2)


def f_melon(d):
    el(d, 32, 32, 25, 25, (58, 148, 78, 255))


def f_melon_top(d):
    for i, x in enumerate((-16, -6, 5, 15)):
        pts = []
        for t in range(-10, 11):
            y = t * 2.4
            pts.append(((32 + x * math.sqrt(max(0, 1 - (y / 25) ** 2)) + math.sin(t * 1.4 + i) * 1.6) * SS, (32 + y) * SS))
        d.line(pts, fill=(28, 96, 52, 255), width=int(3 * SS), joint="curve")


def f_kiwi(d):
    el(d, 32, 32, 21, 18, (134, 96, 56, 255))


def f_kiwi_top(d):
    rng = random.Random(9)
    for _ in range(70):
        a, r = rng.uniform(0, 6.28), rng.uniform(0, 19)
        x, y = 32 + math.cos(a) * r, 32 + math.sin(a) * r * 0.86
        d.line([x * SS, y * SS, (x + 1) * SS, (y + 1.6) * SS], fill=(98, 66, 36, 255), width=SS // 2 + 1)


def f_berry(d):
    poly(d, [(12, 22), (52, 22), (47, 40), (32, 58), (17, 40)], (232, 44, 84, 255))
    el(d, 22, 26, 12, 11, (232, 44, 84, 255))
    el(d, 42, 26, 12, 11, (232, 44, 84, 255))
    el(d, 32, 44, 11, 13, (232, 44, 84, 255))


def f_berry_top(d):
    for row, (y, n, spread) in enumerate(((27, 5, 34), (35, 4, 26), (43, 3, 16), (50, 1, 0))):
        for i in range(n):
            x = 32 if n == 1 else 32 - spread / 2 + spread * i / (n - 1)
            el(d, x, y, 1, 1.5, (255, 222, 122, 255))
    for a in (-160, -125, -90, -55, -20):
        leaf(d, 32, 17, a + 180 if False else a, 12, (66, 156, 70, 255))


def f_gold(d):
    pts = []
    for i in range(10):
        r = 25 if i % 2 == 0 else 13
        a = -math.pi / 2 + i * math.pi / 5
        pts.append((32 + math.cos(a) * r, 33 + math.sin(a) * r))
    poly(d, pts, (250, 196, 40, 255))
    el(d, 32, 33, 14, 14, (250, 196, 40, 255))


def f_gold_top(d):
    for i in range(5):
        a = -math.pi / 2 + i * 2 * math.pi / 5
        d.line([32 * SS, 33 * SS, (32 + math.cos(a) * 19) * SS, (33 + math.sin(a) * 19) * SS],
               fill=(255, 238, 150, 255), width=int(1.5 * SS))


def f_peach(d):
    el(d, 32, 34, 21, 20, (250, 164, 110, 255))
    el(d, 24, 30, 12, 13, (240, 96, 92, 200))


def f_peach_top(d):
    d.arc([22 * SS, 12 * SS, 44 * SS, 56 * SS], 100, 260, fill=(214, 92, 70, 255), width=int(1.6 * SS))
    stem(d, 32, 15, 33, 8, 2)
    leaf(d, 33, 10, -15, 12, (86, 170, 72, 255))


def f_pineapple(d):
    el(d, 32, 39, 16, 20, (240, 178, 50, 255))


def f_pineapple_top(d):
    # Diamond lattice, sampled so that it stays inside the body.
    for i in range(-4, 6):
        for sign in (-1, 1):
            for t in range(61):
                y = 19 + t * 40 / 60
                x = 32 + sign * (i * 8 - 16 + (y - 21) * 0.42)
                if ((x - 32) / 15.2) ** 2 + ((y - 39) / 19.2) ** 2 < 1:
                    el(d, x, y, 0.55, 0.55, (190, 122, 30, 255))
    for a in (-135, -112, -90, -68, -45):
        leaf(d, 32, 21, a, 19, (66, 156, 70, 255))


def f_coconut(d):
    el(d, 32, 33, 22, 21, (92, 62, 42, 255))


def f_coconut_top(d):
    rng = random.Random(14)
    for _ in range(90):
        a, r = rng.uniform(0, 6.28), rng.uniform(0, 20)
        x, y = 32 + math.cos(a) * r, 33 + math.sin(a) * r
        d.line([x * SS, y * SS, (x + 2) * SS, (y + 2.4) * SS], fill=(62, 40, 28, 255), width=SS // 2 + 1)
    for x, y in ((25, 21), (32, 18), (39, 21)):
        el(d, x, y, 2, 2, (40, 26, 20, 255))


def spr_sunburst():
    """The finale fruit: an original, oversized star-ridged fruit with a leafy crown."""
    size = 104
    cx, cy = size / 2, size / 2 + 4
    img = canvas(size)
    d = ImageDraw.Draw(img)
    for a in (-150, -125, -100, -80, -55, -30):
        leaf(d, cx, cy - 34, a, 20, (72, 170, 92, 255))

    def star(r0, wave, fill):
        pts = []
        for i in range(240):
            th = i / 240 * math.tau
            r = r0 + wave * math.cos(10 * th)
            pts.append(((cx + math.cos(th) * r) * SS, (cy + math.sin(th) * r) * SS))
        d.polygon(pts, fill=fill)

    star(41, 4.5, (214, 62, 88, 255))
    star(37, 4.0, (250, 104, 70, 255))
    star(30, 3.2, (255, 160, 62, 255))
    star(21, 2.4, (255, 208, 86, 255))
    star(11, 1.4, (255, 244, 170, 255))
    for i in range(10):
        th = i / 10 * math.tau
        d.line([(cx + math.cos(th) * 12) * SS, (cy + math.sin(th) * 12) * SS, (cx + math.cos(th) * 42) * SS, (cy + math.sin(th) * 42) * SS],
               fill=(150, 40, 70, 170), width=SS)
        th2 = th + math.pi / 10
        el(d, cx + math.cos(th2) * 24, cy + math.sin(th2) * 24, 2.2, 2.2, (120, 36, 60, 255))
    el(d, cx - 16, cy - 20, 8, 5, (255, 244, 214, 150))
    return shrink(sticker(img, 2.4))


# name, body, details, flesh colour, rim colour, seed colour, half-width of cut face
FRUITS = [
    ("apple", f_apple, f_apple_top, (255, 240, 190, 255), (246, 216, 150, 255), (92, 54, 34, 255), 9),
    ("orange", f_orange, f_orange_top, (255, 176, 66, 255), (255, 232, 180, 255), (255, 212, 130, 255), 10),
    ("lemon", f_lemon, f_lemon_top, (252, 240, 130, 255), (255, 250, 214, 255), (240, 222, 96, 255), 8),
    ("pear", f_pear, f_pear_top, (248, 246, 196, 255), (226, 232, 150, 255), (98, 70, 42, 255), 8),
    ("plum", f_plum, f_plum_top, (240, 190, 90, 255), (214, 110, 110, 255), (146, 86, 48, 255), 9),
    ("melon", f_melon, f_melon_top, (238, 66, 82, 255), (244, 244, 190, 255), (40, 26, 30, 255), 12),
    ("kiwi", f_kiwi, f_kiwi_top, (150, 208, 80, 255), (214, 232, 170, 255), (34, 30, 26, 255), 9),
    ("berry", f_berry, f_berry_top, (252, 120, 140, 255), (255, 196, 200, 255), (255, 230, 190, 255), 8),
    ("peach", f_peach, f_peach_top, (255, 214, 130, 255), (250, 150, 110, 255), (150, 70, 50, 255), 10),
    ("pineapple", f_pineapple, f_pineapple_top, (255, 232, 110, 255), (255, 246, 190, 255), (232, 190, 60, 255), 9),
    ("coconut", f_coconut, f_coconut_top, (250, 250, 244, 255), (255, 255, 255, 255), (226, 226, 214, 255), 11),
    ("gold", f_gold, f_gold_top, (255, 236, 120, 255), (255, 250, 200, 255), (214, 150, 20, 255), 8),
]


def fruit_images(spec):
    name, body, top, flesh, rim, seed, cut_w = spec
    base = canvas(CELL)
    body(ImageDraw.Draw(base))
    silhouette = mask_of(base)
    whole = shade(base)
    top(ImageDraw.Draw(whole))

    # Cut sides: the skin on one half plus a slice of flesh along the blade line.
    face = Image.new("RGBA", base.size, (0, 0, 0, 0))
    fd = ImageDraw.Draw(face)
    bb = silhouette.getbbox()
    ch = (bb[3] - bb[1]) / SS
    cy = (bb[1] + bb[3]) / 2 / SS
    el(fd, 32, cy, cut_w + 1.6, ch / 2 - 0.8, rim)
    el(fd, 32, cy, cut_w, ch / 2 - 3.0, flesh)
    rng = random.Random(len(name) * 31)
    for _ in range(9):
        y = cy + rng.uniform(-ch / 2 + 7, ch / 2 - 7)
        x = rng.choice((-1, 1)) * rng.uniform(2, max(3, cut_w - 3)) + 32
        el(fd, x, y, 1.0, 1.7, seed)

    halves = []
    for side in (0, 1):
        keep = Image.new("L", base.size, 0)
        kd = ImageDraw.Draw(keep)
        if side == 0:
            kd.rectangle([0, 0, 32 * SS - 1, 64 * SS], fill=255)
        else:
            kd.rectangle([32 * SS, 0, 64 * SS, 64 * SS], fill=255)
        h = whole.copy()
        h.alpha_composite(face)
        h.putalpha(ImageChops.multiply(ImageChops.multiply(silhouette, keep), Image.new("L", base.size, 255)))
        halves.append(shrink(sticker(h, 1.6)))
    return shrink(sticker(whole)), halves[0], halves[1]


# ---------------------------------------------------------------- other sprites
def spr_bomb():
    img = canvas(CELL)
    d = ImageDraw.Draw(img)
    # Naval-mine silhouette with stubby horns and a warning light.
    for i in range(8):
        a = i * math.pi / 4 + math.pi / 8
        x, y = 32 + math.cos(a) * 24, 31 + math.sin(a) * 24
        d.line([32 * SS, 31 * SS, x * SS, y * SS], fill=(60, 54, 84, 255), width=int(6 * SS))
        el(d, x, y, 3.6, 3.6, (214, 62, 52, 255))
    el(d, 32, 31, 20, 20, (46, 42, 70, 255))
    img = shade(img, light=(180, 190, 255, 90), dark=(0, 0, 0, 80))
    d = ImageDraw.Draw(img)
    # Hazard band
    band = Image.new("RGBA", img.size, (0, 0, 0, 0))
    bd = ImageDraw.Draw(band)
    for i in range(-3, 4):
        x = 32 + i * 9
        col = (250, 202, 44, 255) if i % 2 == 0 else (30, 28, 44, 255)
        bd.rectangle([(x - 4.5) * SS, 27 * SS, (x + 4.5) * SS, 35 * SS], fill=col)
    disc = Image.new("L", img.size, 0)
    el(ImageDraw.Draw(disc), 32, 31, 20, 20, 255)
    band.putalpha(ImageChops.multiply(mask_of(band), disc))
    img.alpha_composite(band)
    return shrink(sticker(img, 1.8))


def spr_power(kind):
    img = canvas(CELL)
    d = ImageDraw.Draw(img)
    base = {"freeze": (90, 196, 244, 255), "double": (255, 110, 170, 255), "frenzy": (255, 158, 40, 255)}[kind]
    el(d, 32, 32, 24, 24, base)
    img = shade(img)
    d = ImageDraw.Draw(img)
    white = (255, 255, 255, 255)
    if kind == "freeze":
        for i in range(3):
            a = i * math.pi / 3
            dx, dy = math.cos(a) * 15, math.sin(a) * 15
            d.line([(32 - dx) * SS, (32 - dy) * SS, (32 + dx) * SS, (32 + dy) * SS], fill=white, width=int(3 * SS))
            for s in (-1, 1):
                ex, ey = 32 + s * dx * 0.68, 32 + s * dy * 0.68
                for t in (-0.9, 0.9):
                    b = a + (0 if s > 0 else math.pi) + t
                    d.line([ex * SS, ey * SS, (ex + math.cos(b) * 6) * SS, (ey + math.sin(b) * 6) * SS], fill=white, width=int(2 * SS))
    elif kind == "double":
        stroke_text(d, "X2", 32, 32, 9.5, white, 3.4)
    else:
        for i in range(8):
            a = i * math.pi / 4
            r0, r1 = (6, 19) if i % 2 == 0 else (6, 13)
            d.line([(32 + math.cos(a) * r0) * SS, (32 + math.sin(a) * r0) * SS,
                    (32 + math.cos(a) * r1) * SS, (32 + math.sin(a) * r1) * SS], fill=white, width=int(3.4 * SS))
    return shrink(sticker(img, 1.8))


def spr_splat(seed):
    rng = random.Random(seed)
    img = canvas(CELL)
    d = ImageDraw.Draw(img)
    el(d, 32, 32, 13, 13, (255, 255, 255, 255))
    for _ in range(16):
        a, r = rng.uniform(0, 6.28), rng.uniform(10, 26)
        s = rng.uniform(1.5, 5.5)
        x, y = 32 + math.cos(a) * r, 32 + math.sin(a) * r
        el(d, x, y, s, s, (255, 255, 255, 255))
        d.line([32 * SS, 32 * SS, x * SS, y * SS], fill=(255, 255, 255, 255), width=int(s * SS))
    return shrink(img).filter(ImageFilter.GaussianBlur(0.3))


def spr_soft(size, power=1.6):
    y, x = np.mgrid[0:size, 0:size]
    c = (size - 1) / 2
    r = np.clip(np.sqrt((x - c) ** 2 + (y - c) ** 2) / (size / 2), 0, 1)
    a = ((1 - r) ** power * 255).astype(np.uint8)
    rgba = np.dstack([np.full_like(a, 255)] * 3 + [a])
    return Image.fromarray(rgba, "RGBA")


def spr_drop():
    img = canvas(16)
    el(ImageDraw.Draw(img), 8, 8, 6.6, 6.6, (255, 255, 255, 255))
    return shrink(img)


def spr_spark():
    img = canvas(24)
    d = ImageDraw.Draw(img)
    pts = []
    for i in range(8):
        r = 11 if i % 2 == 0 else 2.6
        a = i * math.pi / 4
        pts.append((12 + math.cos(a) * r, 12 + math.sin(a) * r))
    poly(d, pts, (255, 255, 255, 255))
    return shrink(img)


def spr_pip(active):
    img = canvas(32)
    d = ImageDraw.Draw(img)
    col = (236, 62, 74, 255) if active else (100, 92, 124, 170)
    d.line([7 * SS, 7 * SS, 25 * SS, 25 * SS], fill=col, width=int(6 * SS))
    d.line([25 * SS, 7 * SS, 7 * SS, 25 * SS], fill=col, width=int(6 * SS))
    for x, y in ((7, 7), (25, 25), (25, 7), (7, 25)):
        el(d, x, y, 3, 3, col)
    return shrink(sticker(img, 1.4, shadow=False)) if active else shrink(img)


def spr_button():
    w, h = 160, 48
    img = canvas(w, h)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([2 * SS, 2 * SS, (w - 2) * SS, (h - 3) * SS], 13 * SS, fill=(255, 255, 255, 255))
    arr = np.array(img).astype(np.float32)
    grad = np.linspace(1.0, 0.76, arr.shape[0])[:, None]
    arr[..., :3] *= grad[..., None]
    img = Image.fromarray(arr.astype(np.uint8), "RGBA")
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([5 * SS, 5 * SS, (w - 5) * SS, 22 * SS], 9 * SS, fill=(255, 255, 255, 46))
    return shrink(sticker(img, 1.4))


def spr_panel():
    img = canvas(128)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([1 * SS, 1 * SS, 127 * SS, 127 * SS], 14 * SS, fill=(255, 255, 255, 255))
    return shrink(img)


# ---------------------------------------------------------------- orchard signs
def spr_plank():
    """A hand-cut sign in pale neutral grain. The game tints it to suit the chosen board."""
    w, h = 256, 56
    yy, xx = np.mgrid[0:h * SS, 0:w * SS]
    y, x = yy / SS, xx / SS
    grain = np.sin(y * 0.85 + np.sin(x * 0.028) * 2.4) * 0.5 + np.sin(y * 2.3 + np.sin(x * 0.05 + 1) * 3) * 0.28
    light = 1 + grain * 0.1 + 0.16 * (1 - y / h) - 0.12 * (y / h)
    arr = np.zeros((h * SS, w * SS, 4), np.uint8)
    for c in range(3):
        arr[..., c] = np.clip(214 * light, 0, 255)
    arr[..., 3] = 255
    img = Image.fromarray(arr, "RGBA")
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([3 * SS, 3 * SS, (w - 3) * SS, (h - 4) * SS], 7 * SS, fill=255)
    img.putalpha(mask)
    d = ImageDraw.Draw(img)
    d.line([8 * SS, 6 * SS, (w - 8) * SS, 6 * SS], fill=(255, 255, 255, 110), width=SS)  # worn top edge
    for x0 in (10, w - 10):
        for y0 in (12, h - 13):
            el(d, x0, y0, 2.0, 2.0, (58, 40, 30, 255))
            el(d, x0 - 0.4, y0 - 0.5, 0.8, 0.8, (214, 196, 170, 255))
    return shrink(sticker(img, 1.2))


def spr_plank_focus():
    """White outline of the plank, tinted with the accent to show which sign has focus."""
    w, h = 256, 56
    img = canvas(w, h)
    ImageDraw.Draw(img).rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], 9 * SS, outline=(255, 255, 255, 255), width=3 * SS)
    return shrink(img)


def spr_knob():
    img = canvas(32)
    d = ImageDraw.Draw(img)
    el(d, 16, 16, 12, 12, (150, 98, 54, 255))
    el(d, 16, 15, 9, 9, (186, 130, 78, 255))
    el(d, 16, 15, 3, 3, (110, 70, 40, 255))
    return shrink(sticker(img, 1.4))


def spr_arrow(back=False):
    """A chunky painted arrow. `back` adds the stem used by every back button."""
    img = canvas(32)
    d = ImageDraw.Draw(img)
    white = (255, 255, 255, 255)
    tip = 6 if back else 9
    d.line([(tip + 10) * SS, 6 * SS, tip * SS, 16 * SS, (tip + 10) * SS, 26 * SS], fill=white, width=5 * SS, joint="curve")
    for p in ((tip + 10, 6), (tip, 16), (tip + 10, 26)):
        el(d, p[0], p[1], 2.5, 2.5, white)
    if back:
        d.line([tip * SS, 16 * SS, 27 * SS, 16 * SS], fill=white, width=5 * SS)
        el(d, 27, 16, 2.5, 2.5, white)
    return shrink(img)


def spr_post():
    w, h = 20, 128
    arr = np.zeros((h, w, 4), np.uint8)
    for x in range(w):
        t = abs(x - w / 2) / (w / 2)
        arr[:, x, 0] = arr[:, x, 1] = arr[:, x, 2] = int(214 - 80 * t)
    arr[..., 3] = 255
    return Image.fromarray(arr, "RGBA")


def spr_icon(kind):
    """Small painted marks for the game-mode signs; each one names that mode's main rule."""
    img = canvas(32)
    d = ImageDraw.Draw(img)
    cream = (255, 240, 210, 255)
    if kind == "clock":
        d.ellipse([5 * SS, 5 * SS, 27 * SS, 27 * SS], outline=cream, width=3 * SS)
        d.line([16 * SS, 16 * SS, 16 * SS, 10 * SS], fill=cream, width=2 * SS)
        d.line([16 * SS, 16 * SS, 21 * SS, 18 * SS], fill=cream, width=2 * SS)
    elif kind == "leaf":
        leaf(d, 7, 25, -45, 25, (150, 214, 110, 255))
    elif kind == "fast":
        for x in (8, 17):
            d.line([x * SS, 7 * SS, (x + 8) * SS, 16 * SS, x * SS, 25 * SS], fill=cream, width=4 * SS, joint="curve")
    elif kind == "one":
        d.line([16 * SS, 6 * SS, 16 * SS, 26 * SS], fill=(240, 90, 80, 255), width=6 * SS)
        d.line([10 * SS, 11 * SS, 16 * SS, 6 * SS], fill=(240, 90, 80, 255), width=5 * SS)
    elif kind == "mine":
        for i in range(8):
            a = i * math.pi / 4
            d.line([16 * SS, 16 * SS, (16 + math.cos(a) * 13) * SS, (16 + math.sin(a) * 13) * SS], fill=(44, 40, 60, 255), width=3 * SS)
        el(d, 16, 16, 9, 9, (58, 52, 76, 255))
        el(d, 16, 16, 3, 3, (240, 90, 80, 255))
    elif kind == "chain":
        for x in (10, 22):
            d.ellipse([(x - 6) * SS, 10 * SS, (x + 6) * SS, 22 * SS], outline=cream, width=3 * SS)
    return shrink(img)


def spr_crack():
    img = canvas(CELL)
    d = ImageDraw.Draw(img)
    pts = [(14, 18), (24, 28), (20, 36), (32, 38), (30, 48), (42, 44), (50, 52)]
    d.line([(x * SS, y * SS) for x, y in pts], fill=(250, 244, 226, 255), width=int(2.2 * SS), joint="curve")
    d.line([(24 * SS, 28 * SS), (36 * SS, 20 * SS)], fill=(250, 244, 226, 255), width=int(1.6 * SS))
    return shrink(img)


def spr_ring():
    img = canvas(64)
    d = ImageDraw.Draw(img)
    d.ellipse([3 * SS, 3 * SS, 61 * SS, 61 * SS], outline=(255, 255, 255, 255), width=4 * SS)
    return shrink(img)


# ---------------------------------------------------------------- stroke font
# Original letterforms: polylines on a 4x6 grid, drawn with round caps.
GLYPHS = {
    "A": [[(0, 6), (2, 0), (4, 6)], [(1, 4), (3, 4)]],
    "B": [[(0, 0), (0, 6), (3, 6), (4, 5), (4, 4), (3, 3), (0, 3)], [(3, 3), (3.6, 2.2), (3.6, 1), (3, 0), (0, 0)]],
    "C": [[(4, 1), (3, 0), (1, 0), (0, 1), (0, 5), (1, 6), (3, 6), (4, 5)]],
    "D": [[(0, 0), (0, 6), (2.6, 6), (4, 4.6), (4, 1.4), (2.6, 0), (0, 0)]],
    "E": [[(4, 0), (0, 0), (0, 6), (4, 6)], [(0, 3), (3, 3)]],
    "F": [[(4, 0), (0, 0), (0, 6)], [(0, 3), (3, 3)]],
    "G": [[(4, 1), (3, 0), (1, 0), (0, 1), (0, 5), (1, 6), (3, 6), (4, 5), (4, 3.2), (2.2, 3.2)]],
    "H": [[(0, 0), (0, 6)], [(4, 0), (4, 6)], [(0, 3), (4, 3)]],
    "I": [[(1, 0), (3, 0)], [(2, 0), (2, 6)], [(1, 6), (3, 6)]],
    "J": [[(1, 0), (4, 0)], [(3.2, 0), (3.2, 5), (2.2, 6), (1, 6), (0, 5)]],
    "K": [[(0, 0), (0, 6)], [(4, 0), (0, 3.4)], [(1.4, 2.4), (4, 6)]],
    "L": [[(0, 0), (0, 6), (4, 6)]],
    "M": [[(0, 6), (0, 0), (2.5, 3.6), (5, 0), (5, 6)]],
    "N": [[(0, 6), (0, 0), (4, 6), (4, 0)]],
    "O": [[(1, 0), (3, 0), (4, 1), (4, 5), (3, 6), (1, 6), (0, 5), (0, 1), (1, 0)]],
    "P": [[(0, 6), (0, 0), (3, 0), (4, 1), (4, 2.4), (3, 3.4), (0, 3.4)]],
    "Q": [[(1, 0), (3, 0), (4, 1), (4, 5), (3, 6), (1, 6), (0, 5), (0, 1), (1, 0)], [(2.6, 4.4), (4.2, 6.2)]],
    "R": [[(0, 6), (0, 0), (3, 0), (4, 1), (4, 2.2), (3, 3.2), (0, 3.2)], [(2, 3.2), (4, 6)]],
    "S": [[(4, 1), (3, 0), (1, 0), (0, 1), (0, 2), (1, 3), (3, 3), (4, 4), (4, 5), (3, 6), (1, 6), (0, 5)]],
    "T": [[(0, 0), (4, 0)], [(2, 0), (2, 6)]],
    "U": [[(0, 0), (0, 5), (1, 6), (3, 6), (4, 5), (4, 0)]],
    "V": [[(0, 0), (2, 6), (4, 0)]],
    "W": [[(0, 0), (1.2, 6), (2.5, 2), (3.8, 6), (5, 0)]],
    "X": [[(0, 0), (4, 6)], [(4, 0), (0, 6)]],
    "Y": [[(0, 0), (2, 3), (4, 0)], [(2, 3), (2, 6)]],
    "Z": [[(0, 0), (4, 0), (0, 6), (4, 6)]],
    "0": [[(1, 0), (3, 0), (4, 1), (4, 5), (3, 6), (1, 6), (0, 5), (0, 1), (1, 0)], [(1.2, 4.4), (2.8, 1.6)]],
    "1": [[(0.6, 1.4), (2, 0), (2, 6)], [(0.6, 6), (3.4, 6)]],
    "2": [[(0, 1), (1, 0), (3, 0), (4, 1), (4, 2), (0, 6), (4, 6)]],
    "3": [[(0, 1), (1, 0), (3, 0), (4, 1), (4, 2), (3, 3), (1.6, 3)], [(3, 3), (4, 4), (4, 5), (3, 6), (1, 6), (0, 5)]],
    "4": [[(3, 6), (3, 0), (0, 4), (4, 4)]],
    "5": [[(4, 0), (0.4, 0), (0.2, 2.6), (3, 2.6), (4, 3.6), (4, 5), (3, 6), (1, 6), (0, 5)]],
    "6": [[(3.6, 0), (1.4, 0), (0, 2), (0, 5), (1, 6), (3, 6), (4, 5), (4, 3.6), (3, 2.8), (1, 2.8), (0, 3.6)]],
    "7": [[(0, 0), (4, 0), (1.4, 6)]],
    "8": [[(1, 3), (0, 2), (0, 1), (1, 0), (3, 0), (4, 1), (4, 2), (3, 3), (1, 3), (0, 4), (0, 5), (1, 6), (3, 6), (4, 5), (4, 4), (3, 3)]],
    "9": [[(0.4, 6), (2.6, 6), (4, 4), (4, 1), (3, 0), (1, 0), (0, 1), (0, 2.4), (1, 3.2), (3, 3.2), (4, 2.4)]],
    "+": [[(0.6, 3), (3.4, 3)], [(2, 1.6), (2, 4.4)]],
    "-": [[(0.8, 3), (3.2, 3)]],
    "!": [[(1, 0), (1, 4)], [(1, 6), (1, 6.01)]],
    "?": [[(0, 1), (1, 0), (3, 0), (4, 1), (4, 2), (2, 3.4), (2, 4.2)], [(2, 6), (2, 6.01)]],
    ".": [[(1, 6), (1, 6.01)]],
    ",": [[(1, 5.6), (0.6, 6.8)]],
    ":": [[(1, 2), (1, 2.01)], [(1, 5), (1, 5.01)]],
    "/": [[(3, 0), (0, 6)]],
    "%": [[(4, 0), (0, 6)], [(0.6, 0.6), (0.61, 0.6)], [(3.4, 5.4), (3.41, 5.4)]],
    "'": [[(1, 0), (1, 1.4)]],
    "(": [[(2, 0), (1, 1.4), (1, 4.6), (2, 6)]],
    ")": [[(1, 0), (2, 1.4), (2, 4.6), (1, 6)]],
    "=": [[(0.6, 2.2), (3.4, 2.2)], [(0.6, 3.8), (3.4, 3.8)]],
}


def glyph_extent(ch):
    xs = [p[0] for line in GLYPHS[ch] for p in line]
    return min(xs), max(xs)


def stroke_text(d, text, cx, cy, size, fill, width):
    """Draws text with the stroke font directly onto a supersampled canvas (centred)."""
    unit = size / 6.0
    gap = unit * 1.5
    total = -gap
    for ch in text:
        lo, hi = glyph_extent(ch) if ch != " " else (0, 2)
        total += (hi - lo) * unit + gap
    pen = cx - total / 2
    for ch in text:
        if ch == " ":
            pen += 2 * unit + gap
            continue
        lo, hi = glyph_extent(ch)
        for line in GLYPHS[ch]:
            pts = [((pen + (x - lo) * unit) * SS, (cy - size / 2 + y * unit) * SS) for x, y in line]
            d.line(pts, fill=fill, width=int(width * SS), joint="curve")
            for p in (pts[0], pts[-1]):
                r = width * SS / 2
                d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=fill)
        pen += (hi - lo) * unit + gap


FONT_CELL = 40
FONT_SIZE = 26
FONT_STROKE = 5.2


def font_glyphs():
    unit = FONT_SIZE / 6.0
    out = []
    for ch in sorted(GLYPHS):
        lo, hi = glyph_extent(ch)
        img = canvas(FONT_CELL)
        d = ImageDraw.Draw(img)
        stroke_text(d, ch, FONT_CELL / 2, FONT_CELL / 2, FONT_SIZE, (255, 255, 255, 255), FONT_STROKE)
        m = mask_of(img)
        edge = dilate(m, 2.3 * SS)
        final = recolor(edge, INK)
        final.alpha_composite(img)
        adv = (hi - lo) * unit + FONT_STROKE + 2.4
        out.append((ch, shrink(final), adv))
    return out


# ---------------------------------------------------------------- logo
def spr_logo():
    w, h = 340, 118
    img = canvas(w, h)
    d = ImageDraw.Draw(img)
    stroke_text(d, "FRUIT", w / 2, 33, 42, (255, 255, 255, 255), 9.5)
    stroke_text(d, "SLICER", w / 2, 86, 42, (255, 255, 255, 255), 9.5)
    m = mask_of(img)
    # Top-to-bottom fill: citrus to berry
    y = np.linspace(0, 1, m.height)[:, None]
    top, bot = np.array([255, 208, 66]), np.array([240, 66, 112])
    grad = (top * (1 - y) + bot * y)[None, ...] if False else None
    rows = (top[None, :] * (1 - y) + bot[None, :] * y).astype(np.uint8)
    fill = np.zeros((m.height, m.width, 4), np.uint8)
    fill[..., :3] = rows[:, None, :]
    fill[..., 3] = np.array(m)
    fill_img = Image.fromarray(fill, "RGBA")
    # Diagonal cut through the lettering: an offset wedge with a bright slash.
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    edge = dilate(m, 3 * SS)
    out.alpha_composite(recolor(dilate(m, 5.2 * SS), CREAM))
    out.alpha_composite(recolor(edge, INK))
    out.alpha_composite(fill_img)
    slash = Image.new("L", img.size, 0)
    sd = ImageDraw.Draw(slash)
    sd.polygon([(0, 74 * SS), (w * SS, 42 * SS), (w * SS, 45.5 * SS), (0, 78 * SS)], fill=255)
    out.paste(Image.new("RGBA", img.size, (0, 0, 0, 0)), (0, 0), ImageChops.multiply(slash, Image.new("L", img.size, 255)))
    glow = Image.new("L", img.size, 0)
    ImageDraw.Draw(glow).polygon([(0, 75 * SS), (w * SS, 43 * SS), (w * SS, 44.2 * SS), (0, 76.2 * SS)], fill=255)
    glow = ImageChops.multiply(glow, edge)  # the slash only exists on the lettering
    out.alpha_composite(recolor(glow, (255, 255, 255, 230)))
    return shrink(out)


# ---------------------------------------------------------------- backgrounds
def v_gradient(w, h, stops):
    ys = np.linspace(0, 1, h)
    pos = [s[0] for s in stops]
    arr = np.zeros((h, w, 4), np.uint8)
    for c in range(3):
        arr[:, :, c] = np.interp(ys, pos, [s[1][c] for s in stops])[:, None].astype(np.uint8)
    arr[..., 3] = 255
    return Image.fromarray(arr, "RGBA")


def hill_layer(w, h, base, amp, freq, color, seed, trees=0, tree_color=None, dots=(236, 118, 88, 255)):
    rng = random.Random(seed)
    img = Image.new("RGBA", (w * 2, h * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    ph = [rng.uniform(0, 6.28) for _ in range(3)]
    def top(x):
        return base + amp * (math.sin(x * freq + ph[0]) * 0.6 + math.sin(x * freq * 2.3 + ph[1]) * 0.28 + math.sin(x * freq * 5.1 + ph[2]) * 0.12)
    pts = [(x * 2, top(x) * 2) for x in range(0, w + 1, 2)]
    d.polygon(pts + [(w * 2, h * 2), (0, h * 2)], fill=color)
    tc = tree_color or color
    for _ in range(trees):
        x = rng.uniform(0, w)
        y = top(x)
        r = rng.uniform(9, 17)
        d.rectangle([(x - 1.6) * 2, y * 2 - r * 1.4, (x + 1.6) * 2, (y + 3) * 2], fill=tc)
        d.ellipse([(x - r) * 2, (y - r * 2.1) * 2, (x + r) * 2, (y - r * 0.2) * 2], fill=tc)
        d.ellipse([(x - r * 1.2) * 2, (y - r * 1.6) * 2, (x - r * 0.1) * 2, (y - r * 0.5) * 2], fill=tc)
        d.ellipse([(x + r * 0.1) * 2, (y - r * 1.7) * 2, (x + r * 1.2) * 2, (y - r * 0.6) * 2], fill=tc)
        for _ in range(5):
            a, q = rng.uniform(0, 6.28), rng.uniform(0, r * 0.9)
            fx, fy = x + math.cos(a) * q, y - r * 1.2 + math.sin(a) * q * 0.8
            d.ellipse([(fx - 1.6) * 2, (fy - 1.6) * 2, (fx + 1.6) * 2, (fy + 1.6) * 2], fill=dots)
    return img.resize((w, h), Image.LANCZOS)


from gen_backgrounds import bg_top, bg_boards  # noqa: E402


# ---------------------------------------------------------------- meta art
def meta_art():
    from gen_meta import build_meta
    sky, far, mid, near = bg_top(0, hill_layer, v_gradient, spr_soft)
    build_meta(sky, far, mid, spr_logo())


# ---------------------------------------------------------------- sheets
def write_sheet(name, images, fmt="rgba8", extra=""):
    lines = [f"--atlas -f {fmt} -z auto --border transparent{extra}"]
    shutil.rmtree(os.path.join(ROOT, "gfx", "png", name), ignore_errors=True)
    for key, img in images:
        rel = f"png/{name}/{key}.png"
        img.save(path("gfx", "png", name, key + ".png"))
        lines.append(rel)
    with open(path("gfx", name + ".t3s"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def build_graphics():
    # The 2D fruit is its own sheet, so that the 3D art style never has to keep it in memory.
    fruit = []
    for spec in FRUITS:
        whole, left, right = fruit_images(spec)
        fruit += [(spec[0], whole), (spec[0] + "_l", left), (spec[0] + "_r", right)]
    fruit += [("bomb", spr_bomb()), ("sunburst", spr_sunburst())]
    write_sheet("fruit", fruit)

    sprites = [
        ("pw_freeze", spr_power("freeze")),
        ("pw_double", spr_power("double")),
        ("pw_frenzy", spr_power("frenzy")),
        ("splat0", spr_splat(1)), ("splat1", spr_splat(2)), ("splat2", spr_splat(3)),
        ("drop", spr_drop()),
        ("spark", spr_spark()),
        ("glow", spr_soft(48)),
        ("ring", spr_ring()),
        ("crack", spr_crack()),
        ("pip_on", spr_pip(True)),
        ("pip_off", spr_pip(False)),
        ("panel", spr_panel()),
        ("plank", spr_plank()),
        ("plank_focus", spr_plank_focus()),
        ("knob", spr_knob()),
        ("arrow", spr_arrow()),
        ("arrow_back", spr_arrow(True)),
        ("post", spr_post()),
        ("ic_clock", spr_icon("clock")),
        ("ic_leaf", spr_icon("leaf")),
        ("ic_fast", spr_icon("fast")),
        ("ic_one", spr_icon("one")),
        ("ic_mine", spr_icon("mine")),
        ("ic_chain", spr_icon("chain")),
    ]
    write_sheet("sprites", sprites)
    write_sheet("logo", [("logo", spr_logo())])

    # Luminance + alpha keeps the white fill tintable and the dark outline dark, at half the memory.
    glyphs = font_glyphs()
    write_sheet("font", [(f"g{i:02d}", g[1]) for i, g in enumerate(glyphs)], fmt="la8")
    index = [-1] * 96
    adv = [0] * 96
    for i, (ch, _, a) in enumerate(glyphs):
        index[ord(ch) - 32] = i
        adv[ord(ch) - 32] = int(round(a))
    adv[0] = 12
    with open(path("source", "font_metrics.h"), "w", newline="\n") as f:
        f.write("// Generated by tools/gen_assets.py. Do not edit.\n#pragma once\n")
        f.write(f"#define FONT_CELL {FONT_CELL}\n#define FONT_CAP {FONT_SIZE}\n")
        f.write("static const signed char kFontIndex[96] = {" + ",".join(map(str, index)) + "};\n")
        f.write("static const unsigned char kFontAdvance[96] = {" + ",".join(map(str, adv)) + "};\n")

    # One sheet per scene and board: only the chosen one is ever in memory.
    for i in range(3):
        sky, far, mid, near = bg_top(i, hill_layer, v_gradient, spr_soft)
        write_sheet(f"bgtop{i}", [("sky", sky), ("far", far), ("mid", mid), ("near", near)], fmt="rgba5551")
    for i, b in enumerate(bg_boards()):
        write_sheet(f"board{i}", [("board", b)], fmt="rgb565")


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    if what in ("all", "gfx"):
        build_graphics()
        meta_art()
    if what in ("all", "audio"):
        import gen_audio
        gen_audio.build_audio()
    print("assets generated")
