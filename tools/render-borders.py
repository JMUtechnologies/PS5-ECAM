#!/usr/bin/env python3
"""The 3DS side's border themes (Settings > Borders, and the 3DS in-game menu): one background
picture each, drawn around the screens wherever the layout puts them. The frames around each screen
(a soft shadow, a hairline, and on Shell a recessed ring) are drawn live by port/app/ingame3ds.cpp,
so one picture fits every layout, swapped or not.

    render-borders.py OUTPUT_FOLDER

Writes midnight.tga, waves.tga, aurora.tga, shell.tga and ps5cemu-har.tga: 960x540, 24-bit,
uncompressed, top row first. They are smooth enough to be scaled up to 4K. Needs NumPy and Pillow. The pictures are the
app's own, drawn here: nothing is taken from a console or a game."""
import importlib.util
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W, H = 3840, 2160
OUT_W, OUT_H = 960, 540


def vgrad(top, bottom, h=H, w=W):
    t = np.linspace(0, 1, h)[:, None, None]
    a, b = np.array(top, float), np.array(bottom, float)
    return np.broadcast_to(a + (b - a) * t, (h, w, 3)).copy()


def vignette(img, strength):
    y, x = np.ogrid[:H, :W]
    d = np.sqrt(((x - W / 2) / (W / 2)) ** 2 + ((y - H / 2) / (H / 2)) ** 2) / math.sqrt(2)
    return img * (1 - strength * d[..., None] ** 1.6)


def blob(img, cx, cy, radius, colour, alpha):
    small = Image.new("L", (W // 8, H // 8), 0)
    ImageDraw.Draw(small).ellipse([(cx - radius) / 8, (cy - radius) / 8, (cx + radius) / 8, (cy + radius) / 8], fill=255)
    m = np.asarray(small.filter(ImageFilter.GaussianBlur(radius / 16)).resize((W, H), Image.BILINEAR), float)[..., None] / 255 * alpha
    return img * (1 - m) + np.array(colour, float) * m


def midnight():
    img = vgrad((16, 21, 30), (6, 8, 12))
    img = blob(img, W * 0.25, -H * 0.2, 1500, (40, 60, 90), 0.35)  # a little light from above
    return vignette(img, 0.35)


def waves():
    # the launcher's 3DS gold (port/frontend/wave.cpp), much darker, so it sits back
    img = vgrad((255, 204, 64), (236, 158, 22)) * 0.15
    pil = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).convert("RGBA")
    for top, length, amp, colour, alpha in [(1480, 2560, 60, (255, 214, 120), 0.035), (1640, 1920, 48, (255, 222, 140), 0.04),
                                            (1800, 1440, 36, (255, 230, 160), 0.045)]:
        layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        pts = [(x, top + amp * (1 - math.cos(2 * math.pi * (x + top) / length))) for x in range(0, W + 8, 8)]
        ImageDraw.Draw(layer).polygon([(0, H)] + pts + [(W, H)], fill=colour + (int(255 * alpha),))
        ImageDraw.Draw(layer).line(pts, fill=colour + (int(255 * alpha * 2.2),), width=12)
        pil = Image.alpha_composite(pil, layer)
    return vignette(np.asarray(pil.convert("RGB"), float), 0.30)


def aurora():
    img = vgrad((12, 15, 30), (7, 8, 16))
    img = blob(img, W * 0.12, H * 0.15, 1300, (30, 170, 160), 0.30)
    img = blob(img, W * 0.92, H * 0.90, 1400, (110, 80, 230), 0.28)
    img = blob(img, W * 0.70, H * 0.05, 900, (230, 110, 150), 0.12)
    return vignette(img, 0.30)


def shell():
    # a matte slate faceplate, lit from above (its grain is kept coarse, for the smaller picture)
    img = vgrad((52, 60, 72), (34, 39, 47))
    rng = np.random.default_rng(7)
    grain = rng.normal(0, 1.6, (OUT_H, OUT_W))
    grain = np.asarray(Image.fromarray(np.uint8(np.clip(grain + 128, 0, 255))).resize((W, H)), float) - 128
    return vignette(img + grain[..., None], 0.22)


def har():
    """PS5CEMU-HAR's own: its two sides as its start screen and banner have them, the Wii U Homebrew
    Launcher's blue and its bubbles on the left (tools/render-background.py's), the 3DS Homebrew
    Launcher's gold waves on the right, the two blending in the middle; dark, so it sits back."""
    spec = importlib.util.spec_from_file_location("render_background", os.path.join(os.path.dirname(os.path.abspath(__file__)), "render-background.py"))
    hbl = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(hbl)
    blue = vgrad(hbl.TOP, hbl.BOTTOM)
    for x, y, radius, alpha in hbl.particles():
        cx, cy, r = x * W, y * H, radius * W
        if r < 1:
            continue
        x0, x1, y0, y1 = int(max(0, cx - r - 2)), int(min(W, cx + r + 3)), int(max(0, cy - r - 2)), int(min(H, cy + r + 3))
        if x0 >= x1 or y0 >= y1:
            continue
        yy, xx = np.mgrid[y0:y1, x0:x1]
        coverage = np.clip(r - np.hypot(xx + 0.5 - cx, yy + 0.5 - cy) + 0.5, 0, 1)[..., None] * alpha
        blue[y0:y1, x0:x1] = blue[y0:y1, x0:x1] * (1 - coverage) + 255 * coverage
    blue *= 0.30
    sand = Image.fromarray(np.clip(vgrad((255, 204, 64), (236, 158, 22)) * 0.26, 0, 255).astype(np.uint8)).convert("RGBA")
    for top, length, amp, colour, alpha in [(1380, 2560, 60, (255, 214, 120), 0.06), (1580, 1920, 48, (255, 222, 140), 0.07),
                                            (1780, 1440, 36, (255, 230, 160), 0.08)]:
        layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        pts = [(x, top + amp * (1 - math.cos(2 * math.pi * (x + top) / length))) for x in range(0, W + 8, 8)]
        ImageDraw.Draw(layer).polygon([(0, H)] + pts + [(W, H)], fill=colour + (int(255 * alpha),))
        ImageDraw.Draw(layer).line(pts, fill=colour + (int(255 * alpha * 2.2),), width=12)
        sand = Image.alpha_composite(sand, layer)
    gold = np.asarray(sand.convert("RGB"), float)
    t = np.clip((np.arange(W) / W - 0.40) / 0.20, 0, 1)
    t = (t * t * (3 - 2 * t))[None, :, None]  # smoothstep: the sides meet softly in the middle
    return vignette(blue * (1 - t) + gold * t, 0.30)


THEMES = {"midnight": midnight, "waves": waves, "aurora": aurora, "shell": shell, "ps5cemu-har": har}


def write_tga(path, rgb):
    """Uncompressed 24-bit TGA, top row first (descriptor bit 5), as port/azahar/core.cpp reads it."""
    h, w, _ = rgb.shape
    header = bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, w & 255, w >> 8, h & 255, h >> 8, 24, 0x20])
    with open(path, "wb") as f:
        f.write(header)
        f.write(np.ascontiguousarray(rgb[..., ::-1]).tobytes())  # BGR


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "port/azahar/borders"
    os.makedirs(out, exist_ok=True)
    for name, make in THEMES.items():
        image = Image.fromarray(np.clip(make(), 0, 255).astype(np.uint8)).resize((OUT_W, OUT_H), Image.LANCZOS)
        write_tga(os.path.join(out, f"{name}.tga"), np.asarray(image))
        print("wrote", name)


if __name__ == "__main__":
    main()
