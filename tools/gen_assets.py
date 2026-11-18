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


# ---------------------------------------------------------------- fruit added in 1.0
def overlay(d, draw, blur=0.0):
    """Draws on a layer that is blended over the image (plain ImageDraw replaces alpha) and kept inside it."""
    img = d._image
    lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    draw(ImageDraw.Draw(lay))
    if blur:
        lay = lay.filter(ImageFilter.GaussianBlur(blur * SS))
    lay.putalpha(ImageChops.multiply(lay.getchannel("A"), img.getchannel("A")))
    img.alpha_composite(lay)


def bez(p0, p1, p2, t):
    u = 1 - t
    return (u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0], u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1])


def bez_normal(p0, p1, p2, t):
    dx = 2 * (1 - t) * (p1[0] - p0[0]) + 2 * t * (p2[0] - p1[0])
    dy = 2 * (1 - t) * (p1[1] - p0[1]) + 2 * t * (p2[1] - p1[1])
    ln = math.hypot(dx, dy) or 1
    return -dy / ln, dx / ln


def tube(curve, width, n=48):
    """Outline of a curved, tapering pod along a quadratic curve; width(t) is the half-width."""
    left, right = [], []
    for k in range(n + 1):
        t = k / n
        x, y = bez(*curve, t)
        nx, ny = bez_normal(*curve, t)
        w = width(t)
        left.append((x + nx * w, y + ny * w))
        right.append((x - nx * w, y - ny * w))
    return left + right[::-1]


def tube_at_x(curve, width, x):
    """Where the pod crosses the blade line x: centre y and half its height there."""
    lo, hi = 0.0, 1.0
    for _ in range(40):
        mid = (lo + hi) / 2
        if bez(*curve, mid)[0] < x:
            lo = mid
        else:
            hi = mid
    t = (lo + hi) / 2
    ny = abs(bez_normal(*curve, t)[1]) or 1
    return bez(*curve, t)[1], width(t) / ny


def cut_circles(fd, circles, cut_w, flesh, rim, seed, pit=None):
    """Cut faces for fruit made of round parts (cherries, grapes): one slice per part the blade crosses."""
    for cx, cy, r in circles:
        dx = abs(32 - cx)
        if dx >= r - 1:
            continue
        h = math.sqrt(r * r - dx * dx)
        el(fd, 32, cy, min(cut_w + 1.4, r), h - 0.3, rim)
        el(fd, 32, cy, min(cut_w, r - 1.2), h - 1.8, flesh)
        if pit:
            el(fd, 32, cy, min(cut_w, r) * 0.42, h * 0.5, pit)
        elif seed and h > 5:
            el(fd, 32, cy, 0.8, 1.4, seed)


BANANA = ((7, 21), (30, 60), (57, 17))


def banana_w(t):
    return 1.2 + 7.3 * math.sin(math.pi * t) ** 0.6


def f_banana(d):
    poly(d, tube(BANANA, banana_w), (252, 214, 64, 255))
    poly(d, tube(((55.2, 20), (57, 15), (59.5, 11)), lambda t: 2.4 - 0.4 * t, 8), (150, 152, 64, 255))


def f_banana_top(d):
    for off, col in ((-0.35, (255, 240, 150, 255)), (0.45, (222, 172, 40, 255))):
        pts = []
        for k in range(6, 43):
            t = k / 48
            x, y = bez(*BANANA, t)
            nx, ny = bez_normal(*BANANA, t)
            w = banana_w(t) * off
            pts.append(((x + nx * w) * SS, (y + ny * w) * SS))
        d.line(pts, fill=col, width=int(1.2 * SS), joint="curve")
    el(d, 7.6, 21.6, 1.9, 1.9, (84, 60, 36, 255))
    el(d, 59.6, 11.2, 1.5, 1.5, (96, 78, 44, 255))


def cut_banana(fd, cy, ch, cut_w, flesh, rim):
    y, h = tube_at_x(BANANA, banana_w, 32)
    el(fd, 32, y, cut_w + 1.6, h + 1, rim)
    el(fd, 32, y, cut_w, h - 1.3, flesh)
    for dy in (-1.5, 1.5):
        el(fd, 32, y + dy, 0.6, 0.6, (150, 116, 70, 255))


def mango_outline():
    pts = []
    for i in range(96):
        a = i / 96 * math.tau
        x, y = math.cos(a) * 18.5, math.sin(a) * 22
        x += 2.6 * math.exp(-((a - 3.3) / 0.7) ** 2)  # the shallow kidney curve on one side
        r = math.radians(-24)
        pts.append((32 + x * math.cos(r) - y * math.sin(r), 34.5 + x * math.sin(r) + y * math.cos(r)))
    return pts


def f_mango(d):
    poly(d, mango_outline(), (252, 156, 46, 255))

    def tint(o):
        el(o, 41, 23, 12, 10, (232, 60, 50, 220))
        el(o, 24, 46, 14, 12, (255, 212, 64, 240))
    overlay(d, tint, blur=3.5)


def f_mango_top(d):
    stem(d, 40.5, 15, 42, 10, 2)
    leaf(d, 42, 11, -10, 13, (86, 170, 72, 255))
    el(d, 40.2, 15.6, 1.8, 1.2, (150, 60, 30, 255))


def cut_mango(fd, cy, ch, cut_w, flesh, rim):
    el(fd, 32, cy, cut_w + 1.6, ch / 2 - 0.8, rim)
    el(fd, 32, cy, cut_w, ch / 2 - 3.0, flesh)
    el(fd, 32, cy + 0.5, 3.0, ch / 2 - 9, (246, 234, 196, 255))
    for dy in (-6, 0, 6):
        fd.line([30.6 * SS, (cy + dy) * SS, 33.4 * SS, (cy + dy + 1.5) * SS], fill=(226, 204, 150, 255), width=SS // 2 + 1)


CHERRIES = ((39, 36.5, 10.5), (27, 44, 12))  # back, then front


def f_cherry(d):
    d.line([27.5 * SS, 32.5 * SS, 31 * SS, 21 * SS, 36 * SS, 12 * SS, 41 * SS, 7 * SS], fill=(108, 140, 56, 255), width=int(2.2 * SS), joint="curve")
    d.line([39 * SS, 26.5 * SS, 40 * SS, 17 * SS, 41 * SS, 7 * SS], fill=(108, 140, 56, 255), width=int(2.2 * SS), joint="curve")
    el(d, *CHERRIES[0][:2], CHERRIES[0][2], CHERRIES[0][2], (186, 22, 46, 255))
    el(d, *CHERRIES[1][:2], CHERRIES[1][2], CHERRIES[1][2], (222, 30, 54, 255))


def f_cherry_top(d):
    cx, cy, r = CHERRIES[1]
    d.arc([(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS], -75, 15, fill=(120, 10, 32, 255), width=int(1.3 * SS))
    for x, y, rr in CHERRIES:
        el(d, x + 0.5, y - rr + 1.6, 2.2, 1.1, (130, 12, 34, 255))
    el(d, 34.5, 31, 1.8, 1.2, (255, 214, 214, 255))
    el(d, 21.5, 38.5, 2.6, 1.7, (255, 204, 204, 255))
    leaf(d, 41, 7.5, -15, 12, (86, 170, 72, 255))


def cut_cherry(fd, cy, ch, cut_w, flesh, rim):
    cut_circles(fd, CHERRIES, cut_w, flesh, rim, None, pit=(240, 222, 196, 255))


def dragon_flaps():
    """Each scale as (base point, base point, tip), swept up the fruit."""
    out = []
    for i in range(11):
        a = -math.pi / 2 + (i - 5) * 0.52
        b1, b2 = a - 0.2, a + 0.2
        p1 = (32 + math.cos(b1) * 15, 35 + math.sin(b1) * 19)
        p2 = (32 + math.cos(b2) * 15, 35 + math.sin(b2) * 19)
        lean = -0.35 if math.cos(a) > 0.05 else (0.35 if math.cos(a) < -0.05 else 0)
        tip = (32 + math.cos(a + lean) * 21.5, 35 + math.sin(a + lean) * 26)
        out.append((p1, p2, tip))
    return out


def lerp2(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def f_dragon(d):
    for p1, p2, tip in dragon_flaps():
        poly(d, [p1, tip, p2], (236, 66, 142, 255))
    el(d, 32, 35, 16, 20, (226, 40, 128, 255))


def f_dragon_top(d):
    for p1, p2, tip in dragon_flaps():
        poly(d, [lerp2(p1, tip, 0.5), tip, lerp2(p2, tip, 0.5)], (132, 196, 70, 255))
    # Scales on the face: pink flaps with green tips, in staggered rows.
    for row, (y, xs) in enumerate(((24, (25, 39)), (34, (20, 32, 44)), (45, (26, 38)))):
        for x in xs:
            base = [(x - 4.2, y + 4.5), (x + 4.2, y + 4.5)]
            tip = (x + 1.2, y - 3.5)
            poly(d, [base[0], tip, base[1]], (246, 108, 168, 255))
            poly(d, [lerp2(base[0], tip, 0.55), tip, lerp2(base[1], tip, 0.55)], (150, 206, 80, 255))


def cut_dragon(fd, cy, ch, cut_w, flesh, rim):
    el(fd, 32, 35, cut_w + 1.6, 19.2, rim)
    el(fd, 32, 35, cut_w, 17.6, flesh)
    rng = random.Random(77)
    for _ in range(26):
        y = 35 + rng.uniform(-14, 14)
        span = cut_w * math.sqrt(max(0, 1 - ((y - 35) / 17) ** 2)) - 1.2
        el(fd, 32 + rng.uniform(-span, span), y, 0.6, 0.75, (30, 22, 30, 255))


def spiky_points():
    out = []
    for i in range(16):
        a = -math.pi / 2 + i * math.tau / 16
        out.append((32 + math.cos(a - 0.17) * 13, 34 + math.sin(a - 0.17) * 13))
        out.append((32 + math.cos(a) * 26.5, 34 + math.sin(a) * 26.5))
        out.append((32 + math.cos(a + 0.17) * 13, 34 + math.sin(a + 0.17) * 13))
    return out


def f_spiky(d):
    pts = spiky_points()
    for k in range(0, len(pts), 3):
        poly(d, pts[k:k + 3], (218, 212, 150, 255))
    el(d, 32, 34, 16, 16, (44, 92, 52, 255))


def f_spiky_top(d):
    # A shaded side on every spike, so they read as sharp cones and not as part of the sticker edge.
    pts = spiky_points()
    for k in range(0, len(pts), 3):
        b1, tip, b2 = pts[k:k + 3]
        poly(d, [lerp2(lerp2(b1, b2, 0.5), tip, 0.25), tip, lerp2(b2, tip, 0.25)], (170, 160, 100, 255))
    rng = random.Random(31)
    for _ in range(9):
        a, r = rng.uniform(0, 6.28), rng.uniform(3, 12)
        el(d, 32 + math.cos(a) * r, 34 + math.sin(a) * r, 2.6, 2.0, (32, 70, 40, 255))
    # Short thorns pointing out of the face, darker at the root.
    for x, y, a in ((26, 28, -2.3), (38, 28, -0.8), (32, 36, -1.57), (24, 40, 2.6), (40, 41, 0.5), (32, 45, 1.57)):
        ux, uy = math.cos(a), math.sin(a)
        el(d, x, y, 2.2, 2.2, (26, 56, 32, 255))
        poly(d, [(x - uy * 1.8, y + ux * 1.8), (x + ux * 6, y + uy * 6), (x + uy * 1.8, y - ux * 1.8)], (240, 236, 204, 255))


def cut_spiky(fd, cy, ch, cut_w, flesh, rim):
    cut_circles(fd, ((32, 34, 16),), cut_w, flesh, rim, None)
    rng = random.Random(5)
    for _ in range(7):
        el(fd, 32 + rng.uniform(-cut_w + 3, cut_w - 3), 34 + rng.uniform(-9, 9), 0.9, 1.4, (214, 186, 80, 255))


def grape_circles():
    out = []
    for y, n, shift in ((20, 4, 0), (29.5, 3, -1.5), (38.5, 3, 1.5), (47, 2, -1), (55, 1, 0.5)):
        for i in range(n):
            out.append((32 + shift + (i - (n - 1) / 2) * 10.6, y, 6.3))
    return out


def f_grapes(d):
    stem(d, 32, 15, 30, 6, 2)
    for x, y, r in grape_circles():
        el(d, x, y, r, r, (112, 52, 156, 255))


def f_grapes_top(d):
    for x, y, r in grape_circles():
        el(d, x, y, r, r, (84, 34, 122, 255))
        el(d, x - 0.5, y - 0.6, r - 1.1, r - 1.1, (132, 66, 178, 255))
        el(d, x - 2.2, y - 2.4, 1.5, 1.1, (214, 186, 240, 255))
    leaf(d, 33, 11, -15, 17, (86, 170, 72, 255))


def cut_grapes(fd, cy, ch, cut_w, flesh, rim):
    cut_circles(fd, grape_circles(), cut_w, flesh, rim, (150, 176, 96, 255))


def f_grape(d):
    stem(d, 32, 26, 33.5, 20.5, 2)
    el(d, 32, 37, 11, 11, (122, 58, 168, 255))


def f_grape_top(d):
    el(d, 32, 26.6, 2.0, 1.0, (84, 34, 122, 255))
    el(d, 27.5, 32, 2.4, 1.7, (220, 196, 244, 255))


def cut_grape(fd, cy, ch, cut_w, flesh, rim):
    cut_circles(fd, ((32, 37, 11),), cut_w, flesh, rim, (150, 176, 96, 255))


HONEY = (232, 132, 22, 255)


def f_honey(d):
    el(d, 32, 35, 21.5, 21.5, (236, 180, 74, 255))


def f_honey_top(d):
    rng = random.Random(21)
    for _ in range(24):
        a, r = rng.uniform(0, 6.28), rng.uniform(4, 19)
        el(d, 32 + math.cos(a) * r, 35 + math.sin(a) * r, 0.7, 0.7, (250, 210, 120, 255))
    # A glaze of honey over the top, with drips running down.
    cap = [(32 + math.cos(math.radians(a)) * 21.3, 35 + math.sin(math.radians(a)) * 21.3) for a in range(-166, -13, 4)]
    for k in range(25):
        x = 52.5 - k * 41 / 24
        cap.append((x, 29 + 1.6 * math.sin(x * 0.55)))
    poly(d, cap, HONEY)
    for x, length in ((19, 10), (28, 18), (38, 8), (46, 13)):
        top = 29 + 1.6 * math.sin(x * 0.55) - 1
        d.rectangle([(x - 2.3) * SS, top * SS, (x + 2.3) * SS, (top + length) * SS], fill=HONEY)
        el(d, x, top + length, 3.0, 3.2, HONEY)
        d.line([(x - 1) * SS, (top + 2) * SS, (x - 1) * SS, (top + length) * SS], fill=(255, 196, 92, 255), width=SS)
        el(d, x - 1, top + length - 0.5, 0.9, 1.1, (255, 244, 214, 255))
    d.arc([14 * SS, 17 * SS, 50 * SS, 53 * SS], 205, 255, fill=(255, 226, 150, 255), width=int(1.5 * SS))
    el(d, 23, 21, 3.4, 1.8, (255, 248, 222, 255))


def jelly_outline(scale=1.0):
    pts = []
    for i in range(120):
        a = i / 120 * math.tau
        r = (20 + 1.3 * math.sin(3 * a + 0.5) + 0.9 * math.sin(5 * a + 1.3)) * scale
        y = math.sin(a) * r * (0.94 if math.sin(a) < 0 else 0.98)
        pts.append((32 + math.cos(a) * r * (1.04 if math.sin(a) > 0 else 1.0), 36 + y))
    return pts


def f_jelly(d):
    poly(d, jelly_outline(), (34, 164, 172, 255))

    def inner(o):
        poly(o, jelly_outline(0.8), (112, 226, 214, 255))
        el(o, 33, 41, 9.5, 8, (255, 132, 186, 255))
    overlay(d, inner, blur=2.6)


def f_jelly_top(d):
    def bubbles(o):
        for x, y, r in ((23, 40, 2.6), (41, 31, 2.0), (37, 48, 1.6), (27, 49, 1.2), (44, 41, 1.4)):
            o.ellipse([(x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS], outline=(255, 255, 255, 210), width=int(0.8 * SS))
            el(o, x - r * 0.35, y - r * 0.35, r * 0.3, r * 0.3, (255, 255, 255, 230))
        o.arc([15 * SS, 19 * SS, 49 * SS, 53 * SS], 195, 260, fill=(255, 255, 255, 235), width=int(2.4 * SS))
        el(o, 40, 23, 2.4, 1.6, (255, 255, 255, 200))
    overlay(d, bubbles)
    leaf(d, 32, 17, -60, 12, (86, 170, 72, 255))


CHILI = ((19, 16), (26, 52), (60, 49))


def chili_w(t):
    return 7.4 * (1 - t) ** 0.7 * min(1.0, 0.6 + t * 4) + 0.7


def f_chili(d):
    poly(d, tube(CHILI, chili_w), (214, 34, 38, 255))
    d.line([19 * SS, 16 * SS, 21 * SS, 9 * SS, 26 * SS, 5 * SS], fill=(72, 138, 56, 255), width=int(2.8 * SS), joint="curve")
    el(d, 19.5, 16.5, 6.5, 4.0, (72, 150, 60, 255))


def f_chili_top(d):
    pts = []
    for k in range(4, 34):
        t = k / 48
        x, y = bez(*CHILI, t)
        nx, ny = bez_normal(*CHILI, t)
        w = chili_w(t) * 0.45
        pts.append(((x - nx * w) * SS, (y - ny * w) * SS))
    d.line(pts, fill=(255, 132, 112, 255), width=int(1.4 * SS), joint="curve")
    for a in (-0.6, 0.3, 1.2):
        poly(d, [(19.5 + math.cos(a) * 2, 16.5 + math.sin(a) * 2), (19.5 + math.cos(a + 0.3) * 7.5, 18 + math.sin(a + 0.3) * 4.6),
                 (19.5 + math.cos(a + 0.6) * 2, 16.5 + math.sin(a + 0.6) * 2)], (52, 116, 44, 255))


def cut_chili(fd, cy, ch, cut_w, flesh, rim):
    y, h = tube_at_x(CHILI, chili_w, 32)
    el(fd, 32, y, cut_w + 1.4, h + 0.6, rim)
    el(fd, 32, y, cut_w, h - 1.2, flesh)
    for dy in (-2.2, 0, 2.2):
        el(fd, 32 + dy * 0.3, y + dy, 0.9, 0.7, (255, 236, 176, 255))


BUNCH = (((39, 11), (9, 20), (13, 55)), ((39, 11), (24, 33), (34, 60)), ((39, 11), (43, 33), (56, 50)))  # back to front


def bunch_w(t):
    return 1.6 + 6.6 * math.sin(math.pi * min(1.0, t * 1.04)) ** 0.55


def f_bunch(d):
    for k, curve in enumerate(BUNCH):
        poly(d, tube(curve, bunch_w), ((234, 186, 44, 255), (246, 202, 54, 255), (254, 216, 66, 255))[k])
    el(d, 39, 11, 5.2, 4.2, (150, 152, 64, 255))


def f_bunch_top(d):
    for k, curve in enumerate(BUNCH):
        pts = []
        for j in range(8, 42):
            t = j / 48
            x, y = bez(*curve, t)
            nx, ny = bez_normal(*curve, t)
            w = bunch_w(t) * 0.42
            pts.append(((x + nx * w) * SS, (y + ny * w) * SS))
        d.line(pts, fill=(255, 240, 150, 255), width=int(1.1 * SS), joint="curve")
        el(d, *bez(*curve, 1.0), 1.5, 1.5, (84, 60, 36, 255))
    poly(d, [(35, 8), (44, 8), (45, 3), (38, 3)], (120, 128, 56, 255))


def cut_bunch(fd, cy, ch, cut_w, flesh, rim):
    for y, h in ((26, 6), (41, 6.5), (56, 5.5)):
        el(fd, 32, y, cut_w + 1.4, h + 0.6, rim)
        el(fd, 32, y, cut_w, h - 1.2, flesh)


def f_pomegranate(d):
    el(d, 32, 38, 21.5, 20.5, (196, 38, 58, 255))
    poly(d, [(25, 21), (24, 8), (29, 13), (32, 6), (35, 13), (40, 8), (39, 21)], (160, 28, 48, 255))


def f_pomegranate_top(d):
    def blush(o):
        el(o, 42, 46, 12, 11, (140, 18, 44, 200))
        el(o, 22, 30, 9, 8, (244, 110, 110, 190))
    overlay(d, blush, blur=3.0)
    poly(d, [(27, 20), (26.5, 12), (30, 15.5), (32, 10), (34, 15.5), (37.5, 12), (37, 20)], (104, 18, 36, 255))
    el(d, 24, 28, 3.2, 1.8, (255, 214, 214, 255))


def cut_pomegranate(fd, cy, ch, cut_w, flesh, rim):
    el(fd, 32, cy, cut_w + 1.6, ch / 2 - 0.8, rim)
    el(fd, 32, cy, cut_w, ch / 2 - 3.0, flesh)
    rng = random.Random(77)
    for _ in range(26):
        y = cy + rng.uniform(-ch / 2 + 5, ch / 2 - 5)
        room = cut_w * math.sqrt(max(0.05, 1 - ((y - cy) / (ch / 2 - 3)) ** 2)) - 1.2
        x = 32 + rng.uniform(-room, room)
        el(fd, x, y, 1.5, 1.9, (206, 28, 62, 255))
        el(fd, x - 0.4, y - 0.6, 0.45, 0.45, (255, 170, 190, 255))


PINATA_POINTS = [(32 + math.cos(a) * 30, 34 + math.sin(a) * 28) for a in (math.radians(k * 60 - 90) for k in range(6))]
PINATA_COLOURS = ((238, 66, 138, 255), (255, 200, 54, 255), (46, 186, 190, 255), (252, 128, 44, 255))


def f_pinata(d):
    for k, tip in enumerate(PINATA_POINTS):
        a = math.atan2(tip[1] - 34, tip[0] - 32)
        s, c = math.sin(a), math.cos(a)
        poly(d, [tip, (32 + c * 15 - s * 9, 34 + s * 15 + c * 9), (32 + c * 15 + s * 9, 34 + s * 15 - c * 9)], PINATA_COLOURS[k % 4])
    el(d, 32, 34, 18.5, 18.5, PINATA_COLOURS[0])


def f_pinata_top(d):
    # Bands of tissue-paper fringe across the body.
    def bands(o):
        for k, y in enumerate(range(18, 54, 7)):
            o.rectangle([10 * SS, y * SS, 54 * SS, (y + 3.5) * SS], fill=PINATA_COLOURS[(k % 3) + 1])
            for x in range(11, 54, 3):
                poly(o, [(x, y + 3.5), (x + 3, y + 3.5), (x + 1.5, y + 6)], PINATA_COLOURS[(k % 3) + 1])
    mask = Image.new("L", d._image.size, 0)
    el(ImageDraw.Draw(mask), 32, 34, 18.5, 18.5, 255)
    lay = Image.new("RGBA", d._image.size, (0, 0, 0, 0))
    bands(ImageDraw.Draw(lay))
    lay.putalpha(ImageChops.multiply(lay.getchannel("A"), mask))
    d._image.alpha_composite(lay)
    el(d, 27, 26, 3.4, 2.0, (255, 240, 246, 255))


def cut_pinata(fd, cy, ch, cut_w, flesh, rim):
    el(fd, 32, cy, cut_w + 1.6, ch / 2 - 6.5, rim)
    el(fd, 32, cy, cut_w, ch / 2 - 8.5, flesh)
    rng = random.Random(9)
    for _ in range(12):
        el(fd, 32 + rng.uniform(-cut_w + 2, cut_w - 2), cy + rng.uniform(-ch / 2 + 11, ch / 2 - 11), 1.8, 1.8, PINATA_COLOURS[rng.randrange(4)])


# name, body, details, flesh colour, rim colour, seed colour (or a function that draws the whole cut face), half-width of cut face
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
    ("banana", f_banana, f_banana_top, (252, 246, 214, 255), (246, 224, 120, 255), cut_banana, 6),
    ("mango", f_mango, f_mango_top, (255, 172, 30, 255), (255, 208, 96, 255), cut_mango, 10),
    ("cherry", f_cherry, f_cherry_top, (168, 20, 44, 255), (130, 12, 32, 255), cut_cherry, 4.5),
    ("dragon", f_dragon, f_dragon_top, (252, 250, 248, 255), (226, 52, 132, 255), cut_dragon, 10),
    ("spiky", f_spiky, f_spiky_top, (250, 238, 160, 255), (70, 122, 62, 255), cut_spiky, 9),
    ("grapes", f_grapes, f_grapes_top, (206, 234, 150, 255), (108, 50, 140, 255), cut_grapes, 4),
    ("grape", f_grape, f_grape_top, (206, 234, 150, 255), (108, 50, 140, 255), cut_grape, 5.5),
    ("honey", f_honey, f_honey_top, (255, 146, 46, 255), (246, 206, 110, 255), (255, 226, 160, 255), 11),
    ("jelly", f_jelly, f_jelly_top, (255, 196, 222, 255), (120, 220, 214, 255), (255, 255, 255, 255), 10),
    ("chili", f_chili, f_chili_top, (250, 100, 52, 255), (190, 26, 30, 255), cut_chili, 5),
    ("bunch", f_bunch, f_bunch_top, (252, 246, 214, 255), (246, 224, 120, 255), cut_bunch, 5),
    ("pomegranate", f_pomegranate, f_pomegranate_top, (250, 214, 196, 255), (214, 70, 84, 255), cut_pomegranate, 12),
    ("pinata", f_pinata, f_pinata_top, (255, 232, 170, 255), (238, 66, 138, 255), cut_pinata, 11),
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
    if callable(seed):
        seed(fd, cy, ch, cut_w, flesh, rim)
    else:
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
    base = {"freeze": (90, 196, 244, 255), "double": (255, 110, 170, 255), "frenzy": (255, 158, 40, 255),
            "magnet": (230, 70, 70, 255), "wide": (120, 210, 110, 255), "shield": (150, 120, 240, 255),
            "time": (70, 200, 200, 255)}[kind]
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
    elif kind == "magnet":
        # A horseshoe magnet, open end down, with pale tips.
        d.arc([20 * SS, 17 * SS, 44 * SS, 41 * SS], 180, 360, fill=white, width=int(5 * SS))
        for x in (22.5, 41.5):
            d.line([x * SS, 29 * SS, x * SS, 41 * SS], fill=white, width=int(5 * SS))
            d.line([x * SS, 38 * SS, x * SS, 44 * SS], fill=(255, 210, 210, 255), width=int(5 * SS))
    elif kind == "wide":
        # Arrows pointing out both ways from a bar: a wider blade.
        d.line([17 * SS, 32 * SS, 47 * SS, 32 * SS], fill=white, width=int(3.4 * SS))
        poly(d, [(13, 32), (21, 25), (21, 39)], white)
        poly(d, [(51, 32), (43, 25), (43, 39)], white)
    elif kind == "shield":
        poly(d, [(32, 16), (45, 21), (44, 34), (32, 47), (20, 34), (19, 21)], white)
        poly(d, [(32, 21), (40, 24), (39.5, 33), (32, 41.5)], base)
    elif kind == "time":
        # A stopwatch with a plus.
        d.ellipse([20 * SS, 21 * SS, 44 * SS, 45 * SS], outline=white, width=int(3.4 * SS))
        d.line([32 * SS, 16 * SS, 32 * SS, 21 * SS], fill=white, width=int(3.4 * SS))
        d.line([32 * SS, 27 * SS, 32 * SS, 39 * SS], fill=white, width=int(3 * SS))
        d.line([26 * SS, 33 * SS, 38 * SS, 33 * SS], fill=white, width=int(3 * SS))
    else:
        for i in range(8):
            a = i * math.pi / 4
            r0, r1 = (6, 19) if i % 2 == 0 else (6, 13)
            d.line([(32 + math.cos(a) * r0) * SS, (32 + math.sin(a) * r0) * SS,
                    (32 + math.cos(a) * r1) * SS, (32 + math.sin(a) * r1) * SS], fill=white, width=int(3.4 * SS))
    return shrink(sticker(img, 1.8))


COIN_GOLD = (246, 196, 62, 255)
COIN_DEEP = (214, 150, 34, 255)
COIN_LIGHT = (255, 232, 140, 255)


def spr_coin(back=False):
    """The coin, face or back: gold, with an apple stamped on the face and a leaf ring on the back."""
    img = canvas(CELL)
    d = ImageDraw.Draw(img)
    el(d, 32, 32, 21, 21, COIN_DEEP)
    el(d, 32, 32, 17.5, 17.5, COIN_GOLD)
    if not back:
        # The stamp: an apple in a deeper gold, lit from the top left.
        el(d, 32.6, 34.6, 9.6, 9.0, COIN_DEEP)
        el(d, 31.4, 33.4, 9.0, 8.4, COIN_LIGHT)
        el(d, 32, 34, 8.4, 7.8, COIN_GOLD)
        d.line([32 * SS, 26 * SS, 33.5 * SS, 21.5 * SS], fill=COIN_DEEP, width=int(2.2 * SS))
        poly(d, [(34, 23.5), (40, 20.5), (38.5, 25)], COIN_DEEP)
    else:
        # The back: a ring of little leaves round a plain centre.
        for i in range(10):
            a = i * math.pi / 5
            el(d, 32 + math.cos(a) * 12.5, 32 + math.sin(a) * 12.5, 2.4, 2.4, COIN_DEEP)
        el(d, 32, 32, 6.5, 6.5, COIN_LIGHT)
        el(d, 32.5, 32.5, 5.5, 5.5, COIN_GOLD)
    img = shade(img, light=(255, 255, 230, 110))
    return shrink(sticker(img, 1.6))


def spr_coin_rim():
    """The coin edge-on: a narrow milled strip, shown while the coin turns through its edge."""
    img = canvas(16, CELL)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([5 * SS, 11 * SS, 11 * SS, 53 * SS], radius=2.5 * SS, fill=COIN_DEEP)
    for y in range(13, 52, 3):
        d.line([6 * SS, y * SS, 10 * SS, y * SS], fill=COIN_LIGHT, width=int(1.1 * SS))
    return shrink(sticker(img, 1.4))


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
    cream = (255, 242, 214, 255)
    warm = (232, 198, 150, 255)
    hi = (255, 252, 240, 255)
    if kind == "clock":
        el(d, 16, 17, 11, 11, (70, 52, 42, 180))
        d.ellipse([5 * SS, 6 * SS, 27 * SS, 28 * SS], outline=cream, width=3 * SS)
        d.rectangle([13.5 * SS, 2.5 * SS, 18.5 * SS, 5.5 * SS], fill=warm)
        d.line([16 * SS, 17 * SS, 16 * SS, 10.5 * SS], fill=hi, width=int(2.4 * SS))
        d.line([16 * SS, 17 * SS, 21.5 * SS, 19 * SS], fill=(255, 206, 90, 255), width=int(2.2 * SS))
        el(d, 16, 17, 1.8, 1.8, hi)
    elif kind == "leaf":
        leaf(d, 6, 26, -42, 24, (116, 188, 78, 255))
        leaf(d, 7, 25, -45, 23, (156, 222, 112, 255))
        d.line([8 * SS, 24 * SS, 22 * SS, 10 * SS], fill=(220, 250, 186, 220), width=int(1.5 * SS))
    elif kind == "fast":
        for i, x in enumerate((6, 15)):
            c = warm if i == 0 else cream
            poly(d, [(x, 6), (x + 10, 16), (x, 26), (x + 4, 16)], c)
            d.line([(x + 1) * SS, 8 * SS, (x + 8.5) * SS, 16 * SS], fill=hi, width=int(1.4 * SS))
    elif kind == "one":
        poly(d, [(16, 3), (28, 16), (16, 29), (4, 16)], (186, 48, 44, 255))
        poly(d, [(16, 5.5), (25.5, 16), (16, 26.5), (6.5, 16)], (242, 86, 76, 255))
        d.line([16 * SS, 9.5 * SS, 16 * SS, 18.5 * SS], fill=hi, width=int(3.2 * SS))
        el(d, 16, 22.5, 1.9, 1.9, hi)
    elif kind == "mine":
        for i in range(8):
            a = i * math.pi / 4
            x, y = 16 + math.cos(a) * 12.5, 16 + math.sin(a) * 12.5
            d.line([16 * SS, 16 * SS, x * SS, y * SS], fill=(48, 44, 68, 255), width=int(3 * SS))
            el(d, x, y, 1.6, 1.6, (214, 68, 58, 255))
        el(d, 16, 16, 9.2, 9.2, (62, 56, 84, 255))
        el(d, 13.5, 13.5, 3.2, 2.2, (130, 126, 164, 200))
        el(d, 16, 16, 3.4, 3.4, (248, 92, 80, 255))
        el(d, 15.2, 15.2, 1.2, 1.2, (255, 210, 190, 255))
    elif kind == "chain":
        d.ellipse([4 * SS, 9 * SS, 18 * SS, 23 * SS], outline=warm, width=int(3.2 * SS))
        d.ellipse([14 * SS, 9 * SS, 28 * SS, 23 * SS], outline=cream, width=int(3.2 * SS))
        d.line([12 * SS, 16 * SS, 20 * SS, 16 * SS], fill=hi, width=int(2.6 * SS))
    elif kind == "note":
        for x, dy in ((10.5, 1), (23.5, -1)):
            el(d, x - 2.2, 22.5 + dy, 4.2, 3.2, cream)
            el(d, x - 3.0, 21.8 + dy, 1.6, 1.1, hi)
            d.line([(x + 1.2) * SS, (22.5 + dy) * SS, (x + 1.2) * SS, (7 + dy) * SS], fill=cream, width=int(2.8 * SS))
        poly(d, [(10.5, 6), (25, 4), (25, 8.8), (10.5, 10.8)], (255, 214, 102, 255))
    return shrink(sticker(img, 1.1, shadow=False))


# ---------------------------------------------------------------- Orchard Blast icons (1.2)
# The new modes' sign marks, in the same hand as spr_icon's.
def spr_mode_icon(kind):
    img = canvas(32)
    d = ImageDraw.Draw(img)
    cream = (255, 242, 214, 255)
    hi = (255, 252, 240, 255)
    if kind == "tree":
        # Orchard Blast: a lush fruit tree with layered foliage and glossy apples.
        poly(d, [(13.5, 28), (18.5, 28), (17.2, 16), (14.8, 16)], (146, 96, 58, 255))
        d.line([15.4 * SS, 27 * SS, 15.8 * SS, 17 * SS], fill=(186, 132, 86, 255), width=int(1.4 * SS))
        el(d, 11, 14, 6.5, 5.8, (76, 152, 68, 255))
        el(d, 21, 14, 6.5, 5.8, (76, 152, 68, 255))
        el(d, 16, 11.5, 9.5, 8.0, (108, 190, 90, 255))
        el(d, 14, 9.0, 5.5, 4.0, (152, 222, 122, 220))
        for x, y in ((10.5, 12.5), (20.5, 10.5), (16.5, 15.5)):
            el(d, x, y, 2.6, 2.6, (238, 72, 66, 255))
            el(d, x - 0.7, y - 0.7, 0.9, 0.9, (255, 200, 190, 255))
    elif kind == "crown":
        # Boss Rush: a jeweled royal gold crown with bevel and gems.
        gold = (255, 208, 76, 255)
        deep = (208, 142, 32, 255)
        poly(d, [(4.5, 23.5), (4.5, 10.5), (10.8, 16.5), (16, 6.5), (21.2, 16.5), (27.5, 10.5), (27.5, 23.5)], gold)
        poly(d, [(16, 6.5), (21.2, 16.5), (27.5, 10.5), (27.5, 23.5), (16, 23.5)], (238, 182, 54, 255))
        d.rectangle([4.5 * SS, 22.5 * SS, 27.5 * SS, 26.8 * SS], fill=deep)
        d.line([5.5 * SS, 23.2 * SS, 26.5 * SS, 23.2 * SS], fill=(255, 238, 160, 255), width=int(1.2 * SS))
        for x, y in ((4.5, 10.5), (16, 6.5), (27.5, 10.5)):
            el(d, x, y, 1.8, 1.8, hi)
        el(d, 16, 19, 2.2, 2.2, (236, 66, 82, 255))
        for x in (9.5, 22.5):
            el(d, x, 19.5, 1.5, 1.5, (96, 196, 250, 255))
    elif kind == "cup":
        # Recipe: a smoothie cup with dome lid, striped straw, and berry swirl.
        d.line([18.5 * SS, 13 * SS, 24 * SS, 3.5 * SS], fill=(246, 96, 116, 255), width=int(2.6 * SS))
        d.line([18.5 * SS, 13 * SS, 24 * SS, 3.5 * SS], fill=hi, width=int(1.2 * SS))
        poly(d, [(7, 11.5), (25, 11.5), (22, 28), (10, 28)], cream)
        poly(d, [(8.4, 14.2), (23.6, 14.2), (21.4, 26.4), (10.6, 26.4)], (236, 98, 142, 255))
        poly(d, [(8.8, 14.2), (16, 14.2), (15, 26.4), (10.6, 26.4)], (250, 132, 170, 255))
        d.rounded_rectangle([5.5 * SS, 9.5 * SS, 26.5 * SS, 12.5 * SS], radius=1.5 * SS, fill=hi)
        d.line([10.5 * SS, 15.5 * SS, 12 * SS, 24.5 * SS], fill=(255, 220, 235, 210), width=int(1.4 * SS))
    elif kind == "target":
        # Sharpshooter: bullseye target with gloss and a gold center star/gleam.
        for r, c in ((12.5, (232, 74, 68, 255)), (9.0, cream), (5.6, (232, 74, 68, 255)), (2.5, (255, 216, 84, 255))):
            el(d, 16, 16, r, r, c)
        d.arc([5 * SS, 5 * SS, 27 * SS, 27 * SS], 200, 260, fill=(255, 180, 170, 220), width=int(1.5 * SS))
    elif kind == "juggle":
        # Juggle: three glossy fruit in the air on a glowing arc.
        d.arc([4.5 * SS, 6 * SS, 27.5 * SS, 33 * SS], 195, 345, fill=cream, width=int(2.2 * SS))
        for x, y, c, hc in (
            (7, 17, (238, 78, 70, 255), (255, 186, 176, 255)),
            (16, 7.5, (255, 198, 62, 255), (255, 242, 170, 255)),
            (25, 17, (118, 198, 82, 255), (204, 246, 176, 255)),
        ):
            el(d, x, y, 4.3, 4.3, c)
            el(d, x - 1.2, y - 1.2, 1.4, 1.1, hc)
    elif kind == "endless":
        # Endless Orchard: a gleaming gold-leaf infinity ribbon with sprouted orchard leaves.
        leaf(d, 16, 7.5, -125, 6.0, (108, 194, 90, 255))
        leaf(d, 16, 7.5, -55, 6.0, (146, 222, 118, 255))
        pts = []
        for i in range(65):
            th = i / 64 * math.tau
            x = 16 + 10.2 * math.cos(th) / (1 + math.sin(th) ** 2 * 0.45)
            y = 17.5 + 5.2 * math.sin(th) * math.cos(th)
            pts.append((x * SS, y * SS))
        d.line(pts, fill=(140, 86, 22, 255), width=int(4.4 * SS), joint="curve")
        d.line(pts, fill=(255, 212, 84, 255), width=int(2.6 * SS), joint="curve")
        el(d, 10.5, 15.2, 1.6, 1.1, hi)
        el(d, 21.5, 15.2, 1.6, 1.1, hi)
    return shrink(sticker(img, 1.1, shadow=False))


CARD_ICONS = [
    "ring", "health", "armor", "blade", "regrowth", "longedge", "steady", "lucky", "parry",
    "juice", "magnet", "bombsquad", "secondwind", "battery", "goldtouch", "seedpouch", "sharprind",
    "echo", "sweep", "luckyleaf", "overflow", "rhythm", "thickpeel", "glass",
    "blender", "freezepop", "defuser", "smoothie", "cleaver", "fruitrain", "pull", "slowmo",
    "goldapple", "fruitbomb", "rindwall", "ripeseed", "skippeel", "bossbane", "luckypit", "coinjar",
    "laststand", "flood",
]
# Badge colours by kind: upgrades amber, passive charms green, reusable charms blue, single use pink.
CARD_KIND = [0] * 9 + [1] * 15 + [2] * 8 + [3] * 10
KIND_COLOURS = [
    ((242, 174, 58, 255), (144, 88, 20, 255), (255, 224, 136, 255)),
    ((96, 184, 92, 255), (42, 102, 42, 255), (176, 238, 166, 255)),
    ((78, 154, 232, 255), (32, 76, 146, 255), (168, 218, 255, 255)),
    ((232, 98, 146, 255), (136, 42, 82, 255), (255, 184, 212, 255)),
]


def spr_card_icon(index):
    """One small round badge per Orchard Blast card: shaded medallion with bevel rim and detailed emblem."""
    key = CARD_ICONS[index]
    img = canvas(32)
    d = ImageDraw.Draw(img)
    fill, edge, rim_hi = KIND_COLOURS[CARD_KIND[index]]
    # Outer dark border and rich inner medallion.
    el(d, 16, 16, 15.2, 15.2, edge)
    el(d, 16, 15.6, 13.4, 13.4, fill)
    img = shade(img, light=(255, 255, 240, 88), dark=(0, 0, 0, 72))
    d = ImageDraw.Draw(img)
    # Inner bevel highlight on top-left and warm rim ring.
    d.arc([3.2 * SS, 2.8 * SS, 28.8 * SS, 28.4 * SS], 195, 325, fill=rim_hi, width=int(1.6 * SS))
    d.arc([3.2 * SS, 3.4 * SS, 28.8 * SS, 29.0 * SS], 20, 160, fill=edge, width=int(1.4 * SS))

    # Draw the emblem on its own layer so we can cast a crisp inner drop-shadow onto the medallion.
    em = canvas(32)
    ed = ImageDraw.Draw(em)
    w = (255, 252, 242, 255)
    cream = (255, 238, 204, 255)
    gold = (255, 214, 78, 255)
    gold_deep = (196, 136, 24, 255)
    ice = (176, 236, 255, 255)
    wood = (166, 114, 72, 255)
    red = (242, 84, 76, 255)
    green = (132, 214, 96, 255)
    dark = (44, 34, 56, 255)

    def ln(pts, width=2.5, c=w):
        ed.line([(x * SS, y * SS) for x, y in pts], fill=c, width=int(width * SS), joint="curve")
        for x, y in (pts[0], pts[-1]):
            el(ed, x, y, width * 0.45, width * 0.45, c)

    def ring(cx, cy, r, width=2.5, c=w):
        ed.ellipse([(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS], outline=c, width=int(width * SS))

    def heart(cx, cy, s, c=w, hi_c=None):
        el(ed, cx - s * 0.48, cy - s * 0.28, s * 0.56, s * 0.56, c)
        el(ed, cx + s * 0.48, cy - s * 0.28, s * 0.56, s * 0.56, c)
        poly(ed, [(cx - s * 1.01, cy - s * 0.08), (cx + s * 1.01, cy - s * 0.08), (cx, cy + s * 1.06)], c)
        if hi_c:
            el(ed, cx - s * 0.52, cy - s * 0.42, s * 0.22, s * 0.16, hi_c)

    def bomb_mark(cx, cy, r, c=dark):
        el(ed, cx, cy, r, r, c)
        el(ed, cx - r * 0.32, cy - r * 0.32, r * 0.3, r * 0.22, (120, 114, 148, 255))
        ln([(cx + r * 0.45, cy - r * 0.72), (cx + r * 0.88, cy - r * 1.28)], 1.9, wood)
        el(ed, cx + r * 0.92, cy - r * 1.32, 1.8, 1.8, gold)

    if key == "ring":
        ring(16, 18.5, 6.8, 2.8, gold)
        ring(16, 18.5, 5.6, 1.2, w)
        poly(ed, [(16, 5.2), (20.8, 9.6), (16, 13.2), (11.2, 9.6)], ice)
        poly(ed, [(16, 5.2), (18.5, 9.6), (16, 11.4), (13.5, 9.6)], w)
    elif key == "health":
        heart(15, 16.5, 6.8, (250, 88, 108, 255), (255, 196, 210, 255))
        ln([(23, 6.5), (23, 12.5)], 2.2, w)
        ln([(20, 9.5), (26, 9.5)], 2.2, w)
    elif key == "armor":
        poly(ed, [(16, 5.5), (25, 8.8), (23.8, 18.2), (16, 26.2), (8.2, 18.2), (7, 8.8)], w)
        poly(ed, [(16, 7.8), (22.4, 10.2), (21.5, 17.2), (16, 23.4), (10.5, 17.2), (9.6, 10.2)], (196, 212, 232, 255))
        poly(ed, [(16, 7.8), (16, 23.4), (10.5, 17.2), (9.6, 10.2)], w)
        ln([(16, 8), (16, 23)], 1.4, (148, 168, 196, 255))
    elif key == "blade":
        poly(ed, [(9, 21.5), (22.5, 6.5), (25.5, 9.5), (12, 24.5)], w)
        poly(ed, [(10.5, 23), (24, 8), (25.5, 9.5), (12, 24.5)], (196, 216, 236, 255))
        ln([(7.5, 19.5), (14, 26)], 2.8, gold)
        ln([(6.5, 25.5), (9.8, 22.2)], 2.6, wood)
    elif key == "regrowth":
        leaf(ed, 8.5, 24.5, -48, 15.5, green)
        leaf(ed, 9.2, 23.8, -50, 13.5, w)
        ln([(22.5, 7), (22.5, 14.5)], 2.4, w)
        ln([(18.8, 10.8), (26.2, 10.8)], 2.4, w)
    elif key == "longedge":
        ln([(6.5, 16), (25.5, 16)], 3.0, w)
        poly(ed, [(4.8, 16), (10.5, 10.8), (10.5, 21.2)], w)
        poly(ed, [(27.2, 16), (21.5, 10.8), (21.5, 21.2)], w)
        el(ed, 16, 16, 2.4, 2.4, gold)
    elif key == "steady":
        poly(ed, [(8.5, 25), (19.5, 6.5), (24.5, 9.8), (13.5, 25)], w)
        poly(ed, [(11, 25), (22, 8.2), (24.5, 9.8), (13.5, 25)], cream)
        ln([(7.5, 26.8), (21, 9)], 1.6, gold_deep)
    elif key == "lucky":
        el(ed, 16, 16, 8.8, 8.8, gold_deep)
        el(ed, 16, 16, 7.6, 7.6, gold)
        ring(16, 16, 6.2, 1.4, (255, 244, 180, 255))
        ln([(16, 12.2), (16, 19.8)], 2.2, gold_deep)
        el(ed, 13.2, 12.8, 1.6, 1.1, w)
    elif key == "parry":
        ln([(7.5, 24.5), (24, 8)], 3.0, w)
        ln([(24.5, 24.5), (8, 8)], 3.0, (212, 228, 246, 255))
        ln([(6.5, 21.5), (11, 26)], 2.4, gold)
        ln([(25.5, 21.5), (21, 26)], 2.4, gold)
        el(ed, 16, 16, 2.5, 2.5, gold)
    elif key == "juice":
        poly(ed, [(16, 5.5), (22.2, 16.8), (9.8, 16.8)], w)
        el(ed, 16, 18.8, 6.4, 6.4, w)
        el(ed, 13.8, 17.2, 2.0, 3.0, ice)
    elif key == "magnet":
        ed.arc([8 * SS, 6.5 * SS, 24 * SS, 22.5 * SS], 0, 180, fill=w, width=int(5.2 * SS))
        ed.rectangle([8 * SS, 10 * SS, 13.2 * SS, 15.5 * SS], fill=w)
        ed.rectangle([18.8 * SS, 10 * SS, 24 * SS, 15.5 * SS], fill=w)
        ed.rectangle([8 * SS, 7.5 * SS, 13.2 * SS, 11.2 * SS], fill=red)
        ed.rectangle([18.8 * SS, 7.5 * SS, 24 * SS, 11.2 * SS], fill=red)
    elif key == "bombsquad":
        bomb_mark(15.2, 17.8, 6.8)
        ring(16, 16, 10.6, 2.4, w)
        ln([(8.5, 8.5), (23.5, 23.5)], 2.4, red)
    elif key == "secondwind":
        heart(15.2, 17.2, 6.0, w, ice)
        ed.arc([5 * SS, 5 * SS, 27 * SS, 27 * SS], 195, 332, fill=gold, width=int(2.4 * SS))
        poly(ed, [(24.5, 8.2), (27.5, 14.2), (20.8, 12.8)], gold)
    elif key == "battery":
        ed.rounded_rectangle([6.5 * SS, 10.5 * SS, 23.8 * SS, 21.5 * SS], radius=2 * SS, outline=w, width=int(2.2 * SS))
        ed.rectangle([23.8 * SS, 13.5 * SS, 26.2 * SS, 18.5 * SS], fill=w)
        ed.rectangle([9.2 * SS, 13 * SS, 18.5 * SS, 19 * SS], fill=gold)
        poly(ed, [(15.5, 11.5), (12.5, 16.2), (15.8, 16.2), (14.5, 20.5), (18.8, 15.4), (15.5, 15.4)], w)
    elif key == "goldtouch":
        el(ed, 15.5, 17.5, 7.8, 7.8, gold)
        el(ed, 13.2, 15.0, 2.6, 1.8, (255, 248, 200, 255))
        leaf(ed, 15.5, 9.5, -55, 7.2, w)
        ln([(22.5, 6), (22.5, 10.5)], 1.8, w)
        ln([(20.2, 8.2), (24.8, 8.2)], 1.8, w)
    elif key == "seedpouch":
        el(ed, 16, 19.2, 8.2, 7.0, cream)
        poly(ed, [(11.5, 8.5), (20.5, 8.5), (18.4, 13.2), (13.6, 13.2)], cream)
        ln([(12.5, 12.8), (19.5, 12.8)], 2.0, gold_deep)
        for x, y in ((13, 19), (18.2, 18.2), (15.8, 22.2)):
            el(ed, x, y, 1.5, 2.0, (92, 56, 32, 255))
    elif key == "sharprind":
        for x in (8.5, 13.5, 18.5, 23.5):
            poly(ed, [(x - 3.4, 23.5), (x + 3.4, 23.5), (x, 8.5)], w)
            poly(ed, [(x, 23.5), (x + 3.4, 23.5), (x, 8.5)], (204, 224, 240, 255))
    elif key == "echo":
        ed.arc([6 * SS, 6 * SS, 29.5 * SS, 29.5 * SS], 190, 265, fill=w, width=int(3.2 * SS))
        ed.arc([2 * SS, 10 * SS, 25.5 * SS, 33.5 * SS], 190, 265, fill=ice, width=int(2.8 * SS))
    elif key == "sweep":
        ln([(7.5, 16.5), (13.8, 22.5), (24.8, 9.2)], 3.6, w)
    elif key == "luckyleaf":
        for dx, dy in ((-3.8, -3.8), (3.8, -3.8), (-3.8, 3.8), (3.8, 3.8)):
            el(ed, 16 + dx, 14.8 + dy, 4.3, 4.3, w)
            el(ed, 16 + dx * 0.6, 14.8 + dy * 0.6, 1.6, 1.6, green)
        ln([(16.8, 19.5), (20.8, 26.5)], 2.2, w)
    elif key == "overflow":
        poly(ed, [(8.8, 14), (23.2, 14), (21, 26.5), (11, 26.5)], w)
        poly(ed, [(10.4, 16.2), (21.6, 16.2), (19.8, 25), (12.2, 25)], (242, 108, 148, 255))
        for x, y, r in ((11, 10, 2.6), (20, 8.2, 2.6), (15.8, 6.2, 2.2)):
            el(ed, x, y, r, r, gold)
    elif key == "rhythm":
        el(ed, 11.8, 22, 4.2, 3.3, w)
        ln([(15.2, 22), (15.2, 6.8), (23.2, 10)], 2.6, w)
        el(ed, 22, 18, 2.0, 2.0, gold)
    elif key == "thickpeel":
        ring(16, 16, 9.2, 4.4, w)
        ring(16, 16, 6.8, 1.8, green)
        el(ed, 16, 16, 3.2, 3.2, gold)
    elif key == "glass":
        poly(ed, [(16, 5.5), (25.2, 13.8), (16, 26.8), (6.8, 13.8)], ice)
        poly(ed, [(16, 5.5), (20.5, 13.8), (16, 26.8), (11.5, 13.8)], w)
        ln([(7, 13.8), (25, 13.8)], 1.4, (90, 168, 222, 255))
    elif key == "blender":
        poly(ed, [(8.8, 6.8), (23.2, 6.8), (21, 20.2), (11, 20.2)], w)
        ed.rounded_rectangle([8.8 * SS, 20.8 * SS, 23.2 * SS, 26.2 * SS], radius=1.8 * SS, fill=cream)
        poly(ed, [(10.6, 11.2), (21.4, 11.2), (20.2, 18.6), (11.8, 18.6)], (242, 102, 148, 255))
        el(ed, 16, 23.5, 1.8, 1.8, gold_deep)
    elif key == "freezepop":
        ed.rounded_rectangle([14.2 * SS, 19.2 * SS, 17.8 * SS, 27.2 * SS], radius=1.5 * SS, fill=(232, 192, 136, 255))
        ed.rounded_rectangle([9.8 * SS, 5.2 * SS, 22.2 * SS, 20.2 * SS], radius=5.2 * SS, fill=ice)
        ed.rounded_rectangle([11.2 * SS, 6.8 * SS, 15.2 * SS, 17.8 * SS], radius=2.0 * SS, fill=w)
    elif key == "defuser":
        bomb_mark(14, 19, 6.8)
        ln([(18.8, 9.2), (25.8, 5.2)], 2.2, w)
        poly(ed, [(21.8, 3.2), (27.2, 7.2), (23.8, 10.2)], gold)
    elif key == "smoothie":
        ln([(18.8, 9.2), (23.2, 3.8)], 2.2, red)
        poly(ed, [(8.8, 9.8), (23.2, 9.8), (21, 26.8), (11, 26.8)], w)
        poly(ed, [(10.4, 13.6), (21.6, 13.6), (20, 25), (12, 25)], green)
        ln([(11.8, 15), (13, 23.5)], 1.4, w)
    elif key == "cleaver":
        ed.rounded_rectangle([7.8 * SS, 7.5 * SS, 22.2 * SS, 19.2 * SS], radius=1.6 * SS, fill=w)
        ed.rectangle([7.8 * SS, 16.5 * SS, 22.2 * SS, 19.2 * SS], fill=(200, 218, 236, 255))
        el(ed, 11, 10.8, 1.6, 1.6, edge)
        ed.rounded_rectangle([19.8 * SS, 18.2 * SS, 24.2 * SS, 27 * SS], radius=1.5 * SS, fill=wood)
    elif key == "fruitrain":
        for x, y, c in ((9, 9.2, red), (17.2, 13.8, gold), (23.8, 19.8, green), (11.8, 22, w)):
            el(ed, x, y, 3.8, 3.8, c)
            el(ed, x - 1.0, y - 1.0, 1.2, 1.0, w)
    elif key == "pull":
        ed.arc([10 * SS, 6 * SS, 26 * SS, 22 * SS], 90, 270, fill=w, width=int(5 * SS))
        for y in (10, 18):
            ln([(5, y), (9, y)], 2.0, gold)
        ed.rectangle([17 * SS, 6 * SS, 22.2 * SS, 11 * SS], fill=red)
        ed.rectangle([17 * SS, 17 * SS, 22.2 * SS, 22 * SS], fill=w)
    elif key == "slowmo":
        poly(ed, [(8.8, 6), (23.2, 6), (16, 16)], w)
        poly(ed, [(8.8, 26), (23.2, 26), (16, 16)], w)
        poly(ed, [(11.5, 8), (20.5, 8), (16, 14.2)], gold)
        poly(ed, [(11.5, 24.5), (20.5, 24.5), (16, 19.5)], gold)
    elif key == "goldapple":
        el(ed, 13, 18.2, 6.5, 7.5, gold)
        el(ed, 19, 18.2, 6.5, 7.5, gold)
        el(ed, 11.2, 15.2, 2.2, 3.0, (255, 248, 200, 255))
        ln([(16, 11), (17, 6)], 2.2, wood)
        leaf(ed, 17, 8, -20, 7.5, green)
    elif key == "fruitbomb":
        for i in range(8):
            a = i * math.pi / 4
            ln([(16 + math.cos(a) * 4.5, 16 + math.sin(a) * 4.5), (16 + math.cos(a) * 11.2, 16 + math.sin(a) * 11.2)], 2.4, w)
        el(ed, 16, 16, 4.8, 4.8, gold)
        el(ed, 16, 16, 2.4, 2.4, w)
    elif key == "rindwall":
        for row in range(3):
            y = 8.8 + row * 5.5
            off = 0 if row % 2 == 0 else 3.5
            for col in range(3):
                x = 6.8 + off + col * 7
                if x + 6 <= 26.2:
                    ed.rounded_rectangle([x * SS, y * SS, (x + 6) * SS, (y + 4.5) * SS], radius=0.8 * SS, fill=w if (row + col) % 2 == 0 else cream)
    elif key == "ripeseed":
        poly(ed, [(16, 26), (10.8, 17), (16, 7.8), (21.2, 17)], gold)
        poly(ed, [(16, 24), (12.2, 17), (16, 9.8)], w)
        ln([(24, 13.5), (24, 6)], 2.2, w)
        poly(ed, [(21, 8.2), (27, 8.2), (24, 3.8)], w)
    elif key == "skippeel":
        for x0, c in ((6.2, cream), (15.5, w)):
            poly(ed, [(x0, 7.8), (x0 + 9.2, 16), (x0, 24.2)], c)
            poly(ed, [(x0, 7.8), (x0 + 9.2, 16), (x0 + 2.5, 16)], w)
    elif key == "bossbane":
        poly(ed, [(6.8, 16), (6.8, 8.8), (11, 13), (16, 5.8), (21, 13), (25.2, 8.8), (25.2, 16)], gold)
        ln([(7.5, 25.8), (24.5, 17.8)], 3.2, w)
    elif key == "luckypit":
        el(ed, 16, 17, 6.2, 8.2, (206, 136, 88, 255))
        el(ed, 14.2, 14.5, 2.2, 3.2, (242, 190, 142, 255))
        for i in range(5):
            a = -math.pi / 2 + i * 2 * math.pi / 5
            el(ed, 16 + math.cos(a) * 11, 17 + math.sin(a) * 10, 1.8, 1.8, gold)
    elif key == "coinjar":
        ed.rounded_rectangle([8.8 * SS, 9.8 * SS, 23.2 * SS, 26.8 * SS], radius=3 * SS, outline=w, width=int(2.2 * SS))
        ed.rounded_rectangle([10.8 * SS, 5.8 * SS, 21.2 * SS, 9.2 * SS], radius=1.2 * SS, fill=cream)
        el(ed, 16, 19.2, 4.5, 4.5, gold)
        el(ed, 14.8, 18.0, 1.5, 1.2, w)
    elif key == "laststand":
        ln([(9.8, 26.8), (9.8, 5.8)], 2.6, cream)
        el(ed, 9.8, 5.2, 1.8, 1.8, gold)
        poly(ed, [(10.8, 6.2), (25.5, 10.2), (10.8, 15.5)], w)
        poly(ed, [(10.8, 10.8), (25.5, 10.2), (10.8, 15.5)], cream)
    elif key == "flood":
        for i, y in enumerate((10.8, 16.5, 22.2)):
            pts = [(6 + k, y + math.sin(k * 0.6) * 2.1) for k in range(0, 21)]
            ln(pts, 2.4, w if i != 1 else ice)

    # Composite emblem drop shadow and emblem onto the medallion, then add a specular gloss dot.
    em_mask = mask_of(em)
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    sh_alpha = ImageChops.offset(em_mask, int(0.8 * SS), int(1.2 * SS))
    sh.putalpha(sh_alpha.point(lambda a: a * 115 // 255))
    sh_col = Image.new("RGBA", img.size, (edge[0] // 2, edge[1] // 2, edge[2] // 2, 255))
    sh_col.putalpha(sh.getchannel("A"))
    img.alpha_composite(sh_col)
    img.alpha_composite(em)
    el(ImageDraw.Draw(img), 10.2, 8.2, 2.4, 1.4, (255, 255, 255, 110))
    return shrink(sticker(img, 1.1, shadow=False))


# ---------------------------------------------------------------- bosses
# Each boss has four poses: guard (guard up), open (guard down, laughing), daze (confused after a slice) and wind
# (winding up an attack). The 2D look is a whole sticker per pose; the 3D look draws the game's own 3D fruit and
# lays a face-only overlay of the same pose on it. The crown and the fuse are drawn by the game, since they move.
BOSS_CELL = 96
BOSS_POSES = ("guard", "open", "daze", "wind")
FACE_INK = (40, 22, 30, 255)


def boss_face(d, cx, cy, r, pose, kind, outline=False):
    """Eyes, brows and mouth for a boss face centred on (cx, cy), sized to a body of radius r."""
    ink = FACE_INK
    w = max(1.0, r * 0.06)
    white = (255, 252, 240, 255)
    soft_ink = (40, 22, 30, 220)
    for side in (-1, 1):
        ex, ey = cx + side * r * 0.34, cy - r * 0.12
        if pose == "dead":
            # Shocked X eyes with crisp cream rim for readability across any fruit rind.
            xr = r * 0.145
            if outline:
                halo = (255, 246, 222, 220)
                d.line([(ex - xr) * SS, (ey - xr) * SS, (ex + xr) * SS, (ey + xr) * SS], fill=halo, width=int(w * 2.1 * SS))
                d.line([(ex - xr) * SS, (ey + xr) * SS, (ex + xr) * SS, (ey - xr) * SS], fill=halo, width=int(w * 2.1 * SS))
            d.line([(ex - xr) * SS, (ey - xr) * SS, (ex + xr) * SS, (ey + xr) * SS], fill=ink, width=int(w * 1.25 * SS))
            d.line([(ex - xr) * SS, (ey + xr) * SS, (ex + xr) * SS, (ey - xr) * SS], fill=ink, width=int(w * 1.25 * SS))
            brow_col = (255, 200, 64, 255) if (kind == "chili" and not outline) else ink
            d.line([(ex - side * r * 0.14) * SS, (ey - r * 0.25) * SS, (ex + side * r * 0.14) * SS, (ey - r * 0.29) * SS],
                   fill=brow_col, width=int(w * 1.3 * SS))
            continue
        if pose == "daze":
            # Swirly eyes: a spiral of short strokes.
            pts = []
            for i in range(28):
                a = i * 0.55 + (0 if side < 0 else math.pi)
                rad = r * 0.02 + i * r * 0.0055
                pts.append(((ex + math.cos(a) * rad) * SS, (ey + math.sin(a) * rad) * SS))
            el(d, ex, ey, r * 0.17, r * 0.17, white)
            d.line(pts, fill=ink, width=int(w * SS * 0.8), joint="curve")
            continue
        eh = {"guard": 0.16, "open": 0.07, "wind": 0.1}[pose]
        if outline:
            el(d, ex, ey, r * 0.15 + w * 0.85, r * eh + w * 0.85, soft_ink)
        el(d, ex, ey, r * 0.15, r * eh, white)
        px = ex - side * r * 0.03 if pose != "open" else ex
        el(d, px, ey + r * 0.02, r * 0.065, min(r * 0.065, r * eh * 0.9), ink)
        # Brows: slanted in and down when angry, raised and cocky when the guard is down.
        if pose == "open":
            bx0, by0, bx1, by1 = ex - r * 0.16, ey - r * 0.2, ex + r * 0.16, ey - r * 0.24
        else:
            tilt = 0.12 if pose == "guard" else 0.18
            bx0, by0 = ex + side * r * 0.2, ey - r * (0.2 + tilt)
            bx1, by1 = ex - side * r * 0.14, ey - r * 0.14
        brow_col = (255, 200, 64, 255) if (kind == "chili" and not outline) else ink
        d.line([bx0 * SS, by0 * SS, bx1 * SS, by1 * SS], fill=brow_col, width=int(w * 1.55 * SS))
    my = cy + r * (0.42 if kind == "king" else 0.32)
    if pose == "dead":
        # Surprised open 'O' mouth + panic sweat drop.
        if outline:
            el(d, cx, my + r * 0.04, r * 0.17 + w * 0.7, r * 0.21 + w * 0.7, (255, 246, 222, 200))
        el(d, cx, my + r * 0.04, r * 0.16, r * 0.20, ink)
        el(d, cx, my + r * 0.11, r * 0.09, r * 0.08, (224, 86, 102, 255))
        el(d, cx + r * 0.68, cy - r * 0.04, r * 0.065, r * 0.095, (150, 215, 255, 255))
    elif pose == "open":
        # A wide laugh, tongue and all.
        el(d, cx, my, r * 0.26, r * 0.17, ink)
        el(d, cx, my + r * 0.07, r * 0.15, r * 0.08, (232, 96, 110, 255))
        d.rectangle([(cx - r * 0.2) * SS, (my - r * 0.17) * SS, (cx + r * 0.2) * SS, (my - r * 0.1) * SS], fill=white)
        # A drop of sweat: it's tired, and it knows you can hit it now.
        el(d, cx + r * 0.62, cy - r * 0.42, r * 0.06, r * 0.09, (150, 210, 255, 255))
    elif pose == "wind":
        # Gritted teeth.
        d.rounded_rectangle([(cx - r * 0.24) * SS, (my - r * 0.08) * SS, (cx + r * 0.24) * SS, (my + r * 0.1) * SS],
                            radius=r * 0.05 * SS, fill=white, outline=ink, width=int(w * SS))
        for i in range(-2, 3):
            x = cx + i * r * 0.09
            d.line([x * SS, (my - r * 0.08) * SS, x * SS, (my + r * 0.1) * SS], fill=ink, width=int(w * 0.7 * SS))
        d.line([(cx - r * 0.24) * SS, (my + r * 0.01) * SS, (cx + r * 0.24) * SS, (my + r * 0.01) * SS], fill=ink, width=int(w * 0.7 * SS))
    elif pose == "daze":
        pts = [((cx - r * 0.22 + i * r * 0.044) * SS, (my + math.sin(i * 1.3) * r * 0.04) * SS) for i in range(11)]
        d.line(pts, fill=ink, width=int(w * 1.2 * SS), joint="curve")
    else:
        d.arc([(cx - r * 0.22) * SS, (my - r * 0.02) * SS, (cx + r * 0.22) * SS, (my + r * 0.26) * SS], 200, 340,
              fill=ink, width=int(w * 1.4 * SS))
    if kind == "king":
        # The King's waxed moustache, over the mouth.
        mc = (250, 214, 90, 255)
        for side in (-1, 1):
            pts = []
            for i in range(13):
                t = i / 12
                pts.append(((cx + side * r * (0.04 + t * 0.36)) * SS, (my - r * 0.17 - math.sin(t * math.pi) * r * 0.04 - (t ** 3) * r * 0.1) * SS))
            d.line(pts, fill=ink, width=int(r * 0.1 * SS), joint="curve")
            d.line(pts, fill=mc, width=int(r * 0.06 * SS), joint="curve")
    elif kind == "pineapple":
        # Armored golden chin-guard goatee sitting cleanly beneath any expression.
        poly(d, [(cx - r * 0.14, my + r * 0.29), (cx + r * 0.14, my + r * 0.29), (cx, my + r * 0.44)], (92, 166, 70, 255))
    elif kind == "chili":
        # Flame-whisker cheek marks for The Inferno Pepper.
        for side in (-1, 1):
            poly(d, [(cx + side * r * 0.44, cy + r * 0.08), (cx + side * r * 0.63, cy + r * 0.02), (cx + side * r * 0.55, cy + r * 0.18)], (255, 206, 76, 255))
    elif kind == "durian":
        # Twin fangs tuck cleanly above the scowl/grin in guard/wind poses without overlapping daze/dead mouths.
        if pose in ("guard", "wind"):
            for side in (-1, 1):
                poly(d, [(cx + side * r * 0.15, my - r * 0.01), (cx + side * r * 0.25, my - r * 0.01), (cx + side * r * 0.20, my + r * 0.14)], white)
    elif kind == "lemon":
        # Sour diamond monocle-crest above the brows and citrus goatee tucked cleanly below the mouth.
        poly(d, [(cx, cy - r * 0.58), (cx + r * 0.12, cy - r * 0.42), (cx, cy - r * 0.28), (cx - r * 0.12, cy - r * 0.42)], (166, 74, 212, 255))
        poly(d, [(cx - r * 0.14, my + r * 0.29), (cx + r * 0.14, my + r * 0.29), (cx, my + r * 0.44)], (188, 224, 62, 255))
    elif kind == "apple":
        # Upper sneer fangs for Aggro Apple during active battle poses.
        if pose in ("guard", "wind"):
            for side in (-1, 1):
                poly(d, [(cx + side * r * 0.13, my - r * 0.01), (cx + side * r * 0.22, my - r * 0.01), (cx + side * r * 0.18, my + r * 0.13)], white)


def boss_body(kind, pose):
    c = BOSS_CELL
    img = canvas(c)
    d = ImageDraw.Draw(img)
    cx, cy, r = c / 2, c / 2 + 2, c * 0.42
    if kind == "king":
        for i in range(8):
            a = i * math.pi / 4 + math.pi / 8
            x, y = cx + math.cos(a) * r * 1.06, cy + math.sin(a) * r * 1.06
            d.line([cx * SS, cy * SS, x * SS, y * SS], fill=(60, 54, 84, 255), width=int(r * 0.24 * SS))
            el(d, x, y, r * 0.13, r * 0.13, (214, 62, 52, 255) if pose != "open" else (250, 202, 44, 255))
        body = (46, 42, 70, 255) if pose != "wind" else (70, 40, 66, 255)
        el(d, cx, cy, r * 0.88, r * 0.88, body)
        img = shade(img, light=(180, 190, 255, 90), dark=(0, 0, 0, 80))
        d = ImageDraw.Draw(img)
        # A royal sash across the chest.
        sash = Image.new("RGBA", img.size, (0, 0, 0, 0))
        sd = ImageDraw.Draw(sash)
        sd.line([(cx - r) * SS, (cy + r * 0.3) * SS, (cx + r) * SS, (cy + r * 0.95) * SS], fill=(170, 40, 70, 255), width=int(r * 0.2 * SS))
        disc = Image.new("L", img.size, 0)
        el(ImageDraw.Draw(disc), cx, cy, r * 0.88, r * 0.88, 255)
        sash.putalpha(ImageChops.multiply(mask_of(sash), disc))
        img.alpha_composite(sash)
        d = ImageDraw.Draw(img)
        boss_face(d, cx, cy - r * 0.08, r * 0.88, pose, "king")
    elif kind == "pineapple":
        # Tall spiky leaf crown and armored golden pineapple body.
        for a, ln, col in ((-130, r * 0.62, (54, 138, 62, 255)), (-108, r * 0.72, (74, 168, 72, 255)),
                           (-90, r * 0.80, (92, 190, 82, 255)), (-72, r * 0.72, (74, 168, 72, 255)),
                           (-50, r * 0.62, (54, 138, 62, 255))):
            leaf(d, cx, cy - r * 0.76, a, ln, col)
        gold = (242, 178, 52, 255) if pose != "wind" else (255, 196, 68, 255)
        el(d, cx, cy + r * 0.08, r * 0.84, r * 0.92, gold)
        img = shade(img, light=(255, 244, 180, 100), dark=(110, 56, 10, 80))
        d = ImageDraw.Draw(img)
        # Crisscross diamond armor lines clipped smoothly onto the body.
        grid = Image.new("RGBA", img.size, (0, 0, 0, 0))
        gd = ImageDraw.Draw(grid)
        for sign in (-1, 1):
            for k in range(-3, 4):
                gd.line([(cx + k * r * 0.34 - sign * r * 0.6) * SS, (cy - r * 0.85) * SS,
                         (cx + k * r * 0.34 + sign * r * 0.6) * SS, (cy + r * 1.05) * SS],
                        fill=(176, 108, 22, 180), width=max(2, int(r * 0.06 * SS)))
        disc = Image.new("L", img.size, 0)
        el(ImageDraw.Draw(disc), cx, cy + r * 0.08, r * 0.84, r * 0.92, 255)
        grid.putalpha(ImageChops.multiply(mask_of(grid), disc))
        img.alpha_composite(grid)
        patch = Image.new("RGBA", img.size, (0, 0, 0, 0))
        el(ImageDraw.Draw(patch), cx, cy + r * 0.06, r * 0.62, r * 0.48, (255, 232, 142, 175))
        img.alpha_composite(patch.filter(ImageFilter.GaussianBlur(SS * 3.0)))
        d = ImageDraw.Draw(img)
        boss_face(d, cx, cy + r * 0.06, r * 0.88, pose, "pineapple")
    elif kind == "chili":
        # Fiery red-orange chili pepper silhouette with blazing flame crests.
        for a, rad, fc in ((-130, r * 0.46, (255, 128, 36, 255)), (-95, r * 0.52, (255, 176, 48, 255)), (-55, r * 0.46, (255, 118, 34, 255))):
            leaf(d, cx, cy - r * 0.58, a, rad, fc)
        body = (226, 38, 42, 255) if pose != "wind" else (248, 66, 34, 255)
        el(d, cx, cy + r * 0.04, r * 0.82, r * 0.94, body)
        img = shade(img, light=(255, 214, 120, 110), dark=(90, 8, 12, 85))
        d = ImageDraw.Draw(img)
        stem(d, cx, cy - r * 0.84, cx + r * 0.16, cy - r * 1.06, w=max(2, int(r * 0.08)))
        leaf(d, cx - r * 0.04, cy - r * 0.84, -150, r * 0.34, (74, 162, 62, 255))
        leaf(d, cx + r * 0.04, cy - r * 0.84, -30, r * 0.34, (74, 162, 62, 255))
        patch = Image.new("RGBA", img.size, (0, 0, 0, 0))
        el(ImageDraw.Draw(patch), cx, cy + r * 0.05, r * 0.58, r * 0.46, (255, 142, 88, 155))
        img.alpha_composite(patch.filter(ImageFilter.GaussianBlur(SS * 2.8)))
        d = ImageDraw.Draw(img)
        boss_face(d, cx, cy + r * 0.04, r * 0.86, pose, "chili")
    elif kind == "durian":
        # Dusty Durian: sculpted faceted armored spikes with ochre-gold highlight bevels & clean face zone.
        for i in range(12):
            a = i * math.pi / 6.0
            ca, sa = math.cos(a), math.sin(a)
            bx0, by0 = cx + ca * r * 0.72 - sa * r * 0.14, cy + sa * r * 0.72 + ca * r * 0.14
            bx1, by1 = cx + ca * r * 0.72 + sa * r * 0.14, cy + sa * r * 0.72 - ca * r * 0.14
            bmid = (cx + ca * r * 0.74, cy + sa * r * 0.74)
            tip = (cx + ca * r * 1.09, cy + sa * r * 1.09)
            poly(d, [(bx0, by0), bmid, tip], (152, 132, 66, 255))
            poly(d, [bmid, (bx1, by1), tip], (116, 98, 48, 255))
        body = (166, 148, 76, 255) if pose != "wind" else (196, 166, 82, 255)
        el(d, cx, cy, r * 0.86, r * 0.86, body)
        img = shade(img, light=(245, 232, 168, 95), dark=(64, 52, 24, 90))
        d = ImageDraw.Draw(img)
        patch = Image.new("RGBA", img.size, (0, 0, 0, 0))
        el(ImageDraw.Draw(patch), cx, cy + r * 0.04, r * 0.64, r * 0.52, (218, 200, 128, 175))
        img.alpha_composite(patch.filter(ImageFilter.GaussianBlur(SS * 3.2)))
        d = ImageDraw.Draw(img)
        boss_face(d, cx, cy + r * 0.02, r * 0.88, pose, "durian")
    elif kind == "lemon":
        # Lord Lemon: sharp pointed citrus tips, vivid sour yellow rind & royal crest.
        for side in (-1, 1):
            poly(d, [(cx + side * r * 0.70, cy - r * 0.24), (cx + side * r * 0.70, cy + r * 0.24), (cx + side * r * 1.06, cy)], (232, 200, 40, 255))
        body = (246, 218, 52, 255) if pose != "wind" else (255, 238, 92, 255)
        el(d, cx, cy, r * 0.88, r * 0.78, body)
        img = shade(img, light=(255, 254, 210, 110), dark=(132, 102, 10, 80))
        d = ImageDraw.Draw(img)
        leaf(d, cx + r * 0.06, cy - r * 0.74, -26, r * 0.44, (84, 178, 66, 255))
        patch = Image.new("RGBA", img.size, (0, 0, 0, 0))
        el(ImageDraw.Draw(patch), cx, cy + r * 0.04, r * 0.54, r * 0.42, (255, 248, 170, 165))
        img.alpha_composite(patch.filter(ImageFilter.GaussianBlur(SS * 2.5)))
        d = ImageDraw.Draw(img)
        boss_face(d, cx, cy + r * 0.02, r * 0.86, pose, "lemon")
    elif kind == "apple":
        # Aggro Apple: deep crimson body with worm-hole pockets and stout stem.
        body = (206, 36, 48, 255) if pose != "wind" else (238, 54, 60, 255)
        el(d, cx, cy + r * 0.04, r * 0.90, r * 0.86, body)
        img = shade(img, light=(255, 196, 196, 105), dark=(78, 8, 18, 90))
        d = ImageDraw.Draw(img)
        stem(d, cx, cy - r * 0.78, cx + r * 0.12, cy - r * 1.04, w=max(2, int(r * 0.09)))
        leaf(d, cx + r * 0.08, cy - r * 0.86, -24, r * 0.40, (78, 168, 64, 255))
        for hx, hy in ((-0.54, -0.34), (0.56, -0.30), (-0.52, 0.36), (0.54, 0.38)):
            el(d, cx + hx * r, cy + hy * r, r * 0.11, r * 0.09, (48, 12, 18, 255))
        patch = Image.new("RGBA", img.size, (0, 0, 0, 0))
        el(ImageDraw.Draw(patch), cx, cy + r * 0.05, r * 0.54, r * 0.44, (248, 134, 134, 150))
        img.alpha_composite(patch.filter(ImageFilter.GaussianBlur(SS * 2.5)))
        d = ImageDraw.Draw(img)
        boss_face(d, cx, cy + r * 0.04, r * 0.88, pose, "apple")
    else:
        green = (58, 148, 78, 255) if pose != "wind" else (84, 150, 64, 255)
        el(d, cx, cy, r, r * 0.94, green)
        img = shade(img)
        d = ImageDraw.Draw(img)
        for i, x in enumerate((-0.66, -0.3, 0.06, 0.42, 0.74)):
            pts = []
            for t in range(-10, 11):
                y = t * r * 0.094
                xx = cx + x * r * math.sqrt(max(0, 1 - (y / (r * 0.94)) ** 2))
                pts.append(((xx + math.sin(t * 1.4 + i) * r * 0.06) * SS, (cy + y) * SS))
            d.line(pts, fill=(28, 96, 52, 255), width=int(r * 0.1 * SS), joint="curve")
        # A little stalk and a leaf, worn at an angle like a hat.
        stem(d, cx + r * 0.05, cy - r * 0.9, cx + r * 0.2, cy - r * 1.08, w=max(2, int(r * 0.08)))
        leaf(d, cx + r * 0.15, cy - r * 1.0, -20, r * 0.42, (96, 186, 80, 255))
        # A pale patch behind the face, so it reads on the dark stripes.
        patch = Image.new("RGBA", img.size, (0, 0, 0, 0))
        el(ImageDraw.Draw(patch), cx, cy + r * 0.05, r * 0.6, r * 0.5, (150, 206, 120, 150))
        img.alpha_composite(patch.filter(ImageFilter.GaussianBlur(SS * 3)))
        d = ImageDraw.Draw(img)
        boss_face(d, cx, cy, r, pose, "melon")
    if pose == "daze":
        # Little stars circling the head.
        for i in range(3):
            a = -math.pi / 2 + (i - 1) * 0.7
            sx, sy = cx + math.cos(a) * r * 0.62, cy - r * 0.72 + math.sin(a) * r * 0.1
            pts = []
            for k in range(10):
                rr = r * (0.11 if k % 2 == 0 else 0.045)
                aa = -math.pi / 2 + k * math.pi / 5
                pts.append((sx + math.cos(aa) * rr, sy + math.sin(aa) * rr))
            poly(d, pts, (255, 226, 90, 255))
    return shrink(sticker(img, 2.0))


def boss_face_overlay(kind, pose):
    """The face alone, for laying on the 3D boss with a soft body-toned cushion so faces blend cleanly over spikes."""
    c = BOSS_CELL
    img = canvas(c)
    cx, cy, r = c / 2, c / 2 + 2, c * 0.42
    scale = 0.88 if kind in ("king", "durian", "apple") else (0.90 if kind in ("pineapple", "chili") else (0.86 if kind == "lemon" else 1.0))
    off = -0.08 if kind == "king" else (0.06 if kind in ("pineapple", "chili") else (0.03 if kind in ("durian", "lemon", "apple") else 0.0))
    fcy = cy + r * off
    fr = r * scale
    cushions = {
        "durian": (182, 164, 88, 175),
        "pineapple": (236, 176, 56, 150),
        "king": (52, 48, 76, 120),
        "melon": (68, 152, 82, 130),
        "chili": (228, 48, 44, 130),
        "lemon": (246, 218, 62, 135),
        "apple": (210, 44, 56, 135),
    }
    patch = Image.new("RGBA", img.size, (0, 0, 0, 0))
    el(ImageDraw.Draw(patch), cx, fcy + fr * 0.05, fr * 0.64, fr * 0.54, cushions.get(kind, (120, 120, 120, 120)))
    img.alpha_composite(patch.filter(ImageFilter.GaussianBlur(SS * 4.2)))
    d = ImageDraw.Draw(img)
    boss_face(d, cx, fcy, fr, pose, kind, outline=True)
    return shrink(img)


def spr_worm():
    """Angry peeking worm for Aggro Apple."""
    img = canvas(CELL)
    d = ImageDraw.Draw(img)
    cx, cy = CELL / 2, CELL / 2
    for i, (ox, oy, rr) in enumerate(((-8, 8, 7), (-3, 4, 8), (3, -1, 9), (8, -6, 10))):
        col = (228, 126, 156, 255) if i < 3 else (246, 148, 174, 255)
        el(d, cx + ox, cy + oy, rr, rr, col)
    img = shade(img, light=(255, 224, 232, 95), dark=(95, 24, 48, 75))
    d = ImageDraw.Draw(img)
    el(d, cx + 6, cy - 8, 2.5, 2.5, (255, 252, 240, 255))
    el(d, cx + 11, cy - 7, 2.5, 2.5, (255, 252, 240, 255))
    el(d, cx + 6.5, cy - 8, 1.3, 1.3, FACE_INK)
    el(d, cx + 11.5, cy - 7, 1.3, 1.3, FACE_INK)
    d.line([(cx + 3) * SS, (cy - 12) * SS, (cx + 12) * SS, (cy - 10) * SS], fill=FACE_INK, width=int(2.2 * SS))
    return shrink(sticker(img, 1.6))


def boss_sprites():
    out = []
    for who in ("melon", "king"):
        out += [(f"boss_{who}_{p}", boss_body(who, p)) for p in BOSS_POSES]
        out += [(f"bface_{who}_{p}", boss_face_overlay(who, p)) for p in BOSS_POSES]
    return out


def orchard_sprites():
    """The 1.2+ sprites, after every older one so that no older sprite number moves."""
    out = [("ic_tree", spr_mode_icon("tree")), ("ic_crown", spr_mode_icon("crown")), ("ic_cup", spr_mode_icon("cup")),
           ("ic_target", spr_mode_icon("target")), ("ic_juggle", spr_mode_icon("juggle"))]
    out += [("cd_" + key, spr_card_icon(i)) for i, key in enumerate(CARD_ICONS)]
    out += boss_sprites()
    # 2.0 Endless bosses & icon added strictly after 1.2 sprites so older sprite IDs never shift.
    for who in ("pineapple", "chili"):
        out += [(f"boss_{who}_{p}", boss_body(who, p)) for p in BOSS_POSES]
        out += [(f"bface_{who}_{p}", boss_face_overlay(who, p)) for p in BOSS_POSES]
    out.append(("ic_endless", spr_mode_icon("endless")))
    # 2.0 Milestone bosses & worm hazard appended last.
    for who in ("durian", "lemon", "apple"):
        out += [(f"boss_{who}_{p}", boss_body(who, p)) for p in BOSS_POSES]
        out += [(f"bface_{who}_{p}", boss_face_overlay(who, p)) for p in BOSS_POSES]
    out.append(("boss_worm", spr_worm()))
    # 2.0 Milestone boss death poses (X eyes & surprised mouth) appended at tail so earlier indices stay fixed.
    for who in ("durian", "lemon", "apple"):
        out.append((f"boss_{who}_dead", boss_body(who, "dead")))
        out.append((f"bface_{who}_dead", boss_face_overlay(who, "dead")))
    return out


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
    "&": [[(4, 6), (1, 1.6), (1.6, 0.2), (2.8, 0.2), (3.2, 1.4), (0.2, 4.2), (0.4, 5.6), (2, 6), (4, 3.6)]],
    "_": [[(0, 6.6), (4, 6.6)]],
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
        # The fill alone too: text draws every outline first, then every fill, so a letter's outline
        # never paints over the letter beside it.
        out.append((ch, shrink(final), adv, shrink(img)))
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
    ph = [rng.uniform(0, 6.2831853) for _ in range(3)]
    # Exact integer harmonics across width w so x=0 and x=w match seamlessly when scrolling.
    c1 = max(1, int(round(w * freq / (2 * math.pi))))
    w1 = 2 * math.pi * c1 / w
    w2 = 2 * math.pi * (c1 * 2 + 1) / w
    w3 = 2 * math.pi * (c1 * 4 + 1) / w

    def top(x):
        return base + amp * (math.sin(x * w1 + ph[0]) * 0.6 + math.sin(x * w2 + ph[1]) * 0.28 + math.sin(x * w3 + ph[2]) * 0.12)

    pts = [(x * 2, top(x) * 2) for x in range(0, w + 1, 2)]
    d.polygon(pts + [(w * 2, h * 2), (0, h * 2)], fill=color)
    tc = tree_color or color
    for _ in range(trees):
        tx = rng.uniform(0, w)
        y = top(tx)
        r = rng.uniform(9, 17)
        spots = [(rng.uniform(0, 6.28), rng.uniform(0, r * 0.9)) for _ in range(5)]
        for x in (tx - w, tx, tx + w):
            if x + r * 1.4 < 0 or x - r * 1.4 > w:
                continue
            d.rectangle([(x - 1.6) * 2, y * 2 - r * 1.4, (x + 1.6) * 2, (y + 3) * 2], fill=tc)
            d.ellipse([(x - r) * 2, (y - r * 2.1) * 2, (x + r) * 2, (y - r * 0.2) * 2], fill=tc)
            d.ellipse([(x - r * 1.2) * 2, (y - r * 1.6) * 2, (x - r * 0.1) * 2, (y - r * 0.5) * 2], fill=tc)
            d.ellipse([(x + r * 0.1) * 2, (y - r * 1.7) * 2, (x + r * 1.2) * 2, (y - r * 0.6) * 2], fill=tc)
            for a, q in spots:
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
        ("coin", spr_coin()),
        ("coin_back", spr_coin(True)),
        ("coin_rim", spr_coin_rim()),
        ("pw_magnet", spr_power("magnet")),
        ("pw_wide", spr_power("wide")),
        ("pw_shield", spr_power("shield")),
        ("pw_time", spr_power("time")),
        ("ic_note", spr_icon("note")),
    ] + orchard_sprites()
    write_sheet("sprites", sprites)
    write_sheet("logo", [("logo", spr_logo())])
    build_font()

    # One sheet per scene and board: only the chosen one is ever in memory.
    for i in range(6):
        sky, far, mid, near = bg_top(i, hill_layer, v_gradient, spr_soft)
        write_sheet(f"bgtop{i}", [("sky", sky), ("far", far), ("mid", mid), ("near", near)], fmt="rgba5551")
    for i, b in enumerate(bg_boards()):
        write_sheet(f"board{i}", [("board", b)], fmt="rgb565")


def build_font():
    # Luminance + alpha keeps the white fill tintable and the dark outline dark, at half the memory.
    glyphs = font_glyphs()
    write_sheet("font", [(f"g{i:02d}", g[1]) for i, g in enumerate(glyphs)] + [(f"f{i:02d}", g[3]) for i, g in enumerate(glyphs)], fmt="la8")
    index = [-1] * 96
    adv = [0] * 96
    for i, (ch, _, a, _f) in enumerate(glyphs):
        index[ord(ch) - 32] = i
        adv[ord(ch) - 32] = int(round(a))
    adv[0] = 12
    with open(path("source", "font_metrics.h"), "w", newline="\n") as f:
        f.write("// Generated by tools/gen_assets.py. Do not edit.\n#pragma once\n")
        f.write(f"#define FONT_CELL {FONT_CELL}\n#define FONT_CAP {FONT_SIZE}\n")
        f.write(f"#define FONT_FILL {len(glyphs)}  // the fill-only copy of glyph i is at i + FONT_FILL\n")
        f.write("static const signed char kFontIndex[96] = {" + ",".join(map(str, index)) + "};\n")
        f.write("static const unsigned char kFontAdvance[96] = {" + ",".join(map(str, adv)) + "};\n")


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    if what in ("all", "gfx"):
        build_graphics()
        meta_art()
    if what == "font":
        build_font()
    if what in ("all", "audio"):
        import gen_audio
        gen_audio.build_audio()
    print("assets generated")
