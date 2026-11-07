#!/usr/bin/env python3
"""Generates the low-poly 3D fruit from code: no third-party models.

Writes romfs/models/fruit_hi.bin and fruit_lo.bin. Each mesh has three parts: whole, left half
and right half, the halves being the whole fruit cut by the plane x = 0 with a coloured cut face.

File format (little endian):
  "FSM1", u32 mesh count (always 3 parts per mesh)
  for every part: u32 vertex count, u32 index count, u32 body index count, u32 flesh RGBA, u32 rim RGBA
  then for every part: its vertices, then its u16 indices (padded to 4 bytes)
A vertex is 20 bytes: float x, y, z; s8 nx, ny, nz, 0; u8 r, g, b, a.
Units are screen pixels, +y is down the screen and +z points at the viewer.

Requires numpy. Output is deterministic.
"""
import math
import os
import struct

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
LOD = {"hi": (14, 20), "lo": (8, 12)}


def lerp3(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def clamp01(v):
    return max(0.0, min(1.0, v))


class Mesh:
    """An indexed triangle list with per-vertex normal and colour."""

    def __init__(self):
        self.v = []
        self.i = []
        self.body = 0  # indices of the fruit itself, before stems and leaves

    def add_vertex(self, p, n, c):
        self.v.append((tuple(p), tuple(n), tuple(c)))
        return len(self.v) - 1

    def add_tri(self, a, b, c):
        self.i += [a, b, c]

    def add(self, other):
        base = len(self.v)
        self.v += other.v
        self.i += [k + base for k in other.i]


def surface(lat, lon, radius, color):
    """A closed, smooth-shaded surface. radius(theta, phi) gives the distance from the origin;
    theta runs 0 (top, -y) to pi (bottom) and phi runs round the vertical axis."""

    def point(th, ph):
        r = radius(th, ph)
        return np.array((math.sin(th) * math.cos(ph) * r, -math.cos(th) * r, math.sin(th) * math.sin(ph) * r))

    return grid(lat, lon, point, color)


def grid(lat, lon, point, color):
    """The same closed surface as surface(), from any point(theta, phi), for shapes that aren't star-shaped."""
    m = Mesh()
    eps = 0.012
    ids = {}
    for a in range(lat + 1):
        th = a / lat * math.pi
        for b in range(lon):
            if (a == 0 or a == lat) and b > 0:
                continue
            ph = b / lon * math.tau
            p = point(th, ph)
            tlo, thi = max(th - eps, 0.001), min(th + eps, math.pi - 0.001)
            n = np.cross(point(tlo, ph) - point(thi, ph), point(max(th, 0.02), ph + eps) - point(max(th, 0.02), ph - eps))
            ln = np.linalg.norm(n)
            n = n / ln if ln > 1e-9 else p / (np.linalg.norm(p) or 1)
            if np.dot(n, p) < 0:
                n = -n
            ids[(a, b)] = m.add_vertex(p, n, color(th, ph))

    def vid(a, b):
        return ids[(a, 0)] if a == 0 or a == lat else ids[(a, b % lon)]

    for a in range(lat):
        for b in range(lon):
            p, q, r, s = vid(a, b), vid(a, b + 1), vid(a + 1, b), vid(a + 1, b + 1)
            if a > 0:
                m.add_tri(p, q, r)
            if a < lat - 1:
                m.add_tri(q, s, r)
    m.body = len(m.i)
    return m


def prism(a, b, ra, rb, color, sides=5):
    """A tapered bar from a to b, used for stems, fuses and crowns."""
    a, b = np.array(a, float), np.array(b, float)
    d = b - a
    d /= np.linalg.norm(d) or 1
    u = np.cross(d, (0.3, 0.2, 1.0))
    u /= np.linalg.norm(u) or 1
    w = np.cross(d, u)
    m = Mesh()
    for k in range(sides):
        t = k / sides * math.tau
        o = u * math.cos(t) + w * math.sin(t)
        m.add_vertex(a + o * ra, o, color)
        m.add_vertex(b + o * rb, o, color)
    for k in range(sides):
        p, q, r, s = 2 * k, 2 * k + 1, (2 * k + 2) % (2 * sides), (2 * k + 3) % (2 * sides)
        m.add_tri(p, r, q)
        m.add_tri(q, r, s)
    tip = m.add_vertex(b, d, color)
    for k in range(sides):
        m.add_tri(2 * k + 1, (2 * k + 3) % (2 * sides), tip)
    return m


def leaf(base, angle, tilt, length, width, color):
    """A two-sided folded leaf."""
    base = np.array(base, float)
    d = np.array((math.cos(angle) * math.cos(tilt), -math.sin(tilt), math.sin(angle) * math.cos(tilt)))
    side = np.array((-math.sin(angle), 0, math.cos(angle)))
    up = np.cross(d, side)
    tip = base + d * length
    mid = base + d * length * 0.5 + up * length * 0.1
    dark = tuple(c * 0.8 for c in color)
    m = Mesh()
    for s in (1, -1):
        e = mid + side * width * s
        nrm = (up + side * s * 0.4)
        nrm = nrm / np.linalg.norm(nrm)
        a, b, c, f = m.add_vertex(base, nrm, dark), m.add_vertex(e, nrm, color), m.add_vertex(tip, nrm, color), m.add_vertex(mid, nrm, dark)
        m.add_tri(a, b, f)
        m.add_tri(f, b, c)
        m.add_tri(a, f, b)
        m.add_tri(f, c, b)
    return m


# ---------------------------------------------------------------- fruit
def tri_shade(colors, t):
    t = clamp01(t) * (len(colors) - 1)
    k = min(int(t), len(colors) - 2)
    return lerp3(colors[k], colors[k + 1], t - k)


def apple(lat, lon):
    def rad(th, ph):
        r = 22 + 2.0 * math.sin(th)
        return r * (1 - 0.22 * math.exp(-(th / 0.55) ** 2) - 0.10 * math.exp(-((math.pi - th) / 0.45) ** 2))

    def col(th, ph):
        blush = 0.5 + 0.5 * math.sin(ph + 0.6)
        return lerp3((226, 44, 48), (250, 120, 60), clamp01(blush * (th / math.pi) ** 0.7))

    m = surface(lat, lon, rad, col)
    m.add(prism((0, -15, 0), (2.5, -27, 0.5), 1.5, 1.0, (96, 62, 36)))
    m.add(leaf((1.5, -21, 0), 0.5, 0.45, 15, 4.2, (84, 170, 72)))
    return m


def orange(lat, lon):
    def rad(th, ph):
        return 22.5 + 0.35 * math.sin(th * 9) * math.sin(ph * 11) * math.sin(th)

    def col(th, ph):
        return lerp3((255, 160, 34), (240, 118, 20), th / math.pi)

    m = surface(lat, lon, rad, col)
    m.add(prism((0, -21.5, 0), (0.5, -24.5, 0), 1.5, 1.2, (96, 110, 50)))
    return m


def lemon(lat, lon):
    def rad(th, ph):
        # Stretched along the screen, with the classic nipple at both ends.
        s = math.sin(th)
        e = abs(math.cos(th))
        return 17.5 + 8.0 * e ** 2 + 5.5 * math.exp(-(s / 0.2) ** 2)

    def col(th, ph):
        return lerp3((255, 234, 76), (246, 206, 40), th / math.pi)

    return surface(lat, lon, rad, col)


def pear(lat, lon):
    def rad(th, ph):
        t = th / math.pi
        return 11.5 + 12.5 * (0.5 - 0.5 * math.cos(t * math.pi)) ** 1.2 + (2.5 if t > 0.3 else 2.5 * t / 0.3)

    def col(th, ph):
        return lerp3((186, 214, 90), (226, 216, 92), th / math.pi)

    m = surface(lat, lon, rad, col)
    m.add(prism((0, -13, 0), (1.5, -23, 0), 1.3, 0.9, (96, 62, 36)))
    m.add(leaf((1, -17.5, 0), 2.6, 0.35, 11, 3.2, (84, 170, 72)))
    return m


def plum(lat, lon):
    def rad(th, ph):
        return 21 * (1 + 0.12 * math.cos(th) ** 2) * (1 - 0.05 * math.exp(-((ph % math.tau) - 3.1) ** 2 * 4))

    def col(th, ph):
        return lerp3((126, 48, 148), (80, 28, 112), th / math.pi)

    return surface(lat, lon, rad, col)


def melon(lat, lon):
    def rad(th, ph):
        return 24.5 * (1 + 0.07 * math.cos(th) ** 2)

    def col(th, ph):
        # Dark stripes running pole to pole; hi-detail meshes resolve these best.
        stripe = 0.5 + 0.5 * math.cos(ph * 5 + math.sin(th * 4) * 0.5)
        return lerp3((58, 128, 62), (32, 88, 44), stripe ** 2)

    return surface(lat, lon, rad, col)


def kiwi(lat, lon):
    def rad(th, ph):
        return 20.5 * (1 + 0.1 * math.sin(th) ** 2)

    def col(th, ph):
        return lerp3((136, 98, 58), (112, 80, 46), 0.5 + 0.5 * math.sin(ph * 7 + th * 9))

    return surface(lat, lon, rad, col)


def berry(lat, lon):
    def rad(th, ph):
        t = th / math.pi
        return 13 + 11 * math.sin(min(t * 1.5, 1) * math.pi / 2) * (1.05 - t * 0.6)

    def col(th, ph):
        seed = math.sin(ph * 9 + (th * 7 // 1) * 1.6) * math.sin(th * 14)
        return (255, 222, 120) if seed > 0.82 else lerp3((236, 34, 58), (196, 22, 44), th / math.pi)

    m = surface(lat, lon, rad, col)
    for k in range(5):
        m.add(leaf((0, -13.5, 0), k / 5 * math.tau, 0.1, 10, 3.0, (84, 170, 72)))
    return m


def peach(lat, lon):
    def rad(th, ph):
        cleft = 0.09 * math.exp(-(math.sin((ph - 1.57) / 2) / 0.16) ** 2) * math.sin(th) ** 0.5
        return 22.5 * (1 - cleft) * (1 - 0.1 * math.exp(-(th / 0.42) ** 2))

    def col(th, ph):
        return lerp3((255, 178, 112), (242, 104, 88), clamp01(0.5 + 0.5 * math.sin(ph + 1.1) * math.sin(th)))

    m = surface(lat, lon, rad, col)
    m.add(leaf((0, -20, 0), 0.7, 0.45, 12, 3.6, (84, 170, 72)))
    return m


def pineapple(lat, lon):
    def rad(th, ph):
        t = th / math.pi
        bump = 0.55 * math.sin(ph * 8 + t * 22) * math.sin(ph * 8 - t * 22) * math.sin(th)
        return (19 + 3.0 * math.sin(th)) * (1 + 0.3 * math.cos(th) ** 2) + bump

    def col(th, ph):
        diamond = math.sin(ph * 8 + th * 7) * math.sin(ph * 8 - th * 7)
        return (190, 118, 30) if diamond > 0.75 else lerp3((244, 180, 50), (222, 148, 40), th / math.pi)

    m = surface(lat, lon, rad, col)
    for k in range(7):
        ang = k / 7 * math.tau
        m.add(leaf((0, -24, 0), ang, 1.0 + 0.2 * (k % 2), 15 + 3 * (k % 2), 3.2, (66, 156, 70)))
    return m


def coconut(lat, lon):
    def rad(th, ph):
        return 22 + 0.4 * math.sin(ph * 13) * math.sin(th * 11) * math.sin(th)

    def col(th, ph):
        fibre = 0.5 + 0.5 * math.sin(ph * 17 + th * 5)
        c = lerp3((100, 68, 44), (66, 42, 28), fibre)
        if th < 0.32:
            d = min(abs((ph % (math.tau / 3)) - math.pi / 3), 1.0)
            if d < 0.3:
                return (38, 24, 18)
        return c

    return surface(lat, lon, rad, col)


def gold(lat, lon):
    def rad(th, ph):
        return (19 + 7 * abs(math.cos(ph * 2.5))) * (0.72 + 0.28 * math.sin(th))

    def col(th, ph):
        return lerp3((255, 230, 96), (240, 170, 34), th / math.pi)

    return surface(lat, lon, rad, col)


def bomb(lat, lon):
    def col(th, ph):
        return (44, 40, 62)

    m = surface(lat, lon, lambda th, ph: 19.0, col)
    m.add(prism((0, -17, 0), (0, -22, 0), 5.5, 5.5, (86, 84, 102), 8))
    m.add(prism((0, -22, 0), (5, -29, 1), 1.2, 1.0, (190, 170, 130)))
    # Red warning stars on both faces, so that a bomb reads clearly at a glance.
    for s in (1, -1):
        cross = prism((-8, 0, 17.5 * s), (8, 0, 17.5 * s), 2.0, 2.0, (232, 56, 48), 4)
        m.add(cross)
        cross = prism((0, -8, 17.5 * s), (0, 8, 17.5 * s), 2.0, 2.0, (232, 56, 48), 4)
        m.add(cross)
    return m


def sunburst(lat, lon):
    def rad(th, ph):
        return (41 + 4.5 * math.cos(ph * 10) * math.sin(th)) * (0.82 + 0.18 * math.sin(th))

    def col(th, ph):
        ridge = 0.5 + 0.5 * math.cos(ph * 10)
        return lerp3((214, 62, 88), (255, 160, 62), clamp01(ridge * 0.9 + 0.3 * math.sin(th) ** 8))

    m = surface(lat + 4, lon + 8, rad, col)
    for k in range(6):
        m.add(leaf((0, -33, 0), k / 6 * math.tau, 0.7, 22, 5.5, (72, 170, 92)))
    return m


# ---------------------------------------------------------------- fruit added in 1.0
def deform(mesh, f):
    """Moves every vertex through f, turning normals with it (by the inverse transpose of f's slope)."""
    out = Mesh()
    out.i = list(mesh.i)
    out.body = mesh.body
    h = 0.01
    for p, n, c in mesh.v:
        p = np.array(p, float)
        jac = np.column_stack([(f(p + e * h) - f(p - e * h)) / (2 * h) for e in np.eye(3)])
        nn = np.linalg.inv(jac).T @ np.array(n, float)
        nn /= np.linalg.norm(nn) or 1
        out.v.append((tuple(f(p)), tuple(nn), c))
    return out


def turn_z(a):
    ca, sa = math.cos(a), math.sin(a)
    return lambda p: np.array((p[0] * ca - p[1] * sa, p[0] * sa + p[1] * ca, p[2]))


def turn_y(a):
    ca, sa = math.cos(a), math.sin(a)
    return lambda p: np.array((p[0] * ca + p[2] * sa, p[1], -p[0] * sa + p[2] * ca))


def centred(mesh):
    """Moves the fruit so that its body sits round the origin, which is what it spins about."""
    pts = np.array([mesh.v[k][0] for k in mesh.i[:mesh.body]])
    mid = (pts.min(axis=0) + pts.max(axis=0)) / 2
    return deform(mesh, lambda p: p - mid)


def ball(centre, r, lat, lon, color):
    m = surface(lat, lon, lambda th, ph: r, color)
    return deform(m, lambda p: p + np.array(centre, float))


def pod(lat, lon, half, radial, color):
    """A long fruit round the y axis: radial(t, phi) is its thickness, t running 0 (top) to 1."""
    def point(th, ph):
        r = radial(th / math.pi, ph) * math.sin(th) ** 0.7
        return np.array((math.cos(ph) * r, -math.cos(th) * half, math.sin(ph) * r))

    return grid(lat, lon + 2, point, lambda th, ph: color(th / math.pi, ph))


def flap(m, base1, base2, tip, c0, c1):
    """A thin two-sided scale, coloured from c0 at its root to c1 at its tip."""
    base1, base2, tip = (np.array(p, float) for p in (base1, base2, tip))
    nrm = np.cross(base2 - base1, tip - base1)
    nrm /= np.linalg.norm(nrm) or 1
    for s in (1, -1):
        a, b, c = m.add_vertex(base1, nrm * s, c0), m.add_vertex(base2, nrm * s, c0), m.add_vertex(tip, nrm * s, c1)
        m.add_tri(a, b, c) if s > 0 else m.add_tri(a, c, b)


def banana(lat, lon):
    def radial(t, ph):
        return 8.6 * (1 + 0.07 * math.cos(5 * ph)) * (1 - 0.25 * t)

    def col(t, ph):
        if t < 0.05:
            return (136, 140, 60)
        if t > 0.95:
            return (90, 66, 40)
        return tri_shade([(232, 210, 70), (252, 218, 66), (246, 204, 54)], t)

    m = pod(lat, lon, 27, radial, col)
    m.add(prism((0, -25.5, 0), (0, -32.5, 0.5), 2.4, 1.8, (128, 136, 62)))
    m.add(prism((0, -32, 0.5), (0, -34, 0.5), 1.8, 1.4, (96, 78, 44), 4))
    m = deform(m, lambda p: np.array((-p[1], p[0], p[2])))  # lie it down, stem to the right
    m = deform(m, lambda p: np.array((p[0], p[1] - 0.021 * p[0] ** 2, p[2])))
    return centred(deform(m, turn_z(-0.18)))


def mango(lat, lon):
    def rad(th, ph):
        d = (math.sin(th) * math.cos(ph), -math.cos(th), math.sin(th) * math.sin(ph))
        r = 1 / math.sqrt((d[0] / 19) ** 2 + (d[1] / 23) ** 2 + (d[2] / 17) ** 2)
        return r * (1 - 0.06 * math.exp(-((d[0] + 0.9) / 0.5) ** 2))  # the kidney curve on one side

    def col(th, ph):
        x, y = math.sin(th) * math.cos(ph), -math.cos(th)
        blush = clamp01((x - y) * 0.75)
        sun = clamp01((y - x) * 0.7)
        c = lerp3((252, 156, 46), (226, 54, 50), blush)
        return lerp3(c, (255, 214, 64), sun)

    m = surface(lat, lon, rad, col)
    m.add(prism((0, -21.5, 0), (1, -26, 0), 1.6, 1.2, (110, 76, 40)))
    m.add(leaf((1, -24.5, 0), 0.4, 0.45, 14, 4.0, (84, 170, 72)))
    return deform(m, turn_z(0.42))


def cherry(lat, lon):
    a, c = 11.5, 9.5  # two cherries, pinched where they touch

    def rad(th, ph):
        u, s = abs(math.cos(th)), math.sin(th)
        return c * u + math.sqrt(max(a * a - c * c * s * s, 0.0))

    def col(th, ph):
        # The dimple at each stem, then a deep red that darkens underneath.
        end = abs(math.cos(th))
        side = math.sin(th) * math.cos(ph)
        dark = clamp01(0.5 + 0.5 * side)
        return lerp3((214, 26, 48), (150, 10, 32), dark * 0.8) if end < 0.97 else (110, 8, 26)

    m = surface(lat + 2, lon, rad, col)
    m = deform(m, lambda p: np.array((-p[1], p[0], p[2])))  # the pair side by side along x
    for x in (-c, c):
        m.add(prism((x, -a + 2, 0), (1.5, -34, 0), 1.0, 0.8, (108, 140, 56), 4))
    m.add(leaf((1.5, -33, 0), 0.3, 0.3, 12, 3.6, (84, 170, 72)))
    m = deform(m, turn_y(1.15))  # mostly front to back, so a cut goes through both cherries
    return centred(deform(m, turn_z(0.22)))


def dragon(lat, lon):
    def rad(th, ph):
        d = (math.sin(th), -math.cos(th))
        return 1 / math.sqrt((d[0] / 17) ** 2 + (d[1] / 22) ** 2)

    def col(th, ph):
        return lerp3((236, 48, 136), (198, 28, 112), th / math.pi)

    def at(th, ph, grow=1.0):
        r = rad(th, ph) * grow
        return (math.sin(th) * math.cos(ph) * r, -math.cos(th) * r, math.sin(th) * math.sin(ph) * r)

    m = surface(lat, lon, rad, col)
    rows = 4 if lat < 10 else 5
    per = 5 if lat < 10 else 7
    for k in range(rows):
        th = 0.75 + k * (2.0 / (rows - 1)) * 0.95
        for j in range(per):
            ph = (j + 0.5 * (k % 2)) / per * math.tau
            flap(m, at(th + 0.12, ph - 0.28), at(th + 0.12, ph + 0.28), at(th - 0.42, ph, 1.28), (240, 76, 150), (140, 204, 74))
    for j in range(4):
        ph = j / 4 * math.tau + 0.4
        flap(m, at(0.32, ph - 0.6), at(0.32, ph + 0.6), at(0.05, ph, 1.38), (240, 76, 150), (140, 204, 74))
    return m


def spiky(lat, lon):
    lo = lat < 10
    m = surface(7 if lo else 11, 10 if lo else 16, lambda th, ph: 17.5 + 0.6 * math.sin(th * 6) * math.sin(ph * 5),
                lambda th, ph: lerp3((48, 104, 56), (30, 70, 40), clamp01(0.5 + 0.5 * math.sin(ph * 3 + th * 4)) * (th / math.pi + 0.3)))
    count = 16 if lo else 24
    golden = math.pi * (3 - math.sqrt(5))
    for k in range(count):
        y = 1 - (k + 0.5) / count * 2
        r = math.sqrt(1 - y * y)
        d = np.array((math.cos(golden * k) * r, y, math.sin(golden * k) * r))
        m.add(prism(d * 15.5, d * 29, 3.4, 0.0, (232, 226, 178), 3 if lo else 4))
    return m


def grapes(lat, lon):
    lo = lat < 10
    m = surface(3 if lo else 4, 5 if lo else 6, lambda th, ph: 10 * (1.15 - 0.45 * th / math.pi) * (1 + 0.5 * math.cos(th) ** 2),
                lambda th, ph: (70, 28, 100))
    glat, glon = (3, 4) if lo else (4, 6)
    rows = ((-15, 10.5, 5), (-4, 9.5, 4), (7, 7, 3), (17, 0, 1)) if lo else ((-16, 11, 5), (-6, 10, 5), (4.5, 7.5, 4), (14, 4, 2), (21, 0, 1))
    for row, (y, ring, n) in enumerate(rows):
        for k in range(n):
            ph = (k + 0.5 * (row % 2)) / n * math.tau
            shade_k = 0.86 + 0.14 * ((k + row) % 2)
            m.add(ball((math.cos(ph) * ring, y, math.sin(ph) * ring), 6.8, glat, glon,
                       lambda th, ph2, s=shade_k: lerp3((140, 72, 186), (92, 40, 136), th / math.pi * 0.8 + 0.2 * (1 - s) * 5)))
    m.body = len(m.i)  # the core and every grape: all of them get cut faces
    m.add(prism((0, -20, 0), (-1.5, -30, 0), 1.4, 1.0, (102, 80, 44), 4))
    m.add(leaf((-1, -26, 0), 0.6, 0.3, 17, 5.5, (84, 170, 72)))
    return centred(m)


def grape(lat, lon):
    m = surface(max(4, lat - 2), max(8, lon - 2), lambda th, ph: 11.5 * (1 + 0.06 * math.cos(th) ** 2),
                lambda th, ph: lerp3((140, 72, 186), (96, 42, 140), th / math.pi))
    m.add(prism((0, -11, 0), (1, -16, 0), 1.1, 0.8, (102, 80, 44), 4))
    return m


def honey(lat, lon):
    def edge(ph):
        return 1.0 + 0.14 * math.sin(ph * 5 + 0.6)

    glaze = (238, 126, 14)

    def col(th, ph):
        if th < edge(ph):
            return lerp3((255, 182, 56), glaze, clamp01(th / 0.6))
        return lerp3((240, 196, 102), (206, 148, 60), th / math.pi)

    R = 22.5
    m = surface(lat, lon, lambda th, ph: R + (0.6 if th < edge(ph) else 0), col)
    lo = lat < 10

    def at(th, ph, r):
        return np.array((math.sin(th) * math.cos(ph), -math.cos(th), math.sin(th) * math.sin(ph))) * r

    # Drips of honey running down from the glaze: ribbons lying on the skin, each ending in a bead.
    for ph, length in ((0.3, 0.75), (1.6, 0.45), (2.9, 0.9), (4.4, 0.6), (5.4, 0.35))[: 4 if lo else 5]:
        th0 = edge(ph) - 0.15
        side = np.array((-math.sin(ph), 0, math.cos(ph)))
        segs = 3 if lo else 4
        prev = None
        for k in range(segs + 1):
            th = th0 + length * k / segs
            w = 3.6 - 1.0 * k / segs
            c = at(th, ph, R + 0.7)
            n = c / np.linalg.norm(c)
            pair = (m.add_vertex(c - side * w, n, glaze), m.add_vertex(c + side * w, n, glaze))
            if prev:
                m.add_tri(prev[0], prev[1], pair[0])
                m.add_tri(prev[1], pair[1], pair[0])
            prev = pair
        m.add(ball(at(th0 + length + 0.05, ph, R + 0.4), 3.6, 3, 4 if lo else 6, lambda th, ph2: (246, 146, 26)))
    return m


def jelly(lat, lon):
    def rad(th, ph):
        sag = 1 + 0.1 * max(0.0, -math.cos(th)) * math.sin(th) ** 2
        return 21 * (1 + 0.05 * math.sin(3 * ph + 0.5) * math.sin(th) + 0.035 * math.sin(2 * ph + 1.0) * math.sin(th * 2)) * sag * (1 - 0.08 * max(0.0, math.cos(th)))

    def col(th, ph):
        bubble = math.sin(ph * 4 + 1) * math.sin(th * 5) > 0.85
        if bubble:
            return (236, 255, 252)
        return tri_shade([(60, 200, 198), (120, 230, 220), (255, 150, 200)], (th / math.pi) ** 1.1)

    m = surface(lat, lon, rad, col)
    m.add(prism((0, -18.5, 0), (0.5, -22.5, 0), 1.1, 0.9, (96, 110, 50), 4))
    m.add(leaf((0.5, -21, 0), 0.9, 0.5, 12, 3.6, (84, 170, 72)))
    return m


def chili(lat, lon):
    def radial(t, ph):
        return 8.2 * (1 - t) ** 0.55 + 0.6

    def col(t, ph):
        if t < 0.07:
            return (66, 140, 56)
        return tri_shade([(226, 40, 36), (214, 30, 34), (186, 22, 30)], t)

    m = pod(lat, lon, 24, radial, col)
    m.add(prism((0, -22.5, 0), (-0.5, -28, 0), 1.6, 1.3, (72, 138, 56), 4))
    m.add(prism((-0.5, -28, 0), (2.5, -32, 0), 1.3, 1.1, (72, 138, 56), 4))
    m = deform(m, lambda p: np.array((p[1], -p[0], p[2])))  # stem to the left
    m = deform(m, lambda p: np.array((p[0], p[1] + 12 - 0.02 * (p[0] - 6) ** 2, p[2])))
    return centred(deform(m, turn_z(0.15)))


COIN_GOLD = (246, 196, 62)
COIN_DEEP = (214, 150, 34)
COIN_LIGHT = (255, 232, 140)


def coin(lat, lon):
    """A gold coin facing the viewer, with a raised rim: a profile turned round the z axis."""
    prof = [  # (r, z) from the middle of the face, round the edge, to the middle of the back
        ((0, 2.0), (15.5, 2.0), COIN_GOLD), ((15.5, 2.0), (16.8, 3.2), COIN_LIGHT), ((16.8, 3.2), (20, 3.2), COIN_LIGHT),
        ((20, 3.2), (21.5, 2.2), COIN_GOLD), ((21.5, 2.2), (21.5, -2.2), COIN_DEEP), ((21.5, -2.2), (20, -3.2), COIN_GOLD),
        ((20, -3.2), (16.8, -3.2), COIN_LIGHT), ((16.8, -3.2), (15.5, -2.0), COIN_LIGHT), ((15.5, -2.0), (0, -2.0), COIN_GOLD),
    ]
    m = Mesh()
    segs = lon
    for (r0, z0), (r1, z1), c in prof:
        dr, dz = r1 - r0, z1 - z0
        ln = math.hypot(dr, dz)
        nr, nz = -dz / ln, dr / ln
        ring0, ring1 = [], []
        for k in range(segs):
            a = k / segs * math.tau
            ca, sa = math.cos(a), math.sin(a)
            n = (nr * ca, nr * sa, nz)
            ring0.append(m.add_vertex((r0 * ca, r0 * sa, z0), n, c))
            ring1.append(m.add_vertex((r1 * ca, r1 * sa, z1), n, c))
        for k in range(segs):
            j = (k + 1) % segs
            # Wound to face outwards, like the fruit.
            if r0 > 0:
                m.add_tri(ring0[k], ring1[k], ring0[j])
            if r1 > 0:
                m.add_tri(ring0[j], ring1[k], ring1[j])
    m.body = len(m.i)
    return m


# name, builder, flesh, rim
FRUIT = [
    ("apple", apple, (255, 240, 190), (246, 216, 150)),
    ("orange", orange, (255, 176, 66), (255, 232, 180)),
    ("lemon", lemon, (252, 240, 130), (255, 250, 214)),
    ("pear", pear, (248, 246, 196), (226, 232, 150)),
    ("plum", plum, (240, 190, 90), (214, 110, 110)),
    ("melon", melon, (238, 66, 82), (244, 244, 190)),
    ("kiwi", kiwi, (150, 208, 80), (214, 232, 170)),
    ("berry", berry, (252, 120, 140), (255, 196, 200)),
    ("peach", peach, (255, 214, 130), (250, 150, 110)),
    ("pineapple", pineapple, (255, 232, 110), (255, 246, 190)),
    ("coconut", coconut, (250, 250, 244), (150, 110, 80)),
    ("gold", gold, (255, 236, 120), (255, 250, 200)),
    ("bomb", bomb, (60, 56, 80), (44, 40, 62)),
    ("sunburst", sunburst, (255, 196, 80), (250, 104, 70)),
    ("banana", banana, (252, 246, 214), (246, 224, 120)),
    ("mango", mango, (255, 172, 30), (255, 208, 96)),
    ("cherry", cherry, (168, 20, 44), (130, 12, 32)),
    ("dragon", dragon, (252, 250, 248), (226, 52, 132)),
    ("spiky", spiky, (250, 238, 160), (70, 122, 62)),
    ("grapes", grapes, (206, 234, 150), (108, 50, 140)),
    ("grape", grape, (206, 234, 150), (108, 50, 140)),
    ("honey", honey, (255, 146, 46), (246, 206, 110)),
    ("jelly", jelly, (255, 196, 222), (120, 220, 214)),
    ("chili", chili, (250, 100, 52), (190, 26, 30)),
    ("coin", coin, COIN_GOLD, COIN_DEEP),
]


# ---------------------------------------------------------------- pre-split halves
def chain_loops(segs):
    """Joins cut segments end to end into closed outlines. A fruit made of several pieces
    (a grape bunch, a cherry pair cut through both) gives one outline per piece."""
    def key(q):
        return (round(q[0], 3), round(q[1], 3), round(q[2], 3))

    ends = {}
    for k, (a, b) in enumerate(segs):
        ends.setdefault(key(a), []).append(k)
        ends.setdefault(key(b), []).append(k)
    used = [False] * len(segs)
    loops = []
    for start in range(len(segs)):
        if used[start]:
            continue
        used[start] = True
        first, cur = segs[start]
        loop = [first, cur]
        while True:
            nxt = next((k for k in ends.get(key(cur), ()) if not used[k]), None)
            if nxt is None:
                break
            used[nxt] = True
            a, b = segs[nxt]
            cur = b if key(a) == key(cur) else a
            if key(cur) == key(first):
                break
            loop.append(cur)
        if len(loop) >= 2:
            loops.append(loop)
    # Join outlines that stop short of each other (a seam that doesn't quite meet).
    merged = True
    while merged:
        merged = False
        for i in range(len(loops)):
            for j in range(len(loops)):
                if i == j:
                    continue
                a, b = loops[i], loops[j]
                if np.linalg.norm(np.array(a[0]) - np.array(a[-1])) < 1.0:
                    continue  # already closed
                for cand in (b, b[::-1]):
                    if np.linalg.norm(np.array(a[-1]) - np.array(cand[0])) < 1.0:
                        loops[i] = a + list(cand)
                        del loops[j]
                        merged = True
                        break
                if merged:
                    break
            if merged:
                break
    return [np.array(lp) for lp in loops if len(lp) >= 3]


def cap_ring(out, ring, side, flesh, rim, lift):
    """One cut face: rind ring, flesh, and a lighter centre, fanned from the outline's middle."""
    keep = [ring[0]]
    for q in ring[1:]:
        if np.linalg.norm(q - keep[-1]) > 0.6:
            keep.append(q)
    if len(keep) > 3 and np.linalg.norm(keep[-1] - keep[0]) <= 0.6:
        keep.pop()
    if len(keep) < 3:
        return
    ring = np.array(keep)
    centre = ring.mean(axis=0)
    # Same turning direction for every outline: angle round the middle, measured in (z, y), rising.
    dz, dy = ring[:, 2] - centre[2], ring[:, 1] - centre[1]
    if np.sum(dz * np.roll(dy, -1) - np.roll(dz, -1) * dy) < 0:
        ring = ring[::-1]
    normal = (-side, 0.0, 0.0)
    push = np.array(normal) * lift
    mid = out.add_vertex(tuple(centre + push), normal, tuple(c * 1.0 + (255 - c) * 0.35 for c in flesh))
    outer = [out.add_vertex(tuple(q + push), normal, rim) for q in ring]
    inner = [out.add_vertex(tuple(centre + (q - centre) * 0.86 + push), normal, flesh) for q in ring]
    count = len(ring)
    for k in range(count):
        n = (k + 1) % count
        out.add_tri(outer[k], inner[k], outer[n])
        out.add_tri(outer[n], inner[k], inner[n])
        out.add_tri(inner[k], mid, inner[n])


def outline_area(ring):
    c = ring.mean(axis=0)
    dz, dy = ring[:, 2] - c[2], ring[:, 1] - c[1]
    return abs(np.sum(dz * np.roll(dy, -1) - np.roll(dz, -1) * dy)) / 2


def split(mesh, side, flesh, rim):
    """Keeps the part of the mesh on one side of x = 0 and closes every piece it cuts with a face."""
    out = Mesh()
    cache = {}

    def emit(p, n, c):
        key = (round(p[0], 3), round(p[1], 3), round(p[2], 3), round(n[0], 2), round(n[1], 2), round(n[2], 2), c)
        if key not in cache:
            cache[key] = out.add_vertex(p, n, c)
        return cache[key]

    segs = []
    for t in range(0, len(mesh.i), 3):
        tri = [mesh.v[mesh.i[t + k]] for k in range(3)]
        # The plane sits a hair off x = 0, so no point (a fruit's top or bottom) lies exactly on it
        # and breaks the outline in two.
        d = [side * v[0][0] + 1e-4 for v in tri]
        poly = []
        cuts = []
        for k in range(3):
            a, b = tri[k], tri[(k + 1) % 3]
            da, db = d[k], d[(k + 1) % 3]
            if da >= 0:
                poly.append(a)
            if (da > 0) != (db > 0) and abs(da - db) > 1e-9:
                # Worked out from the same end every time, so neighbouring triangles agree exactly.
                (a2, d2), (b2, e2) = sorted(((a, da), (b, db)), key=lambda q: q[0][0])
                f = d2 / (d2 - e2)
                p = tuple(a2[0][j] + (b2[0][j] - a2[0][j]) * f for j in range(3))
                nv = tuple(a2[1][j] + (b2[1][j] - a2[1][j]) * f for j in range(3))
                c = tuple(a2[2][j] + (b2[2][j] - a2[2][j]) * f for j in range(3))
                poly.append((p, nv, c))
                cuts.append(p)
        for k in range(1, len(poly) - 1):
            out.add_tri(*[emit(*poly[j]) for j in (0, k, k + 1)])
        if t < mesh.body and len(cuts) == 2:  # stems and leaves stay out of the faces
            segs.append(tuple(np.array(q) for q in cuts))

    # Biggest face first; smaller ones sit a hair in front so overlapping pieces don't flicker.
    loops = sorted(chain_loops(segs), key=outline_area, reverse=True)
    for rank, ring in enumerate(loops):
        cap_ring(out, ring, side, flesh, rim, 0.04 * rank)
    return out


def rgba(c, a=255):
    return int(c[0]) | int(c[1]) << 8 | int(c[2]) << 16 | a << 24


def pack(mesh):
    b = bytearray()
    for p, n, c in mesh.v:
        ln = math.sqrt(sum(x * x for x in n)) or 1
        sn = [max(-127, min(127, int(round(x / ln * 127)))) for x in n]
        b += struct.pack("<3f4b4B", p[0], p[1], p[2], sn[0], sn[1], sn[2], 0, *[max(0, min(255, int(x))) for x in c], 255)
    data = struct.pack(f"<{len(mesh.i)}H", *mesh.i)
    if len(data) % 4:
        data += b"\0\0"
    return bytes(b) + data


def build_models():
    folder = os.path.join(ROOT, "romfs", "models")
    os.makedirs(folder, exist_ok=True)
    for lod, (lat, lon) in LOD.items():
        header = [b"FSM1", struct.pack("<I", len(FRUIT))]
        body = []
        tris = 0
        for name, fn, flesh, rim in FRUIT:
            whole = fn(lat, lon)
            tris += len(whole.i) // 3
            for part in (whole, split(whole, -1, flesh, rim), split(whole, 1, flesh, rim)):
                header.append(struct.pack("<5I", len(part.v), len(part.i), part.body, rgba(flesh), rgba(rim)))
                body.append(pack(part))
        with open(os.path.join(folder, f"fruit_{lod}.bin"), "wb") as f:
            f.write(b"".join(header) + b"".join(body))
        print(f"{lod}: about {tris // len(FRUIT)} triangles per fruit, {sum(map(len, body)) // 1024} KB")


if __name__ == "__main__":
    build_models()
