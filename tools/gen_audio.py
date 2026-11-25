"""Synthesizes every sound and music track. Nothing here uses recorded or third-party audio.

Cut sounds are layered the way a real cut behaves: a quick blade transient, a burst of tiny
fibre "grains" as flesh tears, a body thump, then juice droplets and spray. Each fruit class
changes the balance of those layers.
"""
import os
import random
import struct
import wave

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SFX_RATE = 32000
MUSIC_RATE = 16000  # the mix rolls off above 3.2 kHz, so this loses nothing and saves memory


def path(*parts):
    p = os.path.join(ROOT, *parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


def write_pcm(name, samples, rate):
    pcm = (np.clip(samples, -1, 1) * 32000).astype("<i2")
    with open(path("romfs", "audio", name + ".pcm"), "wb") as f:
        f.write(b"FSA1" + struct.pack("<II", len(pcm), rate) + pcm.tobytes())


def norm(x, peak=0.9):
    """Peak-normalizes and fades the last 20 ms, so no clip ends in an audible click."""
    x = np.array(x, np.float64)
    fade = min(len(x), 640)
    x[-fade:] *= np.linspace(1, 0, fade) ** 2
    m = np.max(np.abs(x)) or 1
    return x / m * peak


def band(x, lo, hi, rate=SFX_RATE):  # noqa: D401
    """Zero-phase band-pass with soft edges."""
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / rate)
    mask = np.clip((f - lo * 0.7) / (lo * 0.3 + 1), 0, 1) * np.clip((hi * 1.3 - f) / (hi * 0.3 + 1), 0, 1)
    return np.fft.irfft(X * mask, len(x))


def n_of(sec, rate=SFX_RATE):
    return int(sec * rate)


def place(out, sig, at, gain=1.0, rate=SFX_RATE):
    p = int(at * rate)
    if p >= len(out):
        return
    k = min(len(sig), len(out) - p)
    out[p:p + k] += sig[:k] * gain


def exp_env(n, tau, rate=SFX_RATE):
    return np.exp(-np.arange(n) / (tau * rate))


def sine_sweep(f0, f1, n, rate=SFX_RATE):
    return np.sin(2 * np.pi * np.cumsum(np.geomspace(f0, f1, n)) / rate)


# ---------------------------------------------------------------- cut layers
def grains(rng, dur, count, lo, hi, tau=0.0016, spread_power=1.5):
    """Fibres tearing: band-limited noise opened by many tiny, irregular envelopes."""
    n = n_of(dur)
    noise = band(rng.normal(0, 1, n), lo, hi)
    env = np.zeros(n)
    g = exp_env(n_of(tau * 6), tau)
    for _ in range(count):
        at = int((rng.random() ** spread_power) * (n - len(g)))
        env[at:at + len(g)] += g * rng.uniform(0.25, 1.0)
    return noise * env * (1 - np.linspace(0, 1, n)) ** 1.2


def blade_transient(rng, level=1.0):
    """The edge entering: a very short bright tick with a faint metallic ring."""
    n = n_of(0.09)
    tick = band(rng.normal(0, 1, n), 3500, 13000) * exp_env(n, 0.0035)
    t = np.arange(n) / SFX_RATE
    ring = sum(a * np.sin(2 * np.pi * f * t) for f, a in ((3170, 0.6), (4730, 0.45), (6310, 0.35), (8210, 0.2)))
    return (tick + ring * exp_env(n, 0.016) * 0.12) * level


def thump(f0, f1, dur, level):
    n = n_of(dur)
    return sine_sweep(f0, f1, n) * exp_env(n, dur * 0.3) * level


def droplets(rng, dur, count, lo=850, hi=3400):
    """Juice: small rising chirps, like droplets leaving the cut and landing."""
    out = np.zeros(n_of(dur))
    for _ in range(count):
        ln = n_of(rng.uniform(0.006, 0.022))
        f = rng.uniform(lo, hi)
        s = sine_sweep(f, f * rng.uniform(1.15, 1.7), ln) * np.hanning(ln)
        at = rng.random() ** 1.3 * dur
        place(out, s, at, rng.uniform(0.15, 0.7))
    return out


def spray(rng, dur, lo=3000, hi=11000):
    n = n_of(dur)
    return band(rng.normal(0, 1, n), lo, hi) * exp_env(n, dur * 0.28) * np.minimum(1, np.arange(n) / n_of(0.006)) * np.linspace(1, 0, n)


def squelch(rng, dur, f0, f1):
    """A wet, vowel-like squish from noise through a moving resonance."""
    n = n_of(dur)
    src = rng.normal(0, 1, n)
    out = np.zeros(n)
    y1 = y2 = 0.0
    freqs = np.geomspace(f0, f1, n)
    r = 0.985
    for i in range(n):
        c = 2 * r * np.cos(2 * np.pi * freqs[i] / SFX_RATE)
        y = src[i] * 0.05 + c * y1 - r * r * y2
        y2, y1 = y1, y
        out[i] = y
    return out * np.sin(np.linspace(0, np.pi, n)) ** 0.7


def cut_crisp(seed):
    """Apple or pear: dense, snappy crunch with little juice."""
    r = np.random.default_rng(seed)
    out = np.zeros(n_of(0.34))
    place(out, blade_transient(r, 0.5), 0)
    place(out, grains(r, 0.095, 75, 1800, 8000), 0.004, 1.0)
    place(out, grains(r, 0.05, 20, 700, 2400, 0.003), 0.006, 0.7)
    place(out, thump(320, 120, 0.07, 0.45), 0.006)
    place(out, droplets(r, 0.2, 10), 0.05, 0.25)
    place(out, spray(r, 0.14), 0.02, 0.1)
    return norm(out, 0.9)


def cut_juicy(seed):
    """Citrus: a softer tear, then a lot of spray and droplets."""
    r = np.random.default_rng(seed)
    out = np.zeros(n_of(0.44))
    place(out, blade_transient(r, 0.42), 0)
    place(out, grains(r, 0.07, 42, 1200, 5600), 0.004, 0.72)
    place(out, thump(240, 110, 0.07, 0.3), 0.008)
    place(out, spray(r, 0.3), 0.012, 0.34)
    place(out, droplets(r, 0.36, 34), 0.018, 0.5)
    place(out, squelch(r, 0.09, 1100, 520), 0.01, 0.34)
    return norm(out, 0.88)


def cut_soft(seed):
    """Plum, kiwi, berry, peach: almost no crunch, mostly a wet squish."""
    r = np.random.default_rng(seed)
    out = np.zeros(n_of(0.36))
    place(out, blade_transient(r, 0.28), 0)
    place(out, squelch(r, 0.15, 1350, 420), 0.004, 0.72)
    place(out, grains(r, 0.045, 18, 700, 3000, 0.0025), 0.004, 0.42)
    place(out, thump(210, 90, 0.08, 0.34), 0.006)
    place(out, droplets(r, 0.28, 24, 650, 2500), 0.03, 0.5)
    place(out, spray(r, 0.14), 0.02, 0.12)
    return norm(out, 0.86)


def cut_melon(seed):
    """Big fruit: a hollow thump, a long crackle through the rind and plenty of juice."""
    r = np.random.default_rng(seed)
    out = np.zeros(n_of(0.62))
    place(out, blade_transient(r, 0.5), 0)
    place(out, thump(165, 70, 0.2, 0.95), 0.006)
    place(out, grains(r, 0.17, 130, 900, 5200, 0.0022, 1.15), 0.005, 1.0)
    place(out, spray(r, 0.36), 0.05, 0.22)
    place(out, droplets(r, 0.46, 42, 600, 2500), 0.08, 0.5)
    return norm(out, 0.94)


def knock():
    """Blade bouncing off a hard coconut shell: woody resonances and a hard tick."""
    r = np.random.default_rng(30)
    n = n_of(0.2)
    t = np.arange(n) / SFX_RATE
    wood = sum(a * np.sin(2 * np.pi * f * t) * np.exp(-t / d) for f, a, d in ((610, 1.0, 0.03), (1240, 0.6, 0.018), (2210, 0.36, 0.01)))
    out = wood + band(r.normal(0, 1, n), 2000, 9000) * exp_env(n, 0.002) * 0.9
    return norm(out, 0.82)


def shell_break():
    r = np.random.default_rng(31)
    out = np.zeros(n_of(0.5))
    place(out, knock(), 0, 0.8)
    place(out, grains(r, 0.13, 60, 900, 4200, 0.003, 1.1), 0.006, 0.9)
    place(out, thump(190, 80, 0.12, 0.6), 0.004)
    place(out, droplets(r, 0.35, 22, 800, 2600), 0.05, 0.4)
    return norm(out, 0.9)


# ---------------------------------------------------------------- blade and impacts
def swish(seed, dur, f0, f1):
    """Air parted by a blade: a narrow band of noise sweeping as the swing passes."""
    r = np.random.default_rng(seed)
    n = n_of(dur)
    src = r.normal(0, 1, n)
    out = np.zeros(n)
    y1 = y2 = 0.0
    freqs = np.concatenate([np.geomspace(f0, f1, n // 2), np.geomspace(f1, f0 * 0.8, n - n // 2)])
    res = 0.93
    for i in range(n):
        c = 2 * res * np.cos(2 * np.pi * freqs[i] / SFX_RATE)
        y = src[i] * 0.12 + c * y1 - res * res * y2
        y2, y1 = y1, y
        out[i] = y
    return norm(out * np.sin(np.linspace(0, np.pi, n)) ** 1.6, 0.6)


def shing():
    """A fast swing: whoosh plus a thin steel ring that drops slightly in pitch."""
    r = np.random.default_rng(8)
    n = n_of(0.36)
    t = np.arange(n) / SFX_RATE
    ring = sum(a * np.sin(2 * np.pi * np.cumsum(f * (1 - 0.018 * t / t[-1]) * np.ones(n)) / SFX_RATE) for f, a in ((3320, 0.7), (5180, 0.55), (7210, 0.42), (9400, 0.2)))
    env = np.minimum(1, t / 0.012) * np.exp(-t / 0.085)
    out = ring * env * 0.42
    out[:n_of(0.22)] += swish(9, 0.22, 1200, 5200) * 0.8
    out += band(r.normal(0, 1, n), 6000, 13000) * np.exp(-t / 0.05) * 0.1
    return norm(out, 0.62)


def boom():
    r = np.random.default_rng(77)
    n = n_of(1.3)
    t = np.arange(n) / SFX_RATE
    rumble = band(r.normal(0, 1, n), 30, 900) * np.exp(-t / 0.42)
    crack = band(r.normal(0, 1, n), 800, 12000) * np.exp(-t / 0.035)
    sub = sine_sweep(170, 32, n) * np.exp(-t / 0.3)
    debris = grains(r, 0.9, 80, 900, 6000, 0.004, 1.0)
    out = rumble * 1.8 + crack * 1.2 + sub * 1.4
    place(out, debris, 0.08, 0.35)
    return norm(np.tanh(out * 1.5), 0.98)


def fuse_loop():
    """Sizzling fuse. Crossfaded so it loops with no click."""
    r = np.random.default_rng(55)
    n = n_of(1.2)
    x = band(r.normal(0, 1, n * 2), 2500, 11000)
    x *= 0.6 + 0.4 * np.abs(band(r.normal(0, 1, n * 2), 3, 28))/0.5
    fade = np.linspace(0, 1, n // 4)
    head = x[:n].copy()
    head[:len(fade)] = x[:len(fade)] * fade + x[n:n + len(fade)] * (1 - fade)
    pops = grains(r, 1.2, 26, 1500, 7000, 0.002, 1.0)
    out = head + pops[:n] * 1.5
    return out / (np.max(np.abs(out)) or 1) * 0.5


def reverb_tail(x, seconds=0.9, wet=0.25, rate=SFX_RATE, seed=3):
    r = np.random.default_rng(seed)
    n = n_of(seconds, rate)
    # Band-limited, so loud stingers ring like a room and not like white noise.
    ir = band(r.normal(0, 1, n), 180, 3600, rate) * np.exp(-np.arange(n) / (seconds * 0.22 * rate))
    size = len(x) + n
    out = np.fft.irfft(np.fft.rfft(x, size) * np.fft.rfft(ir, size), size)
    out = out / (np.max(np.abs(out)) or 1) * (np.max(np.abs(x)) or 1)
    pad = np.zeros(size)
    pad[:len(x)] = x
    return pad + out * wet


# ---------------------------------------------------------------- tuned voices
def mtof(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def bell(freq, dur, rate=SFX_RATE):
    t = np.arange(n_of(dur, rate)) / rate
    return (np.sin(2 * np.pi * freq * t) + 0.45 * np.sin(2 * np.pi * freq * 2.76 * t) * np.exp(-t * 7) + 0.2 * np.sin(2 * np.pi * freq * 5.4 * t) * np.exp(-t * 14)) * np.exp(-t * 3.6)


def seq(notes, spacing=0.07, length=0.7, voice=bell):
    out = np.zeros(n_of(spacing * len(notes) + length))
    for i, m in enumerate(notes):
        place(out, voice(mtof(m), length), i * spacing)
    return out


def riser(dur, f0, f1, seed=1):
    r = np.random.default_rng(seed)
    n = n_of(dur)
    t = np.linspace(0, 1, n)
    noise = band(r.normal(0, 1, n), f0, f1) * t ** 2.2
    return noise


def cymbal(dur, seed=2):
    r = np.random.default_rng(seed)
    n = n_of(dur)
    return band(r.normal(0, 1, n), 4000, 14000) * np.exp(-np.arange(n) / (dur * 0.3 * SFX_RATE)) * np.linspace(1, 0, n)


def chord_stab(notes, dur):
    out = np.zeros(n_of(dur))
    t = np.arange(len(out)) / SFX_RATE
    for m in notes:
        f = mtof(m)
        for d in (0.997, 1.0, 1.003):
            out += np.sin(2 * np.pi * f * d * t) + 0.3 * np.sin(4 * np.pi * f * d * t)
    return out * np.minimum(1, t / 0.01) * np.exp(-t / (dur * 0.34))


def combo_stinger(tier):
    """Each tier adds a layer, so a bigger combo is obviously a bigger event."""
    base = [72, 76, 79, 84, 88, 91, 96, 100]
    if tier == 3:
        return norm(reverb_tail(seq(base[:3], 0.06, 0.55)), 0.75)
    if tier == 4:
        return norm(reverb_tail(seq(base[:4], 0.055, 0.6)), 0.78)
    if tier == 5:
        out = np.zeros(n_of(1.2))
        place(out, seq(base[:6], 0.05, 0.75), 0)
        place(out, thump(120, 60, 0.2, 1.1), 0)
        return norm(reverb_tail(out), 0.84)
    if tier == 6:
        out = np.zeros(n_of(1.5))
        place(out, seq(base, 0.045, 0.8), 0)
        place(out, chord_stab([60, 67, 72, 76], 1.2), 0.36, 0.14)
        place(out, thump(130, 50, 0.25, 1.3), 0.36)
        place(out, cymbal(0.9), 0.36, 0.42)
        return norm(reverb_tail(out, 1.1, 0.3), 0.9)
    if tier == 8:  # INSANE
        out = np.zeros(n_of(2.1))
        place(out, riser(0.45, 800, 7000), 0, 0.3)
        place(out, seq(base + [103, 108], 0.04, 0.9), 0.05)
        place(out, chord_stab([48, 60, 67, 72, 76, 79], 1.6), 0.45, 0.13)
        place(out, thump(150, 38, 0.45, 1.8), 0.45)
        place(out, cymbal(1.3), 0.45, 0.32)
        return norm(np.tanh(reverb_tail(out, 1.4, 0.24) * 0.9), 0.95)
    # LEGENDARY: two rising hits and a huge resolved chord.
    out = np.zeros(n_of(2.9))
    place(out, riser(0.7, 500, 7500, 4), 0, 0.34)
    place(out, seq(base + [103, 108], 0.035, 0.9), 0.02)
    place(out, chord_stab([55, 62, 67, 74], 0.5), 0.42, 0.13)
    place(out, thump(120, 50, 0.25, 1.4), 0.42)
    place(out, chord_stab([36, 48, 60, 67, 72, 76, 84], 2.2), 0.72, 0.13)
    place(out, thump(160, 30, 0.7, 2.2), 0.72)
    place(out, cymbal(1.9, 5), 0.72, 0.36)
    place(out, seq([84, 91, 96, 100, 103, 108], 0.07, 1.0), 0.82, 0.55)
    return norm(np.tanh(reverb_tail(out, 1.8, 0.28) * 0.9), 0.97)


def finale_hit():
    """One blade pass through the big fruit. The game raises its pitch with every hit."""
    r = np.random.default_rng(90)
    out = np.zeros(n_of(0.2))
    place(out, grains(r, 0.06, 26, 1200, 6000), 0, 0.7)
    place(out, squelch(r, 0.07, 1200, 620), 0.002, 0.42)
    place(out, thump(300, 170, 0.05, 0.4), 0)
    place(out, bell(mtof(79), 0.17), 0.002, 0.34)
    return norm(out, 0.72)


def finale_rise():
    out = np.zeros(n_of(1.0))
    place(out, riser(0.6, 300, 5000, 7), 0, 0.3)
    place(out, seq([60, 64, 67, 72, 76], 0.1, 0.6), 0.02, 0.8)
    place(out, thump(90, 150, 0.5, 0.9), 0)
    return norm(reverb_tail(out, 0.8, 0.2), 0.8)


def finale_burst():
    """The fruit gives way: a big wet burst, a deep hit, then a resolved chord and a shower of notes."""
    r = np.random.default_rng(91)
    out = np.zeros(n_of(2.4))
    place(out, thump(170, 36, 0.6, 2.0), 0)
    place(out, cut_melon(92), 0, 1.2)
    place(out, cut_juicy(93), 0.02, 0.9)
    place(out, droplets(r, 1.1, 90, 600, 3000), 0.05, 0.5)
    place(out, chord_stab([48, 60, 67, 72, 76, 84], 1.9), 0.03, 0.12)
    place(out, seq([84, 88, 91, 96, 100, 103, 108], 0.06, 0.9), 0.1, 0.55)
    place(out, cymbal(1.5, 6), 0.02, 0.26)
    return norm(np.tanh(reverb_tail(out, 1.4, 0.22) * 0.9), 0.97)


# ---------------------------------------------------------------- calm music
def felt_note(freq, dur, rate=MUSIC_RATE):
    """Soft felt-piano or kalimba tone: mostly fundamental, rounded attack, long decay."""
    t = np.arange(int(dur * rate)) / rate
    s = np.sin(2 * np.pi * freq * t) + 0.22 * np.sin(4 * np.pi * freq * t) * np.exp(-t * 3.2) + 0.06 * np.sin(6 * np.pi * freq * t) * np.exp(-t * 7)
    return s * np.minimum(1, t / 0.018) * np.exp(-t * 1.5)


def pad_note(freq, dur, rate=MUSIC_RATE):
    """Warm pad: slightly detuned sines with very slow swells, no sharp edges."""
    t = np.arange(int(dur * rate)) / rate
    s = sum(np.sin(2 * np.pi * freq * d * t + p) for d, p in ((0.998, 0.0), (1.0, 1.1), (1.002, 2.3)))
    s = s + 0.25 * np.sin(4 * np.pi * freq * t)
    return s * np.sin(np.linspace(0, np.pi, len(t))) ** 1.4


def pluck_note(freq, dur, rate=MUSIC_RATE):
    """A soft plucked string: quick attack, overtones that die away first."""
    t = np.arange(int(dur * rate)) / rate
    s = sum(np.sin(2 * np.pi * freq * h * t) * a * np.exp(-t * d) for h, a, d in ((1, 1.0, 2.0), (2, 0.42, 4.0), (3, 0.2, 7.0), (4, 0.08, 11.0)))
    return s * np.minimum(1, t / 0.004)


def bowl_note(freq, dur, rate=MUSIC_RATE):
    """A struck metal bowl: slightly inharmonic partials with a very long, slowly beating ring."""
    t = np.arange(int(dur * rate)) / rate
    s = sum(np.sin(2 * np.pi * freq * h * t) * a * np.exp(-t * d) for h, a, d in ((1, 1.0, 0.7), (1.004, 0.7, 0.8), (2.71, 0.28, 1.5), (5.15, 0.08, 3.0)))
    return s * np.minimum(1, t / 0.03)


def flute_note(freq, dur, rate=MUSIC_RATE):
    """A breathy wooden flute: slow swell, gentle vibrato that arrives late."""
    t = np.arange(int(dur * rate)) / rate
    vib = 0.006 * np.minimum(1, t / 1.2) * np.sin(2 * np.pi * 4.6 * t)
    phase = 2 * np.pi * freq * np.cumsum(1 + vib) / rate
    s = np.sin(phase) + 0.12 * np.sin(2 * phase)
    return s * np.sin(np.linspace(0, np.pi, len(t))) ** 0.7


def compose_calm(bpm, bars, chords, scale, seed, note_chance, pad_level=0.22, melody_level=0.34, tail=0.45, shaker=0.0, lead=felt_note, arp_level=0.14, breeze=0.0):
    """Slow chords and a sparse melody. Everything wraps, including reverb, so the loop is seamless."""
    rate = MUSIC_RATE
    rng = random.Random(seed)
    beat = 60.0 / bpm
    n = int(round(bars * 4 * beat * rate))
    mix = np.zeros(n)

    def put(sig, when, gain):
        p = int(when * rate) % n
        idx = (np.arange(len(sig)) + p) % n
        np.add.at(mix, idx, sig * gain)

    for bar in range(bars):
        ch = chords[bar % len(chords)]
        t0 = bar * 4 * beat
        for m in ch:
            put(pad_note(mtof(m), beat * 5.5), t0 - beat * 0.5, pad_level / len(ch) * 2)
        put(pad_note(mtof(ch[0] - 12), beat * 5), t0, pad_level * 0.9)
        # A slow, gently rolled arpeggio.
        for i, m in enumerate(ch):
            when = t0 + i * beat * rng.choice((0.5, 1.0)) + rng.uniform(-0.012, 0.012)
            if arp_level > 0:
                put(felt_note(mtof(m + 12), beat * 4), when, arp_level * rng.uniform(0.75, 1.0))

    # Sparse melody that breathes, with room between phrases.
    idx = len(scale) // 2
    for step in range(bars * 8):
        when = step * beat / 2
        if step % 16 > 11 or rng.random() > note_chance:
            continue
        idx = max(0, min(len(scale) - 1, idx + rng.choice((-2, -1, -1, 1, 1, 2))))
        put(lead(mtof(scale[idx]), beat * 5), when + rng.uniform(-0.01, 0.01), melody_level * rng.uniform(0.65, 1.0))

    if shaker > 0:
        nr = np.random.default_rng(seed)
        ln = int(0.09 * rate)
        hat = band(nr.normal(0, 1, ln), 3500, 9000, rate) * np.sin(np.linspace(0, np.pi, ln)) ** 2
        for step in range(bars * 8):
            put(hat, step * beat / 2, shaker * (1.0 if step % 2 else 0.55))

    if breeze > 0:
        # Air moving through leaves: filtered noise, swelling a whole number of times per loop.
        nr = np.random.default_rng(seed + 2)
        air = band(nr.normal(0, 1, n), 300, 1800, rate)
        air = air / (np.max(np.abs(air)) or 1)
        mix = mix + air * breeze * (0.6 + 0.4 * np.sin(2 * np.pi * 3 * np.arange(n) / n))

    # Circular reverb keeps the tail wrapping into the start of the loop.
    nr = np.random.default_rng(seed + 1)
    ir = nr.normal(0, 1, n) * np.exp(-np.arange(n) / (2.2 * rate))
    wet = np.fft.irfft(np.fft.rfft(mix) * np.fft.rfft(ir), n)
    wet = wet / (np.max(np.abs(wet)) or 1) * (np.max(np.abs(mix)) or 1)
    out = mix + wet * tail
    # Roll off the top end so nothing is bright or sharp.
    X = np.fft.rfft(out)
    f = np.fft.rfftfreq(n, 1 / rate)
    out = np.fft.irfft(X / np.sqrt(1 + (f / 3200.0) ** 4), n)
    return out / (np.max(np.abs(out)) or 1) * 0.62  # no end fade: this has to loop


# ---------------------------------------------------------------- Orchard Blast
def clang():
    """Deflected!: the blade bounces off a boss's guard. Inharmonic metal partials over a hard tick."""
    r = np.random.default_rng(90)
    n = n_of(0.55)
    t = np.arange(n) / SFX_RATE
    metal = sum(a * np.sin(2 * np.pi * f * t) * np.exp(-t / d) for f, a, d in
                ((820, 1.0, 0.16), (1347, 0.7, 0.11), (2210, 0.55, 0.08), (3190, 0.4, 0.05), (4870, 0.25, 0.03)))
    tick = band(r.normal(0, 1, n), 2500, 11000) * exp_env(n, 0.004)
    out = metal * np.minimum(1, t / 0.001) + tick * 1.2
    place(out, knock(), 0, 0.5)
    return norm(reverb_tail(out, 0.4, 0.15), 0.8)


def open_chime():
    """Open!: the guard drops. A bright rising two-note chime, quick so it reads as a cue."""
    out = np.zeros(n_of(0.7))
    place(out, bell(mtof(84), 0.5), 0)
    place(out, bell(mtof(91), 0.6), 0.075)
    place(out, bell(mtof(96), 0.5), 0.075, 0.35)
    return norm(reverb_tail(out, 0.5, 0.2), 0.62)


def roar():
    """A boss gets angry: a growl made of low noise shaped by a wobbling formant, falling in pitch."""
    r = np.random.default_rng(91)
    n = n_of(1.0)
    t = np.arange(n) / SFX_RATE
    env = np.minimum(1, t / 0.08) * np.exp(-np.maximum(0, t - 0.35) / 0.25)
    growl = np.sign(np.sin(2 * np.pi * np.cumsum(np.linspace(95, 55, n)) / SFX_RATE)) * 0.5
    growl = band(growl + r.normal(0, 0.6, n), 70, 1400)
    growl *= 0.65 + 0.35 * np.sin(2 * np.pi * 23 * t)
    sub = sine_sweep(110, 45, n) * 0.8
    out = (growl * 1.4 + sub) * env
    return norm(np.tanh(out * 1.8), 0.9)


def boss_intro():
    """A boss drops in: a rising whoosh, a heavy landing and a low minor brass stab."""
    out = np.zeros(n_of(2.0))
    place(out, riser(0.6, 200, 3000, 92), 0, 0.5)
    place(out, thump(120, 38, 0.5, 1.6), 0.6)
    place(out, boom(), 0.6, 0.45)
    place(out, chord_stab([36, 43, 48, 51], 1.2), 0.62, 0.2)
    return norm(reverb_tail(out, 1.0, 0.3), 0.95)


def victory():
    """A boss is beaten: a short fanfare that lands on a big major chord."""
    out = np.zeros(n_of(2.4))
    place(out, seq([67, 72, 76, 79], 0.09, 0.6), 0, 0.8)
    place(out, seq([84, 79, 84, 88], 0.12, 0.9), 0.4, 0.8)
    place(out, chord_stab([60, 64, 67, 72, 76, 84], 1.4), 0.88, 0.16)
    place(out, thump(150, 55, 0.3, 1.2), 0.88)
    place(out, cymbal(1.2, 93), 0.88, 0.35)
    return norm(reverb_tail(out, 1.0, 0.3), 0.92)


def pop():
    """A single-use charm is spent: a round, quick pop with a little sparkle after it."""
    out = np.zeros(n_of(0.5))
    n = n_of(0.07)
    t = np.arange(n) / SFX_RATE
    place(out, sine_sweep(380, 1500, n) * np.exp(-t / 0.025), 0, 1.0)
    place(out, band(np.random.default_rng(94).normal(0, 1, n_of(0.01)), 1500, 8000), 0, 0.4)
    place(out, seq([91, 96], 0.05, 0.3), 0.05, 0.25)
    return norm(reverb_tail(out, 0.4, 0.15), 0.62)




def _syllable(f0, f1, dur, vowel, seed):
    """One sung syllable: a buzzing voice with an "h" of breath first, shaped by the vowel's formants."""
    n = n_of(dur)
    t = np.arange(n) / SFX_RATE
    f = np.linspace(f0, f1, n) * (1 + 0.015 * np.sin(2 * np.pi * 6 * t))
    ph = np.cumsum(f) / SFX_RATE
    buzz = sum(np.sin(2 * np.pi * h * ph) / h ** 1.1 for h in range(1, 30) if h * max(f0, f1) < 9000)
    breath = np.random.default_rng(seed).normal(0, 1, n)
    src = buzz * np.minimum(1, t / 0.03) + breath * 0.5 * np.exp(-t / 0.025)
    out = sum(band(src, fc * 0.8, fc * 1.2) * g for fc, g in vowel)
    return out * np.minimum(1, t / 0.012) * np.exp(-t / (dur * 0.45))


VOWEL_HA = ((750, 1.0), (1200, 0.7), (2600, 0.25))
VOWEL_HO = ((500, 1.0), (850, 0.75), (2400, 0.15))


def laugh():
    """A boss's cocky laugh, ha-ha-ha-ha falling away. Played higher for The Big Melon, lower for The Bomb King."""
    out = np.zeros(n_of(1.0))
    for i in range(5):
        f0 = 230 - i * 14
        place(out, _syllable(f0, f0 * 0.9, 0.12, VOWEL_HA, 700 + i), i * 0.135, 1.0 - i * 0.12)
    return norm(reverb_tail(out, 0.4, 0.15), 0.75)


def chuckle():
    """A short ho-ho when your blade is Deflected."""
    out = np.zeros(n_of(0.5))
    for i in range(2):
        place(out, _syllable(190 - i * 20, 170 - i * 20, 0.1, VOWEL_HO, 710 + i), i * 0.12, 1.0 - i * 0.2)
    return norm(reverb_tail(out, 0.3, 0.12), 0.65)


def whoosh():
    """Something big moving fast: low, wide air that swells and passes."""
    r = np.random.default_rng(720)
    n = n_of(0.45)
    t = np.linspace(0, 1, n)
    air = band(r.normal(0, 1, n), 120, 1800) * np.sin(np.pi * t) ** 1.8
    hiss = band(r.normal(0, 1, n), 1500, 5000) * np.sin(np.pi * np.clip(t * 1.3 - 0.15, 0, 1)) ** 2 * 0.3
    return norm(air + hiss, 0.6)


def launch():
    """The Giant Bomb knocked clear: a rising rush and a distant pop as it leaves the stage."""
    out = np.zeros(n_of(1.4))
    place(out, riser(0.5, 300, 5000, 721), 0, 0.6)
    place(out, swish(722, 0.3, 500, 2600), 0.35, 0.8)
    far = boom()[:n_of(0.8)] * np.exp(-np.arange(n_of(0.8)) / n_of(0.25))
    place(out, band(far, 60, 1500), 0.75, 0.35)
    return norm(out, 0.7)


def levelup():
    """Level up in Orchard Blast: a crisp, sparkly rising RPG arpeggio chime that resolves on a bright bell chord."""
    out = np.zeros(n_of(1.1))
    for i, m in enumerate((74, 78, 81, 86)):
        place(out, bell(mtof(m), 0.45), i * 0.065, 0.72)
        place(out, bell(mtof(m + 12), 0.32), i * 0.065 + 0.012, 0.28)
    place(out, chord_stab([62, 69, 74, 78, 86], 0.75), 0.20, 0.16)
    place(out, thump(160, 75, 0.18, 0.65), 0.20, 0.45)
    return norm(reverb_tail(out, 0.65, 0.22), 0.78)


# ---------------------------------------------------------------- boss music
# Brighter and busier than the calm tracks, so a higher rate: hats and the chip lead need the top end.
BOSS_RATE = 22050
BOSS_LUFS = -19.0  # the same integrated loudness as every other song in the game


def _t(n, rate=BOSS_RATE):
    return np.arange(n) / rate


def _harm(freq, n, amps, rate=BOSS_RATE, detune=1.0):
    """Sums harmonics h (amplitude amps(h, t)) that stay under 10 kHz, so nothing folds back."""
    t = _t(n, rate)
    out = np.zeros(n)
    h = 1
    while h * freq < 10000 and h <= 24:
        a = amps(h, t)
        if np.isscalar(a) and a == 0:
            h += 1
            continue
        out += a * np.sin(2 * np.pi * freq * detune * h * t)
        h += 1
    return out


def _gate(n, release, rate=BOSS_RATE):
    g = np.ones(n)
    r = min(n, int(release * rate))
    if r > 0:
        g[-r:] = np.linspace(1, 0, r) ** 1.5
    return g


def bk_kick(punch=1.0):
    n = int(0.34 * BOSS_RATE)
    t = _t(n)
    f = 46 + 120 * punch * np.exp(-t / 0.032)
    body = np.sin(2 * np.pi * np.cumsum(f) / BOSS_RATE) * np.exp(-t / 0.13)
    click = band(np.random.default_rng(501).normal(0, 1, n), 1500, 6000, BOSS_RATE) * np.exp(-t / 0.003) * 0.35
    return np.tanh((body + click) * 1.7)


def bk_snare(seed=502, bright=1.0):
    n = int(0.24 * BOSS_RATE)
    t = _t(n)
    noise = band(np.random.default_rng(seed).normal(0, 1, n), 1300, 7500 * bright, BOSS_RATE) * np.exp(-t / 0.075)
    tone = (np.sin(2 * np.pi * 182 * t) + 0.5 * np.sin(2 * np.pi * 330 * t)) * np.exp(-t / 0.045)
    return noise * 1.1 + tone * 0.7


def bk_clap(seed=503):
    n = int(0.2 * BOSS_RATE)
    t = _t(n)
    src = band(np.random.default_rng(seed).normal(0, 1, n), 900, 5200, BOSS_RATE)
    env = np.exp(-t / 0.07) * 0.6
    for at in (0.0, 0.011, 0.022):
        env += np.where(t >= at, np.exp(-(t - at) / 0.007), 0)
    return src * env


def bk_hat(open_=False, seed=504):
    n = int((0.16 if open_ else 0.05) * BOSS_RATE)
    t = _t(n)
    return band(np.random.default_rng(seed).normal(0, 1, n), 6500, 10500, BOSS_RATE) * np.exp(-t / (0.07 if open_ else 0.016))


def bk_crash(seed=505):
    n = int(1.6 * BOSS_RATE)
    t = _t(n)
    return band(np.random.default_rng(seed).normal(0, 1, n), 3000, 10500, BOSS_RATE) * np.exp(-t / 0.5) * np.minimum(1, t / 0.002)


def bk_timpani(freq):
    """A royal drum: a low, slightly falling boom with a soft skin hit."""
    n = int(0.9 * BOSS_RATE)
    t = _t(n)
    f = freq * (1 + 0.08 * np.exp(-t / 0.05))
    body = np.sin(2 * np.pi * np.cumsum(f) / BOSS_RATE) + 0.35 * np.sin(2 * np.pi * np.cumsum(f * 1.5) / BOSS_RATE) * np.exp(-t / 0.15)
    skin = band(np.random.default_rng(int(freq)).normal(0, 1, n), 200, 2000, BOSS_RATE) * np.exp(-t / 0.02) * 0.4
    return (body * np.exp(-t / 0.35) + skin) * np.minimum(1, t / 0.002)


def bk_tom(freq):
    n = int(0.3 * BOSS_RATE)
    t = _t(n)
    f = freq * (1 + 0.5 * np.exp(-t / 0.03))
    return np.sin(2 * np.pi * np.cumsum(f) / BOSS_RATE) * np.exp(-t / 0.11)


def bk_bass(freq, dur):
    """A round synth bass: bright at the pluck, then mostly the low harmonics, with a sine under it."""
    n = int(dur * BOSS_RATE)
    s = _harm(freq, n, lambda h, t: (1.0 / h) * np.exp(-t * (3 + h * 2.2)))
    s += 0.9 * np.sin(2 * np.pi * freq * _t(n))
    return s * np.minimum(1, _t(n) / 0.003) * _gate(n, 0.025)


def bk_chip(freq, dur):
    """A square-ish lead with a late vibrato: the cheeky voice of the melon tracks."""
    n = int(dur * BOSS_RATE)
    t = _t(n)
    vib = 1 + 0.004 * np.minimum(1, np.maximum(0, t - 0.12) / 0.2) * np.sin(2 * np.pi * 5.5 * t)
    ph = 2 * np.pi * freq * np.cumsum(vib) / BOSS_RATE
    s = np.zeros(n)
    h = 1
    while h * freq < 9000:
        s += np.sin(h * ph) / h
        h += 2
    env = np.minimum(1, t / 0.006) * (0.62 + 0.38 * np.exp(-t / 0.08))
    return s * env * _gate(n, 0.03)


def bk_brass(freq, dur):
    """A brass section: detuned saws that brighten as the note swells."""
    n = int(dur * BOSS_RATE)
    t = _t(n)
    bright = np.minimum(1, t / 0.06)
    s = sum(_harm(freq, n, lambda h, tt: (1.0 / h) * (bright if h > 2 else 1.0) * np.exp(-h * 0.12), detune=d) for d in (0.996, 1.0, 1.004)) / 3
    env = np.minimum(1, t / 0.025) * (0.8 + 0.2 * np.exp(-t / 0.2))
    return s * env * _gate(n, 0.05)


def bk_bell(freq, dur):
    return bell(freq, dur, BOSS_RATE)


def bk_pluck(freq, dur):
    return pluck_note(freq, dur, BOSS_RATE)


def bk_pad(freq, dur):
    return pad_note(freq, dur, BOSS_RATE)


def bk_wobble(freq, dur):
    """Heavy dubstep wobble growl: low square/saw stack swept by a 6 Hz LFO."""
    n = int(dur * BOSS_RATE)
    t = _t(n)
    lfo = 0.35 + 0.65 * (0.5 - 0.5 * np.cos(2 * np.pi * 6.0 * t))
    s = (_harm(freq * 0.5, n, lambda h, tt: (1.0 / h) * (lfo if h > 2 else 1.0)) +
         0.5 * _harm(freq * 0.503, n, lambda h, tt: (1.0 / h) * lfo))
    s = np.tanh(s * 2.1) * 0.65
    return s * np.minimum(1, t / 0.005) * _gate(n, 0.025)


def bk_glitch(freq, dur):
    """Glitch-hop chopped FM lead: gritty step-quantized carrier with fast gate stutters."""
    n = int(dur * BOSS_RATE)
    t = _t(n)
    mod = np.sin(2 * np.pi * freq * 2.0 * t) * 2.4 * np.exp(-t * 8.0)
    s = np.sin(2 * np.pi * freq * t + mod) + 0.35 * np.sin(2 * np.pi * freq * 3.0 * t)
    chop = np.where((np.sin(2 * np.pi * 16.0 * t) >= 0) | (t < 0.04), 1.0, 0.25)
    s = np.round(s * 12.0) / 12.0 * chop
    return s * np.minimum(1, t / 0.004) * _gate(n, 0.02)


def bk_reese(freq, dur):
    """Neurofunk DnB detuned Reese lead: wide phasing saws with midrange bite."""
    n = int(dur * BOSS_RATE)
    t = _t(n)
    s = sum(_harm(freq, n, lambda h, tt: (1.0 / h) * np.exp(-h * 0.09), detune=d) for d in (0.989, 0.996, 1.0, 1.005, 1.012)) / 4.2
    s = np.tanh(s * 1.9) * 0.75
    return s * np.minimum(1, t / 0.01) * _gate(n, 0.03)


def bk_tense(freq, dur):
    """Dark pulsing string/drone for the Room 50 reveal tension loop."""
    n = int(dur * BOSS_RATE)
    t = _t(n)
    s = sum(_harm(freq, n, lambda h, tt: (1.0 / h) * np.exp(-h * 0.35), detune=d) for d in (0.994, 1.0, 1.006)) / 3.0
    pulse = 0.6 + 0.4 * np.sin(2 * np.pi * 3.2 * t)
    return s * pulse * np.minimum(1, t / 0.04) * _gate(n, 0.08)


def bk_kick_heavy():
    """Aggressive clipped dubstep/DnB kick: sharp pitch slam + punchy transient click."""
    n = int(0.30 * BOSS_RATE)
    t = _t(n)
    f = 48.0 + 195.0 * np.exp(-t / 0.022)
    body = np.sin(2 * np.pi * np.cumsum(f) / BOSS_RATE) * np.exp(-t / 0.115)
    click = band(np.random.default_rng(711).normal(0, 1, n), 1800, 7200, BOSS_RATE) * np.exp(-t / 0.004) * 0.55
    return np.tanh((body * 1.25 + click) * 2.5)


def bk_snare_heavy(seed=712):
    """Hard-hitting modern EDM/DnB snare: punchy 205 Hz fundamental + crackling white noise."""
    n = int(0.24 * BOSS_RATE)
    t = _t(n)
    noise = band(np.random.default_rng(seed).normal(0, 1, n), 1100, 8200, BOSS_RATE) * np.exp(-t / 0.082)
    tone = (np.sin(2 * np.pi * (205.0 + 75.0 * np.exp(-t / 0.015)) * t) + 0.6 * np.sin(2 * np.pi * 412 * t)) * np.exp(-t / 0.048)
    return np.tanh((noise * 1.35 + tone * 0.95) * 2.1)


def bk_growl(freq, dur):
    """Aggressive FM/wavefolded dubstep growl bass with sweeping formant bite & sub weight."""
    n = int(dur * BOSS_RATE)
    t = _t(n)
    sub_f = freq * 0.5
    while sub_f < 48:
        sub_f *= 2
    lfo = 0.25 + 0.75 * (0.5 - 0.5 * np.cos(2 * np.pi * 7.5 * t))
    mod = (3.4 * lfo + 1.2 * np.exp(-t * 9.0)) * np.sin(2 * np.pi * sub_f * 2.0 * t)
    mod += 1.6 * lfo * np.sin(2 * np.pi * sub_f * 3.0 * t)
    car = np.sin(2 * np.pi * sub_f * t + mod) + 0.55 * np.sin(2 * np.pi * sub_f * 2.0 * t + 0.7 * mod)
    fold = np.tanh(car * 2.8) + 0.35 * np.sin(car * 4.5)
    sub = 0.75 * np.sin(2 * np.pi * sub_f * t)
    s = np.tanh((fold * (0.45 + 0.55 * lfo) + sub) * 1.8) * 0.78
    return s * np.minimum(1, t / 0.004) * _gate(n, 0.02)


def bk_acid(freq, dur):
    """Squelchy resonant 303/cyberpunk acid saw lead with sharp filter bite."""
    n = int(dur * BOSS_RATE)
    t = _t(n)
    env = 0.28 + 0.72 * np.exp(-t / 0.09)
    res_f = freq * (3.5 + 4.5 * np.exp(-t / 0.07))
    s = _harm(freq, n, lambda h, tt: (1.0 / h) * (0.35 + 1.15 * np.exp(-abs(h * freq - res_f) / (freq * 1.4 + 80.0))))
    s = np.tanh(s * 2.6 * env) * 0.68
    return s * np.minimum(1, t / 0.003) * _gate(n, 0.02)


def bk_glitch_heavy(freq, dur):
    """Crushed staccato glitch-hop lead: aggressive FM bark + fast bitcrush stutter chops."""
    n = int(dur * BOSS_RATE)
    t = _t(n)
    slide = 1.0 + 0.14 * np.exp(-t / 0.018)
    ph = 2 * np.pi * freq * np.cumsum(slide) / BOSS_RATE
    mod = np.sin(2.0 * ph) * (3.2 * np.exp(-t * 7.0) + 0.9)
    s = np.sin(ph + mod) + 0.5 * _harm(freq * 1.5, n, lambda h, tt: 1.0 / h if h <= 5 else 0.0)
    chop = np.where((np.sin(2 * np.pi * 24.0 * t) >= -0.15) | (t < 0.025), 1.0, 0.12)
    s = np.tanh((np.round(s * 9.0) / 9.0) * 2.4) * chop * 0.72
    return s * np.minimum(1, t / 0.003) * _gate(n, 0.018)


def bk_reese_heavy(freq, dur):
    """Ferocious Neurofunk detuned Reese wall: 7 wide oscillators + sweeping mid-band crunch."""
    n = int(dur * BOSS_RATE)
    t = _t(n)
    s = sum(_harm(freq, n, lambda h, tt: (1.0 / h) * np.exp(-h * 0.045), detune=d)
            for d in (0.982, 0.990, 0.996, 1.0, 1.005, 1.011, 1.019)) / 4.8
    sw = 0.55 + 0.45 * np.sin(2 * np.pi * 4.5 * t)
    s = np.tanh(s * (2.6 + 1.4 * sw)) * 0.80
    return s * np.minimum(1, t / 0.006) * _gate(n, 0.02)


VOICES = {
    "chip": bk_chip, "brass": bk_brass, "pluck": bk_pluck, "bell": bk_bell,
    "wobble": bk_wobble, "glitch": bk_glitch, "reese": bk_reese, "tense": bk_tense,
    "growl": bk_growl, "acid": bk_acid, "glitch_heavy": bk_glitch_heavy, "reese_heavy": bk_reese_heavy,
}


def chord_tone(chord, i):
    """Chord tones counted up from the root: 0 root, 1 third, 2 fifth, 3 the octave, and on."""
    root, quality = chord
    iv = (0, 3, 7) if quality == "m" else (0, 4, 7)
    return root + iv[i % 3] + 12 * (i // 3)


def parse_drums(text):
    """One bar of 16 steps per drum, written like 'k...k...' with '.' for a rest."""
    return {line.strip()[0]: line.strip()[2:] for line in text.strip().splitlines() if line.strip()}


def compose_boss(bpm, sections, seed):
    """A boss track built from sections of 8 bars. Everything wraps, so the loop has no seam.
    Each section: chords (one per bar, (root midi, 'M' or 'm')), drums (16-step lines), bass (16 steps:
    'x' root, 'o' octave up, 'f' fifth, '.' rest), leads ((voice, octave shift, gain, motif bars)), and
    flags for stabs, pads, timpani, a riser and a closing fill."""
    rate = BOSS_RATE
    beat = 60.0 / bpm
    step = beat / 4
    bars = sum(len(s["chords"]) for s in sections)
    n = int(round(bars * 4 * beat * rate))
    music = np.zeros(n)
    drums = np.zeros(n)
    rng = np.random.default_rng(seed)

    def put(buf, sig, when, gain):
        p = int(round(when * rate)) % n
        idx = (np.arange(len(sig)) + p) % n
        np.add.at(buf, idx, sig * gain)

    kick, snare, clap = bk_kick(), bk_snare(), bk_clap()
    hkick, hsnare = bk_kick_heavy(), bk_snare_heavy()
    hat, ohat, crash = bk_hat(), bk_hat(True), bk_crash()
    bar0 = 0
    is_heavy = any(s.get("heavy") for s in sections)
    for sec in sections:
        pat = parse_drums(sec.get("drums", ""))
        use_kick = hkick if sec.get("heavy") else kick
        use_snare = hsnare if sec.get("heavy") else snare
        for b, chord in enumerate(sec["chords"]):
            t0 = (bar0 + b) * 4 * beat
            last = b == len(sec["chords"]) - 1
            if b == 0 and sec.get("crash", True):
                put(drums, crash, t0, 0.52 if sec.get("heavy") else 0.45)
            for s in range(16):
                when = t0 + s * step
                swing = sec.get("swing", 0) * step if s % 2 else 0
                fill = last and sec.get("fill") and s >= 12
                if pat.get("k", "")[s:s + 1] == "x":
                    put(drums, use_kick, when, 0.95 if sec.get("heavy") else 0.8)
                if fill:
                    put(drums, use_snare, when, 0.55 + 0.12 * (s - 12))
                    put(drums, use_snare, when + step / 2, 0.42 + 0.1 * (s - 12))
                    continue
                c = pat.get("s", "")[s:s + 1]
                if c in ("x", "g"):
                    put(drums, use_snare, when + swing, (1.0 if sec.get("heavy") else 0.85) if c == "x" else (0.38 if sec.get("heavy") else 0.3))
                if pat.get("c", "")[s:s + 1] == "x":
                    put(drums, clap, when + swing, 0.78 if sec.get("heavy") else 0.7)
                h = pat.get("h", "")[s:s + 1]
                if h == "x":
                    put(drums, hat, when + swing, 0.36 * (1.0 if s % 4 == 2 else 0.7) * rng.uniform(0.85, 1.0))
                elif h == "o":
                    put(drums, ohat, when + swing, 0.36)
                tm = pat.get("t", "")[s:s + 1]
                if tm in ("x", "y"):
                    put(drums, bk_timpani(mtof(36 + (chord[0] + (7 if tm == "y" else 0)) % 12)), when, 0.9)
                if pat.get("m", "")[s:s + 1] in ("1", "2", "3"):
                    put(drums, bk_tom(150 / int(pat["m"][s])), when, 0.55)
            # Bass.
            bp = sec.get("bass", "")
            for s in range(16):
                c = bp[s:s + 1]
                if c in ("x", "o", "f"):
                    ln = 1
                    while s + ln < 16 and bp[s + ln] == "-":
                        ln += 1
                    m = chord[0] - 12 + (12 if c == "o" else 7 if c == "f" else 0)
                    while m > 47:
                        m -= 12
                    while m < 33:
                        m += 12
                    bsig = bk_bass(mtof(m), ln * step * 0.92)
                    if sec.get("heavy"):
                        bsig = np.tanh(bsig * 2.2) * 0.9
                    put(music, bsig, t0 + s * step + (sec.get("swing", 0) * step if s % 2 else 0), 0.48 if sec.get("heavy") else 0.4)
            # Pads hold the chord under the whole bar.
            if sec.get("pad", 0) > 0:
                for i in range(3):
                    put(music, bk_pad(mtof(chord_tone(chord, i) + 12), 4 * beat * 1.05), t0, sec["pad"] / 3)
            # Brass stabs: short chords on the steps given.
            st = sec.get("stabs", "")
            for s in range(16):
                if st[s:s + 1] == "x":
                    for i in range(3):
                        stab = bk_brass(mtof(chord_tone(chord, i) + 12), step * 1.6)
                        if sec.get("heavy"):
                            stab = np.tanh(stab * 2.0) * 0.95
                        put(music, stab, t0 + s * step, 0.20 if sec.get("heavy") else 0.16)
            # Leads: a motif of one or more bars, with notes as (step, length in steps, chord tone).
            for voice, octave, gain, motif in sec.get("leads", ()):
                for st0, ln, tone in motif[b % len(motif)]:
                    f = mtof(chord_tone(chord, tone) + 12 * octave)
                    put(music, VOICES[voice](f, ln * step * 0.95), t0 + st0 * step, gain * 1.55)
            if last and sec.get("riser"):
                r = riser(4 * beat, 300, 6800, seed + bar0)
                r = np.interp(np.arange(int(len(r) * rate / SFX_RATE)) * SFX_RATE / rate, np.arange(len(r)), r)
                put(music, r / (np.max(np.abs(r)) or 1), t0, 0.28 if is_heavy else 0.22)
        bar0 += len(sec["chords"])

    # A short room on the music only, wrapping like everything else; tighter on heavy aggressive tracks.
    ir = np.random.default_rng(seed + 1).normal(0, 1, n) * np.exp(-np.arange(n) / ((0.22 if is_heavy else 0.5) * rate))
    wet = np.fft.irfft(np.fft.rfft(music) * np.fft.rfft(ir), n)
    wet = wet / (np.max(np.abs(wet)) or 1) * (np.max(np.abs(music)) or 1)
    out = music + wet * (0.06 if is_heavy else 0.18) + drums * (1.08 if is_heavy else 0.8)
    # Soften the very top, keeping biting highs on heavy milestone tracks.
    X = np.fft.rfft(out)
    f = np.fft.rfftfreq(n, 1 / rate)
    out = np.fft.irfft(X / np.sqrt(1 + (f / (8800.0 if is_heavy else 7500.0)) ** 4), n)
    out = out / (np.max(np.abs(out)) or 1)
    drive = 2.05 if is_heavy else 1.4
    return np.tanh(out * drive) / np.tanh(drive)


def level_boss(x, rate=BOSS_RATE):
    """Brings a track to BOSS_LUFS with ffmpeg's EBU R128 meter, the way the Shop's songs were levelled.
    Peaks over 0.97 are softly limited."""
    import subprocess
    import tempfile
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
        name = tmp.name
    with wave.open(name, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes((np.clip(x, -1, 1) * 32000).astype("<i2").tobytes())
    r = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", name, "-af", "ebur128", "-f", "null", "-"], capture_output=True, text=True)
    os.remove(name)
    import re
    lufs = float(re.findall(r"I:\s+(-?[\d.]+) LUFS", r.stderr)[-1])
    y = x * 10 ** ((BOSS_LUFS - lufs) / 20)
    peak = np.max(np.abs(y))
    if peak > 0.97:
        over = np.abs(y) > 0.8
        y[over] = np.sign(y[over]) * (0.8 + 0.17 * np.tanh((np.abs(y[over]) - 0.8) / 0.17))
    return y, lufs


M = "M"
m = "m"

# The Big Melon: bouncy and playful, with a push.
MELON_DRUMS = """k x...x...x...x...
c ....x.......x...
h ..x...x...x...x."""
MELON_DRUMS_BUSY = """k x...x...x...x...
c ....x.......x...
h xxoxxxoxxxoxxxox"""
MELON_DRUMS_LIGHT = """c ....x.......x...
h ..x...x...x...x."""


def track_seed_spitter():
    """Seed Spitter (The Big Melon, 144 BPM, F major): a hopping chip tune over four-on-the-floor."""
    prog = [(53, M), (48, M), (50, m), (46, M)] * 2
    hop = [[(0, 2, 3), (3, 1, 4), (4, 2, 5), (6, 2, 4), (8, 3, 3), (12, 2, 2), (14, 2, 3)],
           [(0, 3, 4), (4, 2, 3), (6, 2, 2), (8, 6, 3), (14, 1, 2), (15, 1, 1)]]
    answer = [[(0, 1, 5), (2, 1, 5), (4, 2, 6), (8, 1, 5), (10, 1, 4), (12, 4, 3)],
              [(0, 2, 2), (2, 2, 3), (4, 2, 4), (6, 2, 5), (8, 8, 6)]]
    arp = [[(s, 1, (0, 1, 2, 3)[(s // 2) % 4]) for s in range(0, 16, 2)]]
    bass = "x--ox-o-x--ox-o-"
    return 144, [
        dict(chords=prog, drums=MELON_DRUMS, bass=bass, leads=[("pluck", 1, 0.22, arp)], fill=True),
        dict(chords=prog, drums=MELON_DRUMS, bass=bass, leads=[("chip", 1, 0.2, hop), ("pluck", 1, 0.14, arp)], fill=True),
        dict(chords=[(50, m), (46, M), (53, M), (48, M)] * 2, drums=MELON_DRUMS_LIGHT, bass="x-------x-------", pad=0.3,
             leads=[("bell", 2, 0.18, answer)], riser=True, fill=True),
        dict(chords=prog, drums=MELON_DRUMS_BUSY, bass=bass, leads=[("chip", 1, 0.2, hop), ("chip", 2, 0.08, answer), ("pluck", 1, 0.12, arp)], fill=True),
    ]


def track_rind_and_run():
    """Rind and Run (The Big Melon, 132 BPM, D mixolydian): a swung, cheeky chase."""
    prog = [(50, M), (48, M), (43, M), (50, M), (50, M), (48, M), (43, M), (45, M)]
    run = [[(0, 1, 3), (2, 1, 3), (3, 1, 4), (4, 2, 5), (7, 1, 4), (8, 2, 3), (11, 1, 2), (12, 4, 3)],
           [(0, 1, 5), (2, 1, 4), (4, 1, 3), (6, 1, 2), (8, 2, 1), (10, 2, 2), (12, 4, 3)]]
    tease = [[(0, 2, 6), (4, 2, 5), (8, 1, 6), (10, 1, 5), (12, 4, 4)], [(0, 8, 3), (12, 2, 4), (14, 2, 5)]]
    drums = """k x.....x.x.....x.
s ....x.......x..g
h x.x.x.x.x.x.x.x."""
    drums_b = """k x.....x.x.....x.
s ....x..g....x.gg
h xxxxxxxxxxxxxxxx"""
    bass = "x-.x.-o-x-.x.fo."
    return 132, [
        dict(chords=prog, drums=drums, bass=bass, swing=0.18, leads=[("chip", 1, 0.19, run)], fill=True),
        dict(chords=prog, drums=drums_b, bass=bass, swing=0.18, stabs="..x...x...x...x.", leads=[("chip", 1, 0.19, run), ("bell", 2, 0.1, tease)], fill=True),
        dict(chords=[(43, M), (45, M), (50, M), (50, M)] * 2, drums="""h x.x.x.x.x.x.x.x.
c ....x.......x...""", bass="x---o---x---o---", swing=0.18, pad=0.26, leads=[("pluck", 2, 0.2, tease)], riser=True, fill=True),
        dict(chords=prog, drums=drums_b, bass=bass, swing=0.18, stabs="..x...x...x...x.", leads=[("chip", 1, 0.2, run), ("chip", 2, 0.07, tease)], fill=True),
    ]


# The Bomb King: heavier, bigger, with a royal march edge.
def track_long_live_the_fuse():
    """Long Live the Fuse (The Bomb King, 116 BPM, C minor): a royal march with timpani and brass."""
    prog = [(48, m), (44, M), (51, M), (43, M)] * 2
    fanfare = [[(0, 3, 3), (3, 1, 3), (4, 4, 5), (8, 3, 4), (11, 1, 3), (12, 4, 2)],
               [(0, 3, 3), (3, 1, 2), (4, 4, 1), (8, 8, 2)]]
    low = [[(0, 6, 0), (6, 2, 1), (8, 8, 2)]]
    march = """k x.......x.......
s ....x..g.g..x.gg
t x.......y.......
h x...x...x...x..."""
    march_b = """k x.....x.x.......
s g.g.x.gg.gg.x.gg
t x.....x.y.......
h x.x.x.x.x.x.x.x."""
    bass = "x-----x.x-------"
    return 116, [
        dict(chords=prog, drums=march, bass=bass, leads=[("brass", 0, 0.22, low)], fill=True),
        dict(chords=prog, drums=march, bass=bass, leads=[("brass", 1, 0.24, fanfare)], stabs="x.......x.......", fill=True),
        dict(chords=[(41, m), (48, m), (44, M), (43, M)] * 2, drums="""t x.......x.......
s ............x...""", bass="x---------------", pad=0.32, leads=[("bell", 2, 0.14, fanfare)], riser=True, fill=True),
        dict(chords=prog, drums=march_b, bass="x-----x.x---x---", leads=[("brass", 1, 0.25, fanfare), ("brass", 0, 0.14, low)], stabs="x.....x.x.......", fill=True),
    ]


def track_powder_keg_court():
    """Powder Keg Court (The Bomb King, 128 BPM, D minor): driving drums, toms and brass hits."""
    prog = [(50, m), (46, M), (48, M), (45, M)] * 2
    drive = [[(0, 2, 3), (2, 2, 4), (4, 2, 5), (6, 1, 4), (7, 1, 3), (8, 4, 4), (12, 2, 3), (14, 2, 2)],
             [(0, 2, 3), (2, 2, 2), (4, 4, 1), (8, 2, 2), (10, 2, 3), (12, 4, 2)]]
    crown = [[(0, 4, 6), (4, 4, 5), (8, 8, 4)], [(0, 4, 5), (4, 4, 4), (8, 8, 3)]]
    drums = """k x...x...x...x...
s ....x.......x...
h ..x...x...x...x.
m ..............12"""
    drums_b = """k x...x...x.x.x...
s ....x.......x.g.
h xxoxxxoxxxoxxxox
m ............1123"""
    bass = "x.x.x.x.x.x.o.x."
    return 128, [
        dict(chords=prog, drums=drums, bass=bass, stabs="x.....x.......x.", fill=True),
        dict(chords=prog, drums=drums_b, bass=bass, stabs="x.....x.......x.", leads=[("chip", 1, 0.16, drive), ("brass", 0, 0.12, crown)], fill=True),
        dict(chords=[(46, M), (48, M), (50, m), (50, m)] * 2, drums="""t x.......x.......
h ..x...x...x...x.""", bass="x-------x-------", pad=0.3, leads=[("brass", 1, 0.2, crown)], riser=True, fill=True),
        dict(chords=prog, drums=drums_b, bass=bass, stabs="x.....x.x.....x.", leads=[("chip", 1, 0.17, drive), ("brass", 1, 0.18, crown)], fill=True),
    ]


def track_tension_loop():
    """Ominous tension loop (96 BPM, B minor): pulsing low strings, heartbeat sub & distant ticking."""
    prog = [(47, m), (43, M), (48, M), (42, M)] * 2
    motif = [[(0, 8, 0), (8, 8, 1)], [(0, 8, 2), (8, 8, 0)]]
    drums = """k x.......x.......
h ..x...x...x...x."""
    return 96, [
        dict(chords=prog, drums=drums, bass="x-------x-------", pad=0.32, leads=[("tense", 0, 0.24, motif)]),
        dict(chords=prog, drums=drums, bass="x---x---x---x---", pad=0.36, leads=[("tense", 0, 0.28, motif), ("bell", 2, 0.08, motif)]),
    ]


def track_boss_dubstep():
    """Abyssal Core (Milestone Boss - Heavy Tearout Dubstep, 140 BPM, D minor): relentless FM growls & acid drops."""
    prog = [(50, m), (46, M), (48, m), (45, M)] * 2
    prog_b = [(50, m), (51, M), (49, m), (45, M)] * 2
    drop = [[(0, 4, 0), (4, 2, 3), (6, 2, 1), (8, 3, 2), (11, 1, 4), (12, 2, 3), (14, 2, 5)],
            [(0, 2, 0), (2, 2, 4), (4, 2, 3), (6, 2, 5), (8, 4, 2), (12, 2, 4), (14, 2, 1)]]
    scream = [[(0, 2, 3), (3, 1, 5), (4, 2, 6), (6, 2, 4), (8, 2, 5), (10, 2, 7), (12, 2, 5), (14, 2, 4)],
              [(0, 4, 6), (4, 2, 5), (6, 2, 3), (8, 2, 4), (10, 2, 6), (12, 4, 7)]]
    charge = """k x...x...x...x...
s ........x.....g.
h xxoxxxoxxxoxxxox
c ....x.......x..."""
    crush = """k x..x..x...x..x..
s ........x....gg.
c ........x.......
h xxoxxxoxxxoxxxox"""
    double_time = """k x.x.x.x...x.x.x.
s ....x..g....x.gg
c ....x.......x...
h xxoxxxoxxxoxxxox"""
    return 140, [
        dict(chords=prog, drums=charge, bass="x.x.f.x.x.x.o.x.", leads=[("growl", 0, 0.34, drop), ("brass", 1, 0.18, drop)], stabs="x...x...x...x...", heavy=True, riser=True, fill=True),
        dict(chords=prog, drums=crush, bass="x.x.o.x.x.x.f.x.", leads=[("growl", 0, 0.42, drop), ("acid", 1, 0.24, scream)], stabs="x.....x.x...x...", heavy=True, fill=True),
        dict(chords=prog_b, drums=double_time, bass="x.o.f.x.x.o.f.x.", leads=[("growl", 0, 0.40, scream), ("glitch_heavy", 1, 0.24, drop), ("acid", 2, 0.16, scream)], stabs="x..x..x...x..x..", heavy=True, riser=True, fill=True),
        dict(chords=prog, drums=crush, bass="x.x.f.x.o.x.f.x.", leads=[("growl", 0, 0.44, scream), ("reese_heavy", 0, 0.26, drop), ("acid", 1, 0.26, scream)], stabs="x...x...x.x.x...", heavy=True, fill=True),
    ]


def track_boss_glitch():
    """Sour Circuit (Milestone Boss - Aggressive Electro Glitch Hop, 110 BPM, F minor): biting FM chops & hard breaks."""
    prog = [(41, m), (49, M), (44, M), (48, M)] * 2
    prog_b = [(41, m), (46, m), (49, M), (48, M)] * 2
    glitch_motif = [[(0, 2, 0), (2, 1, 3), (3, 1, 5), (4, 2, 4), (6, 2, 6), (8, 2, 2), (10, 1, 4), (11, 1, 5), (12, 2, 3), (14, 2, 6)],
                    [(0, 2, 2), (2, 2, 5), (4, 2, 6), (6, 2, 4), (8, 2, 3), (10, 1, 5), (11, 1, 7), (12, 4, 4)]]
    drums = """k x..x..x...x.x.x.
s ....x..g....x.gx
h xx.xxxoxxx.xxxox
m ............1231"""
    drums_b = """k x..x.x..x.x.x.x.
s ....x.gg....x.gx
c ....x.......x...
h xxoxxxoxxxoxxxox
m ........12312312"""
    return 110, [
        dict(chords=prog, drums=drums, bass="x.xo.fx.x.x.of.x.", swing=0.22, leads=[("glitch_heavy", 1, 0.34, glitch_motif), ("growl", 0, 0.28, glitch_motif)], stabs="x..x..x...x...x.", heavy=True, fill=True),
        dict(chords=prog, drums=drums_b, bass="x.xof.x.x.xof.xx.", swing=0.22, leads=[("glitch_heavy", 1, 0.36, glitch_motif), ("acid", 2, 0.22, glitch_motif), ("growl", 0, 0.30, glitch_motif)], stabs="x..x..x..x..x.x.", heavy=True, riser=True, fill=True),
        dict(chords=prog_b, drums=drums, bass="x.o.f.x.x.o.f.x.", swing=0.22, leads=[("acid", 1, 0.30, glitch_motif), ("brass", 1, 0.22, glitch_motif)], stabs="x...x...x.x.x...", heavy=True, fill=True),
        dict(chords=prog, drums=drums_b, bass="x.xof.x.o.f.x.xx.", swing=0.22, leads=[("glitch_heavy", 1, 0.38, glitch_motif), ("growl", 0, 0.34, glitch_motif), ("reese_heavy", 0, 0.24, glitch_motif)], stabs="x..x.x..x..x.x..", heavy=True, fill=True),
    ]


def track_boss_dnb():
    """Core Meltdown (Milestone Boss - Aggressive Techstep Drum & Bass, 174 BPM, E minor): tearing high-speed breaks."""
    prog = [(40, m), (48, M), (45, m), (47, M)] * 2
    prog_b = [(40, m), (41, M), (45, m), (47, M)] * 2
    arp = [[(0, 2, 0), (2, 1, 3), (3, 1, 5), (4, 2, 4), (6, 2, 6), (8, 2, 3), (10, 2, 5), (12, 2, 6), (14, 2, 7)],
           [(0, 2, 5), (2, 2, 3), (4, 2, 6), (6, 2, 7), (8, 2, 4), (10, 2, 5), (12, 2, 6), (14, 2, 4)]]
    dnb = """k x.......x.x.....
s ....x..g..g.x.gg
c ....x.......x...
h xxoxxxoxxxoxxxox"""
    dnb_b = """k x..x....x.x.....
s ....x.gg....x.gx
c ....x.......x...
h xxoxxxoxxxoxxxox
m ............1123"""
    return 174, [
        dict(chords=prog, drums=dnb, bass="x.x.f.x.x.o.f.x.", leads=[("reese_heavy", 0, 0.34, arp), ("acid", 1, 0.22, arp)], stabs="x.....x...x.....", heavy=True, fill=True),
        dict(chords=prog, drums=dnb_b, bass="x.o.f.x.x.x.f.o.", leads=[("reese_heavy", 0, 0.36, arp), ("acid", 2, 0.24, arp), ("brass", 1, 0.20, arp)], stabs="x...x...x...x...", heavy=True, riser=True, fill=True),
        dict(chords=prog_b, drums=dnb, bass="x.f.o.x.x.f.x.o.", leads=[("growl", 0, 0.34, arp), ("glitch_heavy", 2, 0.22, arp)], stabs="x..x..x...x..x..", heavy=True, fill=True),
        dict(chords=prog, drums=dnb_b, bass="x.x.o.f.x.o.f.x.", leads=[("reese_heavy", 0, 0.38, arp), ("acid", 1, 0.26, arp), ("growl", 0, 0.24, arp)], stabs="x...x.x.x...x.x.", heavy=True, fill=True),
    ]


def track_boss_neuro():
    """Slicer's Reckoning (Milestone Boss - Ferocious Neurofunk DnB, 176 BPM, C minor): relentless finale assault."""
    prog = [(48, m), (46, M), (44, M), (43, M)] * 2
    prog_b = [(48, m), (49, M), (46, m), (43, M)] * 2
    fury = [[(0, 2, 3), (2, 1, 5), (3, 1, 6), (4, 2, 4), (6, 2, 7), (8, 2, 5), (10, 1, 6), (11, 1, 8), (12, 2, 6), (14, 2, 7)],
            [(0, 2, 6), (2, 2, 4), (4, 2, 5), (6, 2, 7), (8, 2, 3), (10, 2, 5), (12, 2, 6), (14, 2, 8)]]
    neuro_drums = """k x..x....x.x.....
s ....x.gg....x.gg
c ....x.......x...
h xxoxxxoxxxoxxxox"""
    neuro_rush = """k x.x...x.x.x.x...
s ....x.gg..g.x.gg
c ....x.......x...
h xxoxxxoxxxoxxxox
m ............3211"""
    return 176, [
        dict(chords=prog, drums=neuro_drums, bass="x.o.f.x.x.o.f.x.", leads=[("reese_heavy", 0, 0.36, fury), ("growl", 0, 0.24, fury)], stabs="x...x...x...x...", heavy=True, fill=True),
        dict(chords=prog, drums=neuro_rush, bass="x.o.f.x.o.f.x.o.", leads=[("reese_heavy", 0, 0.38, fury), ("acid", 1, 0.26, fury), ("brass", 1, 0.22, fury)], stabs="x.....x.x.....x.", heavy=True, riser=True, fill=True),
        dict(chords=prog_b, drums=neuro_drums, bass="x.f.o.x.x.f.o.x.", leads=[("growl", 0, 0.36, fury), ("glitch_heavy", 2, 0.24, fury), ("acid", 1, 0.22, fury)], stabs="x..x..x.x..x..x.", heavy=True, fill=True),
        dict(chords=prog, drums=neuro_rush, bass="x.o.f.x.x.o.f.x.", leads=[("reese_heavy", 0, 0.40, fury), ("growl", 0, 0.28, fury), ("acid", 2, 0.26, fury)], stabs="x.x.x.x.x.x.x.x.", heavy=True, fill=True),
    ]


BOSS_TRACKS = (
    ("music_boss_seeds", track_seed_spitter, 601),
    ("music_boss_rind", track_rind_and_run, 602),
    ("music_boss_fuse", track_long_live_the_fuse, 603),
    ("music_boss_keg", track_powder_keg_court, 604),
    ("music_tension", track_tension_loop, 605),
    ("music_boss_dubstep", track_boss_dubstep, 606),
    ("music_boss_glitch", track_boss_glitch, 607),
    ("music_boss_dnb", track_boss_dnb, 608),
    ("music_boss_neuro", track_boss_neuro, 609),
)


def build_boss_music():
    for name, make, seed in BOSS_TRACKS:
        bpm, sections = make()
        x = compose_boss(bpm, sections, seed)
        y, lufs = level_boss(x)
        write_pcm(name, y, BOSS_RATE)
        print(f"{name}: {bpm} BPM, {len(y) / BOSS_RATE:.1f} s, was {lufs:.1f} LUFS")


# ---------------------------------------------------------------- everything
def build_audio():
    s = SFX_RATE
    # Older clip names that no longer exist are removed so they don't bloat the ROM.
    folder = os.path.dirname(path("romfs", "audio", "x"))
    # The Shop's recorded tracks (see the credits) are not generated, so they stay.
    recorded = ("music_summer", "music_hero", "music_trap", "music_unruly", "music_vengeance", "music_explorer",
                "music_ahead", "music_spare", "music_fright", "music_worthless", "music_lies", "music_liesvip", "music_staywithme", "music_enough",
                # the five found boss tracks (convert_shop_music.py found)
                "music_boss_minto", "music_boss_ansimuz", "music_boss_bounce", "music_boss_champion", "music_boss_epic")
    for f in os.listdir(folder):
        if os.path.splitext(f)[0] in recorded:
            continue
        os.remove(os.path.join(folder, f))

    write_pcm("swish0", swish(1, 0.17, 800, 3600), s)
    write_pcm("swish1", swish(2, 0.21, 650, 2900), s)
    write_pcm("swish2", swish(3, 0.14, 1000, 4600), s)
    write_pcm("shing", shing(), s)
    write_pcm("crisp0", cut_crisp(10), s)
    write_pcm("crisp1", cut_crisp(11), s)
    write_pcm("juicy0", cut_juicy(12), s)
    write_pcm("juicy1", cut_juicy(13), s)
    write_pcm("soft0", cut_soft(14), s)
    write_pcm("soft1", cut_soft(15), s)
    write_pcm("melon0", cut_melon(16), s)
    write_pcm("melon1", cut_melon(17), s)
    write_pcm("knock", knock(), s)
    write_pcm("shell", shell_break(), s)
    write_pcm("throw", norm(swish(19, 0.28, 300, 1100), 0.24), s)
    write_pcm("boom", boom(), s)
    write_pcm("fuse", fuse_loop(), s)
    for tier in (3, 4, 5, 6, 8, 10):
        write_pcm(f"combo{tier}", combo_stinger(tier), s)
    n = np.zeros(n_of(0.5))
    place(n, thump(180, 90, 0.16, 0.9), 0)
    place(n, seq([60, 55], 0.1, 0.35), 0.01, 0.35)
    write_pcm("strike", norm(n, 0.62), s)
    click = np.zeros(n_of(0.09))
    place(click, bell(1320, 0.08), 0)
    write_pcm("click", norm(click, 0.5), s)
    write_pcm("power", norm(reverb_tail(seq([79, 83, 86, 91], 0.05, 0.5)), 0.72), s)
    write_pcm("freeze", norm(reverb_tail(seq([96, 91, 88, 84, 79], 0.07, 0.7), 1.2, 0.4), 0.72), s)
    fever = np.zeros(n_of(1.6))
    place(fever, riser(0.5, 700, 9000, 6), 0, 0.5)
    place(fever, seq([72, 76, 79, 84, 88, 91, 96], 0.04, 0.8), 0.1)
    place(fever, chord_stab([60, 67, 72, 76, 84], 1.1), 0.5, 0.14)
    place(fever, thump(140, 50, 0.3, 1.4), 0.5)
    write_pcm("fever", norm(reverb_tail(fever, 1.0, 0.3), 0.92), s)
    write_pcm("go", norm(seq([72, 84], 0.12, 0.5), 0.6), s)
    write_pcm("over", norm(reverb_tail(seq([72, 67, 64, 60], 0.17, 0.8)), 0.68), s)
    write_pcm("best", norm(reverb_tail(seq([72, 76, 79, 84, 79, 84, 88], 0.1, 0.9)), 0.78), s)
    write_pcm("gold", norm(reverb_tail(seq([88, 93, 100], 0.045, 0.55)), 0.72), s)
    write_pcm("finale_rise", finale_rise(), s)
    write_pcm("finale_hit", finale_hit(), s)
    write_pcm("finale_burst", finale_burst(), s)
    # A coin thrown up: a short metallic ting that wobbles as it spins.
    coin = np.zeros(n_of(0.34))
    place(coin, bell(2350, 0.3), 0)
    place(coin, bell(3170, 0.2), 0.012, 0.45)
    t = np.arange(len(coin)) / s
    coin *= 0.55 + 0.45 * np.abs(np.cos(2 * np.pi * 9 * t))
    write_pcm("coin", norm(coin, 0.46), s)
    # A coin caught: a bright two-note plink.
    plink = np.zeros(n_of(0.5))
    place(plink, bell(1976, 0.22), 0)
    place(plink, bell(2637, 0.34), 0.055)
    write_pcm("plink", norm(reverb_tail(plink, 0.5, 0.18), 0.6), s)

    # The Shop's door bell: a little brass bell on a spring, rung a few times as the door swings.
    def shop_bell(f0, dur):
        t = np.arange(n_of(dur)) / s
        partials = ((1.0, 1.0, 2.2), (2.32, 0.5, 4.0), (4.25, 0.28, 7.0), (6.63, 0.12, 11.0))
        tone = sum(a * np.sin(2 * np.pi * f0 * h * t) * np.exp(-t * d) for h, a, d in partials)
        tink = np.random.default_rng(int(f0)).normal(0, 1, len(t)) * np.exp(-t * 400) * 0.25
        return (tone + band(tink, 4000, 9000)) * np.minimum(1, t / 0.0015)
    door = np.zeros(n_of(1.6))
    for at, gain, f0 in ((0.0, 1.0, 1760), (0.085, 0.7, 1745), (0.16, 0.55, 1770), (0.25, 0.35, 1752), (0.37, 0.2, 1764)):
        place(door, shop_bell(f0, 1.2), at, gain)
    write_pcm("shopbell", norm(reverb_tail(door, 0.6, 0.2), 0.55), s)
    # Orchard Blast.
    write_pcm("clang", clang(), s)
    write_pcm("open", open_chime(), s)
    write_pcm("roar", roar(), s)
    write_pcm("bossin", boss_intro(), s)
    write_pcm("victory", victory(), s)
    write_pcm("pop", pop(), s)
    write_pcm("laugh", laugh(), s)
    write_pcm("chuckle", chuckle(), s)
    write_pcm("whoosh", whoosh(), s)
    write_pcm("launch", launch(), s)
    write_pcm("levelup", levelup(), s)
    # Clear speaker-audible boss voice dialogue blip (Room 25 encounter): warm midrange + soft harmonics.
    dur_blip = 0.062
    bt = np.arange(n_of(dur_blip)) / s
    bf = 560.0 - 110.0 * (bt / dur_blip) ** 0.75
    bph = 2 * np.pi * np.cumsum(bf) / s
    tri = (2.0 / np.pi) * np.arcsin(np.sin(bph))
    tone = 0.62 * np.sin(bph) + 0.28 * tri + 0.16 * np.sin(2.0 * bph) + 0.05 * np.sin(3.0 * bph)
    tone = band(tone, 180, 2800, s)
    atk = 0.5 - 0.5 * np.cos(np.pi * np.minimum(1.0, bt / 0.006))
    rel = np.minimum(1.0, np.maximum(0.0, (dur_blip - bt) / 0.016)) ** 1.25
    blip = tone * atk * np.exp(-bt / 0.032) * rel
    write_pcm("boss_blip", norm(blip, 0.56), s)

    # Steam train whistle for Milestone Boss Phase 2 enrage (<= 50% HP): dual-tone locomotive chord + steam hiss.
    rng_tw = np.random.default_rng(701)
    dur_tw = 0.85
    tw_t = np.arange(n_of(dur_tw)) / s
    vib = 1.0 + 0.008 * np.sin(2 * np.pi * 6.5 * tw_t)
    bend = 0.96 + 0.04 * np.minimum(1.0, tw_t / 0.08)
    tw_sig = np.zeros_like(tw_t)
    for f0, g0 in ((698.46, 0.45), (830.61, 0.40), (1046.50, 0.35), (1396.91, 0.15)):
        ph = 2 * np.pi * np.cumsum(f0 * bend * vib) / s
        tw_sig += g0 * (np.sin(ph) + 0.22 * np.sin(2 * ph))
    steam = band(rng_tw.normal(0, 1, len(tw_t)), 1400, 4800, s) * 0.22
    gate = (1.0 - np.exp(-tw_t / 0.025)) * np.minimum(1.0, np.maximum(0.0, (dur_tw - tw_t) / 0.12))
    # Two dramatic puffs: short blast then longer wail.
    pulse = np.where((tw_t >= 0.19) & (tw_t < 0.25), np.exp(-(tw_t - 0.19) / 0.02), 1.0)
    write_pcm("train_whistle", norm(reverb_tail((tw_sig + steam) * gate * pulse, 0.45, 0.16), 0.78), s)

    # Bubbly splash sound for Lord Lemon's drain rising in Lemonade.
    dur_bub = 0.72
    bub = np.zeros(n_of(dur_bub))
    rng_bub = np.random.default_rng(702)
    for bi in range(11):
        at = 0.02 + bi * 0.056 + rng_bub.uniform(-0.012, 0.012)
        bf0 = rng_bub.uniform(360, 620) * (1.0 + bi * 0.04)
        bdur = rng_bub.uniform(0.045, 0.072)
        bn = n_of(bdur)
        bt2 = np.arange(bn) / s
        b_sw = np.sin(2 * np.pi * np.cumsum(np.geomspace(bf0, bf0 * 2.15, bn)) / s)
        b_env = (1.0 - np.exp(-bt2 / 0.003)) * np.exp(-bt2 / (bdur * 0.45))
        place(bub, b_sw * b_env, at, rng_bub.uniform(0.55, 1.0))
    write_pcm("bubbles", norm(reverb_tail(bub, 0.35, 0.15), 0.72), s)

    # 2.1.0 Achievement toast SFX: slow swoosh-in, retro pixel jingle, slow swoosh-out, and Mythic drop fanfare.
    rng_ach = np.random.default_rng(210)
    # ach_in: soft airy rising swoosh (~0.48s)
    dur_sw = 0.48
    t_sw = np.arange(n_of(dur_sw)) / s
    noise_in = rng_ach.normal(0.0, 1.0, len(t_sw))
    env_in = np.sin(np.pi * (t_sw / dur_sw)) ** 1.3 * (0.3 + 0.7 * (t_sw / dur_sw))
    tone_in = np.sin(2 * np.pi * np.cumsum(320.0 + 380.0 * (t_sw / dur_sw)) / s)
    write_pcm("ach_in", norm((noise_in * 0.28 + tone_in * 0.55) * env_in, 0.55), s)

    # ach_jingle: bright 4-note retro pixel arpeggio (~0.62s)
    jin = np.zeros(n_of(0.62))
    for i, midi in enumerate((76, 80, 83, 88)):
        nt = 0.18 if i < 3 else 0.34
        lt = np.arange(n_of(nt)) / s
        f = mtof(midi)
        sq = np.tanh(2.5 * np.sin(2 * np.pi * f * lt)) + 0.35 * np.sin(4 * np.pi * f * lt)
        env = np.exp(-lt / (0.08 if i < 3 else 0.14)) * np.minimum(1.0, lt / 0.004)
        place(jin, sq * env, i * 0.075, 0.65)
    write_pcm("ach_jingle", norm(reverb_tail(jin, 0.25, 0.12), 0.72), s)

    # ach_out: soft falling swoosh (~0.48s)
    noise_out = rng_ach.normal(0.0, 1.0, len(t_sw))
    env_out = np.sin(np.pi * (t_sw / dur_sw)) ** 1.3 * (1.0 - 0.65 * (t_sw / dur_sw))
    tone_out = np.sin(2 * np.pi * np.cumsum(680.0 - 360.0 * (t_sw / dur_sw)) / s)
    write_pcm("ach_out", norm((noise_out * 0.26 + tone_out * 0.50) * env_out, 0.52), s)

    # mythic_drop: shimmering mythic fanfare (~1.1s)
    myt = np.zeros(n_of(1.1))
    for i, midi in enumerate((72, 76, 79, 84, 88, 91)):
        place(myt, bell(mtof(midi), 0.55), i * 0.055, 0.75)
    write_pcm("mythic_drop", norm(reverb_tail(myt, 0.65, 0.24), 0.80), s)

    # camera: crisp mechanical shutter double-click + bright flash discharge (~0.24s)
    cam = np.zeros(n_of(0.24))
    t_sh1 = np.arange(n_of(0.035)) / s
    sh1 = band(rng_ach.normal(0.0, 1.0, len(t_sh1)), 1200, 7500, s) * np.exp(-t_sh1 / 0.008)
    sh1 += np.sin(2 * np.pi * (2400.0 * np.exp(-t_sh1 / 0.012)) * t_sh1) * np.exp(-t_sh1 / 0.006) * 0.75
    place(cam, sh1, 0.0, 0.95)
    t_sh2 = np.arange(n_of(0.032)) / s
    sh2 = band(rng_ach.normal(0.0, 1.0, len(t_sh2)), 1500, 8500, s) * np.exp(-t_sh2 / 0.007)
    sh2 += np.sin(2 * np.pi * (2900.0 * np.exp(-t_sh2 / 0.010)) * t_sh2) * np.exp(-t_sh2 / 0.005) * 0.65
    place(cam, sh2, 0.048, 0.85)
    t_fl = np.arange(n_of(0.14)) / s
    fl = band(rng_ach.normal(0.0, 1.0, len(t_fl)), 2200, 11000, s) * np.exp(-t_fl / 0.032) * 0.35
    fl += np.sin(2 * np.pi * (5200.0 + 1400.0 * np.exp(-t_fl / 0.04)) * t_fl) * np.exp(-t_fl / 0.025) * 0.12
    place(cam, fl, 0.010, 0.65)
    write_pcm("camera", norm(cam, 0.82), s)

    # Original tracks: all slow, soft and major-leaning.
    menu = [[60, 64, 67, 71, 74], [57, 60, 64, 67, 71], [65, 69, 72, 76], [55, 59, 62, 67, 69]]
    play = [[62, 66, 69, 73], [59, 62, 66, 69], [67, 71, 74, 78], [64, 67, 71, 74]]
    zen = [[60, 67, 69, 74], [65, 69, 72, 79], [57, 64, 67, 71], [62, 65, 69, 76]]
    write_pcm("music_menu", compose_calm(62, 8, menu, [72, 74, 76, 79, 81, 84, 86, 88], 5, 0.42), MUSIC_RATE)
    write_pcm("music_play", compose_calm(84, 8, play, [74, 76, 78, 81, 83, 86, 88, 90], 12, 0.5, 0.18, 0.3, 0.35, 0.018), MUSIC_RATE)
    write_pcm("music_zen", compose_calm(50, 8, zen, [72, 74, 79, 81, 84, 86, 91], 31, 0.3, 0.26, 0.26, 0.6), MUSIC_RATE)
    # Calm.
    meadow = [[55, 59, 62, 66], [52, 55, 59, 62], [60, 64, 67, 71], [62, 66, 69, 72]]
    lantern = [[53, 57, 60, 64], [50, 53, 57, 60], [58, 62, 65, 69], [60, 64, 67, 70]]
    write_pcm("music_meadow", compose_calm(56, 8, meadow, [71, 74, 76, 78, 79, 83, 86], 41, 0.4), MUSIC_RATE)
    write_pcm("music_lantern", compose_calm(68, 8, lantern, [69, 72, 74, 77, 79, 81, 84], 52, 0.46, 0.2, 0.3, 0.38, 0.012, pluck_note), MUSIC_RATE)
    # Zen: open fifths, five-note scales and a lot of air.
    moss = [[50, 57, 62], [55, 62, 67], [47, 54, 62], [52, 59, 64]]
    bamboo = [[45, 52, 57], [41, 48, 57], [43, 50, 55], [40, 47, 52]]
    clouds = [[48, 55, 62], [53, 60, 65], [50, 57, 64], [46, 53, 60]]
    write_pcm("music_moss", compose_calm(44, 8, moss, [74, 76, 78, 81, 83, 86], 63, 0.2, 0.26, 0.22, 0.65, 0.0, bowl_note, 0.07), MUSIC_RATE)
    write_pcm("music_bamboo", compose_calm(48, 8, bamboo, [69, 72, 74, 76, 79, 81], 74, 0.26, 0.2, 0.3, 0.55, 0.0, flute_note, 0.0, 0.05), MUSIC_RATE)
    write_pcm("music_clouds", compose_calm(40, 8, clouds, [72, 74, 79, 81, 84, 86], 85, 0.12, 0.3, 0.2, 0.7, 0.0, bowl_note, 0.05, 0.03), MUSIC_RATE)
    # Boss tracks: busier, at their own rate, levelled to the same loudness.
    build_boss_music()

    # The HOME Menu banner sound: two quick slices, a clean finishing cut, then a bright little jingle.
    # Banner sound can be at most three seconds long.
    rate = 44100
    n = np.zeros(n_of(2.8))
    place(n, swish(21, 0.17, 800, 3600), 0.02, 0.8)
    place(n, cut_juicy(22), 0.08, 0.75)
    place(n, swish(23, 0.15, 1000, 4200), 0.36, 0.8)
    place(n, cut_crisp(24), 0.42, 0.75)
    place(n, swish(25, 0.21, 650, 2900), 0.72, 0.9)
    place(n, cut_melon(26), 0.8, 0.9)
    place(n, shing(), 0.8, 0.55)
    place(n, seq([72, 76, 79, 84, 88], 0.11, 1.2), 1.02, 0.75)
    n = reverb_tail(n, 0.8, 0.22)[:n_of(2.8)]
    n = norm(n, 0.82) * np.minimum(1, np.linspace(9, 0, len(n)))
    jingle = np.interp(np.arange(int(len(n) * rate / s)) * s / rate, np.arange(len(n)), n)
    with wave.open(path("meta", "banner.wav"), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(np.repeat((jingle * 32000).astype("<i2"), 2).tobytes())
