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


# ---------------------------------------------------------------- everything
def build_audio():
    s = SFX_RATE
    # Older clip names that no longer exist are removed so they don't bloat the ROM.
    folder = os.path.dirname(path("romfs", "audio", "x"))
    for f in os.listdir(folder):
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
