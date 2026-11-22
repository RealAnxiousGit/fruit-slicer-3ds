#!/usr/bin/env python3
"""Generates the 'Fruit Slicer: Orchard Blast' v2.0 update promo logo using gen_assets's exact vector logo style."""
import os
import sys

import numpy as np
from PIL import Image, ImageChops, ImageDraw

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
import gen_assets as g  # noqa: E402

ROOT = os.path.abspath(os.path.join(TOOLS_DIR, ".."))
REPO_ROOT = os.path.abspath(os.path.join(ROOT, ".."))


def gradient_fill(mask, top_rgb, bot_rgb):
    bbox = mask.getbbox()
    y0, y1 = (bbox[1], bbox[3]) if bbox else (0, mask.height)
    ys = np.arange(mask.height, dtype=np.float32)
    t = np.clip((ys - y0) / max(1.0, float(y1 - y0)), 0.0, 1.0)[:, None]
    top = np.array(top_rgb, dtype=np.float32)
    bot = np.array(bot_rgb, dtype=np.float32)
    rows = (top[None, :] * (1.0 - t) + bot[None, :] * t).astype(np.uint8)
    fill = np.zeros((mask.height, mask.width, 4), np.uint8)
    fill[..., :3] = rows[:, None, :]
    fill[..., 3] = np.array(mask)
    return Image.fromarray(fill, "RGBA")


def build_orchard_blast_logo():
    w, h = 520, 224
    ss = g.SS

    # Top title block: "FRUIT" / "SLICER" in exact citrus-to-berry gradient with diagonal blade cut.
    title_raw = Image.new("RGBA", (w * ss, h * ss), (0, 0, 0, 0))
    td = ImageDraw.Draw(title_raw)
    g.stroke_text(td, "FRUIT", w / 2, 42, 50, (255, 255, 255, 255), 11.0)
    g.stroke_text(td, "SLICER", w / 2, 102, 50, (255, 255, 255, 255), 11.0)

    tm = g.mask_of(title_raw)
    t_fill = gradient_fill(tm, [255, 208, 66], [240, 66, 112])

    # Bottom subtitle block: "ORCHARD BLAST" in lush orchard-melon green-to-gold gradient.
    sub_raw = Image.new("RGBA", (w * ss, h * ss), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sub_raw)
    g.stroke_text(sd, "ORCHARD BLAST", w / 2, 168, 34, (255, 255, 255, 255), 8.2)

    sm = g.mask_of(sub_raw)
    s_fill = gradient_fill(sm, [175, 248, 105], [70, 205, 118])

    combined_mask = ImageChops.lighter(tm, sm)
    edge = g.dilate(combined_mask, 3.6 * ss)
    outer = g.dilate(combined_mask, 6.2 * ss)

    out = Image.new("RGBA", (w * ss, h * ss), (0, 0, 0, 0))
    out.alpha_composite(g.recolor(outer, g.CREAM))
    out.alpha_composite(g.recolor(edge, g.INK))
    out.alpha_composite(t_fill)
    out.alpha_composite(s_fill)

    # Diagonal blade slash across "FRUIT SLICER" matching spr_logo().
    slash = Image.new("L", out.size, 0)
    ImageDraw.Draw(slash).polygon(
        [(0, 88 * ss), (w * ss, 50 * ss), (w * ss, 54.0 * ss), (0, 92.5 * ss)], fill=255
    )
    t_edge = g.dilate(tm, 3.6 * ss)
    slash_masked = ImageChops.multiply(slash, g.dilate(tm, 6.2 * ss))
    clear = Image.new("RGBA", out.size, (0, 0, 0, 0))
    out.paste(clear, (0, 0), slash_masked)

    glow = Image.new("L", out.size, 0)
    ImageDraw.Draw(glow).polygon(
        [(0, 89.2 * ss), (w * ss, 51.2 * ss), (w * ss, 52.8 * ss), (0, 90.8 * ss)], fill=255
    )
    glow = ImageChops.multiply(glow, t_edge)
    out.alpha_composite(g.recolor(glow, (255, 255, 255, 235)))

    bbox = out.getbbox()
    if bbox:
        pad = 8 * ss
        x0 = max(0, bbox[0] - pad)
        y0 = max(0, bbox[1] - pad)
        x1 = min(out.width, bbox[2] + pad)
        y1 = min(out.height, bbox[3] + pad)
        out = out.crop((x0, y0, x1, y1))

    target_w = 1280
    target_h = max(1, round(out.height * target_w / out.width))
    return out.resize((target_w, target_h), Image.LANCZOS)


def main():
    logo = build_orchard_blast_logo()
    out_paths = [
        os.path.join(REPO_ROOT, "FruitSlicerPromo", "Fruit Slicer Orchard Blast Logo.png"),
        os.path.join(ROOT, "docs", "store", "logo_orchard_blast.png"),
    ]
    for p in out_paths:
        os.makedirs(os.path.dirname(p), exist_ok=True)
        logo.save(p, "PNG", optimize=True)
        print(f"wrote {p} ({logo.width}x{logo.height})")


if __name__ == "__main__":
    main()
