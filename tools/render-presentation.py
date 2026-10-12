#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw PS5CEMU-HAR's home-screen art: its background, sce_sys/pic0.dds, and its tile,
sce_sys/icon0.png.

    render-presentation.py [--title-font FONT] SCE_SYS_DIR [PREVIEW_PNG]

The PS5 shows pic0.dds behind the app while it is selected on the home screen, and pic1.dds while
it starts; tools/package.sh installs this one image as both. It is the README's banner
(tools/render-banner.py, whose layout this reads) made the size of the screen: the app's three sides
in three equal bands, in the order of the launcher's switch. Cemu's dark Wii U Homebrew Launcher blue
with its bubbles (tools/render-background.py), Azahar's dark gold with the 3DS Homebrew Launcher's
waves, and melonDS's dark green with its rounded pixels rising; the Wii U GamePad, the 3DS and the DS
Lite each in the middle of its own band (tools/render-icons.py's), glowing in its side's colour; and
under them the app's name, large, over the tagline. It sits above the middle: the shell draws its own
title, a Play button and its shading over the lower left. icon0.png, the tile, is the same in a
square: the three devices above the name.

The console takes a single 3840x2160 BC7_UNORM DX10 DDS without mipmaps
(ps5-native-app-boilerplate's tools/validate-assets.sh), which this encodes itself in BC7's mode 6:
per 4x4 block, a line between two colours, along the block's principal axis, and a 4-bit position
on it for each pixel. The tile is a 512x512 opaque PNG; the console rounds its corners.

This needs Pillow and numpy, and the banner's fonts: the name's is Typodermic's Zrnic, which its
licence lets the developer use for fixed artwork like these images but not share, so it is not in
this repository: --title-font names the developer's own copy (zrnic rg.otf; without it, Segoe UI
Black from Windows, or DejaVu Sans Bold, stand in); the tagline's is Segoe UI Semibold (the launcher's
Lexend SemiBold, tools/fonts, elsewhere). The output is committed (sce_sys/pic0.dds, sce_sys/icon0.png), so the build needs none
of them. The devices take a minute or so: render-icons.py draws them a pixel at
a time.
"""

import importlib.util
import os
import random
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 3840, 2160
HALF = W // 2
FONT_DIRS = ["C:/Windows/Fonts", "/mnt/c/Windows/Fonts"]
HEAVY = [os.path.join(d, "seguibl.ttf") for d in FONT_DIRS] + ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]
# the tagline: Segoe UI Semibold from Windows, else the launcher's own Lexend at its semibold weight
LEXEND = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts", "Lexend[wght].ttf")
SEMIBOLD = [os.path.join(d, "seguisb.ttf") for d in FONT_DIRS] + [LEXEND, "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]
TAGLINE = (0xe6, 0xee, 0xf6)


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(os.path.dirname(os.path.abspath(__file__)), file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ICONS = load("render_icons", "render-icons.py")
HBL = load("render_background", "render-background.py")
BANNER = load("render_banner", "render-banner.py")


def rgb(hex_colour):
    return tuple(int(hex_colour[i:i + 2], 16) for i in (1, 3, 5))


class Banner:
    """render-banner.py's picture (1280x320) at scale, its (0, 0) at origin: where its pieces go."""

    def __init__(self, scale, origin):
        self.scale, self.origin = scale, origin

    def x(self, value):
        return self.origin[0] + value * self.scale

    def y(self, value):
        return self.origin[1] + value * self.scale


def gradient(width, height, top, bottom, t0=0.0, t1=1.0):
    """A top-to-bottom gradient, t0 to t1 of the way between the two colours (clamped)."""
    t = np.clip(t0 + (t1 - t0) * (np.arange(height, dtype=np.float64)[:, None] + 0.5) / height, 0.0, 1.0)
    image = np.empty((height, width, 3))
    for c in range(3):
        image[:, :, c] = top[c] + (bottom[c] - top[c]) * t
    return image


def bubbles(width, height, banner, x0):
    """Cemu's band: the Wii U Homebrew Launcher's background where the banner shows it (its discs
    at x * 1280 - 320, its rows as they are), at this scale, darkened as the banner's."""
    bx0, by0 = (x0 - banner.origin[0]) / banner.scale, (0 - banner.origin[1]) / banner.scale
    bh = height / banner.scale
    image = gradient(width, height, HBL.TOP, HBL.BOTTOM, by0 / 720.0, (by0 + bh) / 720.0)
    s = banner.scale
    for x, y, radius, alpha in HBL.particles():
        cx, cy, r = (x * 1280 - 320 - bx0) * s, (y * 720 - by0) * s, radius * 1280 * s
        if r < 0.5 or cx + r < 0 or cx - r > width or cy + r < 0 or cy - r > height:
            continue
        xa, xb = max(0, int(cx - r - 1)), min(width, int(cx + r + 2))
        ya, yb = max(0, int(cy - r - 1)), min(height, int(cy + r + 2))
        px = np.arange(xa, xb) + 0.5 - cx
        py = np.arange(ya, yb)[:, None] + 0.5 - cy
        a = alpha * np.clip(r - np.hypot(px, py) + 0.5, 0.0, 1.0)
        part = image[ya:yb, xa:xb]
        part += (255.0 - part) * a[:, :, None]
    return image * (1.0 - BANNER.OVERLAY)


def sand(width, height, banner, x0, seam):
    """Azahar's band: the 3DS Homebrew Launcher's gold, darkened, and the banner's waves from its seam
    (seam: the banner's x where the band starts), fading back into the ground below the banner."""
    image = gradient(width, height, BANNER.WAVE_TOP, BANNER.WAVE_BOTTOM) * (1.0 - BANNER.WAVE_OVERLAY)
    ground = image.copy()
    fx = (np.arange(width, dtype=np.float64)[None, :] + 0.5 + x0 - banner.x(seam)) / banner.scale
    fy = np.arange(height, dtype=np.float64)[:, None] + 0.5
    for top, length, amplitude, colour, alpha in BANNER.WAVES:
        surface = banner.y(top + amplitude * (1.0 - np.cos(2.0 * np.pi * fx / length)))
        a = np.clip(fy - surface + 0.5, 0.0, 1.0) * alpha
        image += (np.array(rgb(colour), dtype=np.float64) - image) * a[:, :, None]
    fade = np.clip((fy - banner.y(BANNER.H)) / (180.0 * banner.scale), 0.0, 1.0) * 0.75
    return image + (ground - image) * fade[:, :, None]


def melon(width, height, banner, x0, squares):
    """melonDS's band: its green, darkened, and rounded pixels rising: squares (x, y, size, alpha in
    the banner's pixels)."""
    image = gradient(width, height, BANNER.MELON_TOP, BANNER.MELON_BOTTOM) * (1.0 - BANNER.MELON_OVERLAY)
    colour = np.array(rgb(BANNER.GREEN["accent"]), dtype=np.float64)
    for x, y, size, alpha in squares:
        cx, cy, half = banner.x(x) - x0, banner.y(y), size * banner.scale / 2
        radius = size * banner.scale * 0.22
        xa, xb = max(0, int(cx - half - 1)), min(width, int(cx + half + 2))
        ya, yb = max(0, int(cy - half - 1)), min(height, int(cy + half + 2))
        if xa >= xb or ya >= yb:
            continue
        px = np.abs(np.arange(xa, xb) + 0.5 - cx) - (half - radius)
        py = np.abs(np.arange(ya, yb)[:, None] + 0.5 - cy) - (half - radius)
        d = np.hypot(np.maximum(px, 0), np.maximum(py, 0)) + np.minimum(np.maximum(px, py), 0) - radius
        a = alpha * np.clip(0.5 - d, 0.0, 1.0)
        part = image[ya:yb, xa:xb]
        part += (colour - part) * a[:, :, None]
    return image


def banner_squares(top, bottom):
    """render-banner.py's pixels (the same scatter, by its seed), and more of them at the banner's
    density above and below it, from top to bottom (the banner's y)."""
    rng = random.Random(1144)
    squares = []
    for _ in range(26):
        size = rng.uniform(9, 30)
        x, y = rng.uniform(BANNER.SEAMS[1] - 10, BANNER.W + 10), rng.uniform(-10, BANNER.H + 10)
        squares.append((x, y, size, rng.uniform(0.14, 0.42)))
    more = random.Random(1145)
    for low, high in ((top, -10), (BANNER.H + 10, bottom)):
        for _ in range(max(0, round(26 * (high - low) / (BANNER.H + 20)))):
            size = more.uniform(9, 30)
            x, y = more.uniform(BANNER.SEAMS[1] - 10, BANNER.W + 10), more.uniform(low, high)
            squares.append((x, y, size, more.uniform(0.14, 0.42)))
    return squares


def background(width, height, banner, squares):
    """The three bands, side by side, and the soft seams between them, brightest at the banner's middle."""
    left, middle = (round(banner.x(seam)) for seam in BANNER.SEAMS)
    image = np.concatenate([bubbles(left, height, banner, 0), sand(middle - left, height, banner, left, BANNER.SEAMS[0]),
                            melon(width - middle, height, banner, middle, squares)], axis=1)
    seam = max(2, round(banner.scale * 2))
    fade = np.clip(1.0 - np.abs(np.arange(height) + 0.5 - banner.y(BANNER.H / 2)) / (height * 0.5), 0.0, 1.0)[:, None, None] * 0.45
    for x in (left, middle):
        part = image[:, x - seam // 2:x + seam // 2]
        part += (255.0 - part) * fade
    return Image.fromarray(np.clip(np.rint(image), 0, 255).astype(np.uint8), "RGB").convert("RGBA")


def glows(size, banner, row_y, unit):
    """Each console's glow in its side's colour, as the banner's (an ellipse 150 by 80 and blurred 34 for
    a device 172 wide), on the row, for devices unit wide."""
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    k = unit / BANNER.T
    for cx, palette, opacity in zip(BANNER.CENTRES, (BANNER.BLUE, BANNER.GOLD, BANNER.GREEN), (0.34, 0.32, 0.28)):
        single = Image.new("RGBA", size, (0, 0, 0, 0))
        x = banner.x(cx)
        ImageDraw.Draw(single).ellipse((x - 150 * k, row_y - 80 * k, x + 150 * k, row_y + 80 * k),
                                       fill=rgb(palette["accent"]) + (round(255 * opacity),))
        layer = Image.alpha_composite(layer, single.filter(ImageFilter.GaussianBlur(34 * k)))
    return layer


def device(kind, scale, centre, size):
    """A device as an RGBA layer over the whole picture: its colours where its silhouette is, and
    its shadow, drawn by render-icons.py in the box it fills."""
    draw = ICONS.DEVICES[kind]
    ox, oy = centre
    # its unit square's part that has it, with a margin
    x0, x1 = int(ox - 0.45 * scale), int(ox + 0.45 * scale)
    y0, y1 = int(oy - 0.42 * scale), int(oy + 0.42 * scale)
    pixel = 1.0 / scale
    rgba = np.zeros((y1 - y0, x1 - x0, 4), dtype=np.uint8)
    for y in range(y0, y1):
        v = (y + 0.5 - oy) / scale + 0.5
        row = rgba[y - y0]
        for x in range(x0, x1):
            u = (x + 0.5 - ox) / scale + 0.5
            colour, silhouette = draw(u, v, pixel, ICONS.BODY)
            if silhouette > 0.0:
                row[x - x0] = (int(round(colour[0])), int(round(colour[1])), int(round(colour[2])), int(round(silhouette * 255)))
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    layer.paste(Image.fromarray(rgba, "RGBA"), (x0, y0))
    shadow = Image.new("RGBA", size, (0, 0, 0, 0))
    shadow.putalpha(layer.getchannel("A").point(lambda a: a * 0.45))
    shadow = shadow.transform(shadow.size, Image.AFFINE, (1, 0, 0, 0, 1, -max(1, int(0.02 * scale))))  # a little lower
    return Image.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(0.02 * scale)), layer)


# each device and where its body's middle is in its unit square (the GamePad's at v 0.53, the 3DS's at
# 0.4925, the DS's at 0.495): on the row, as render-banner.py places them
KINDS = (("wiiu", 0.53), ("3ds", 0.4925), ("ds", 0.495))


def devices(image, banner, unit, row_y):
    for (kind, body), cx in zip(KINDS, BANNER.CENTRES):
        image = Image.alpha_composite(image, device(kind, unit, (banner.x(cx), row_y + (0.5 - body) * unit), image.size))
    return image


def font(candidates, size):
    for path in candidates:
        if os.path.isfile(path):
            face = ImageFont.truetype(path, size)
            if path == LEXEND:
                face.set_variation_by_axes([600])  # SemiBold, as the launcher's tokens use it
            return face
    sys.exit("no font found: " + ", ".join(candidates))


def wordmark(size, s, centre_x, baseline, mark_size, plate, tagline_y=0, tagline_size=0):
    """The banner's name under the consoles: its dark plate (x0, y0, x1, y1), the name and the tagline,
    with their soft shadow; s is the banner's scale here."""
    back = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(back).rounded_rectangle(plate, radius=44 * s, fill=(4, 8, 16, round(255 * 0.34)))
    back = back.filter(ImageFilter.GaussianBlur(26 * s))
    text = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(text)
    draw.text((centre_x, baseline), "PS5CEMU-HAR", font=font(HEAVY, round(mark_size)), fill=rgb(BANNER.BODY) + (255,), anchor="ms")
    if tagline_size:
        draw.text((centre_x, tagline_y), "Wii U, 3DS and DS on PlayStation 5", font=font(SEMIBOLD, round(tagline_size)),
                  fill=TAGLINE + (255,), anchor="ms")
    shadow = Image.new("RGBA", size, (0, 0, 0, 0))
    shadow.putalpha(text.getchannel("A").point(lambda a: a * 0.45))
    shadow = shadow.transform(size, Image.AFFINE, (1, 0, 0, 0, 1, -max(1, round(2 * s)))).filter(ImageFilter.GaussianBlur(3 * s))
    return Image.alpha_composite(Image.alpha_composite(back, shadow), text)


def picture():
    """pic0: the banner three times its size, its middle a little above the screen's."""
    banner = Banner(3, (0, 384))
    top, bottom = -banner.origin[1] / banner.scale, (H - banner.origin[1]) / banner.scale
    image = background(W, H, banner, banner_squares(top, bottom))
    row = banner.y(BANNER.ROW)
    unit = BANNER.T * banner.scale
    image = Image.alpha_composite(image, glows((W, H), banner, row, unit))
    image = devices(image, banner, unit, row)
    s = banner.scale
    mark = 88 * BANNER.TITLE_SCALE * s
    plate = (banner.x(BANNER.HALF - 420), banner.y(BANNER.BASELINE - 100), banner.x(BANNER.HALF + 420), banner.y(BANNER.BASELINE + 50))
    image = Image.alpha_composite(image, wordmark((W, H), s, HALF, banner.y(BANNER.BASELINE), mark, plate, banner.y(BANNER.TAGLINE), 26 * s))
    return image.convert("RGB")


def tile():
    """icon0: the banner's three bands in a square, the three devices across them and the name under them."""
    size = 512
    # the banner's width over the square: its bands at that scale, the consoles' row in the upper half
    row, unit = 158, 184  # the devices as large as their bands allow: each band is 171 wide
    banner = Banner(size / BANNER.W, (0, 0))
    banner.origin = (0, row - BANNER.ROW * banner.scale)
    top, bottom = -banner.origin[1] / banner.scale, (size - banner.origin[1]) / banner.scale
    image = background(size, size, banner, banner_squares(top, bottom))
    image = Image.alpha_composite(image, glows((size, size), banner, row, unit))
    image = devices(image, banner, unit, row)
    mark = 96
    while font(HEAVY, mark).getlength("PS5CEMU-HAR") > 460:  # the name fits, with room for its shadow
        mark -= 1
    tag = 26
    while font(SEMIBOLD, tag).getlength("Wii U, 3DS and DS on PlayStation 5") > 430:
        tag -= 1
    image = Image.alpha_composite(image, wordmark((size, size), 0.75, size // 2, 392, mark, (36, 304, size - 36, 474), 446, tag))
    return image.convert("RGB")


WEIGHTS = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64])


def encode_bc7(rgb):
    """BC7 mode 6 blocks, row by row, for an opaque H x W x 3 image (W and H multiples of 4)."""
    h, w = rgb.shape[:2]
    out = []
    for y in range(0, h, 64):  # 16 rows of blocks at a time
        strip = rgb[y:y + 64].astype(np.float64)
        rows = strip.shape[0] // 4
        px = strip.reshape(rows, 4, w // 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 3)
        mean = px.mean(axis=1)
        d = px - mean[:, None, :]
        cov = np.einsum("npi,npj->nij", d, d)
        axis = np.ones((len(px), 3)) / np.sqrt(3.0)
        for _ in range(8):  # the principal axis, by power iteration
            v = np.einsum("nij,nj->ni", cov, axis)
            norm = np.linalg.norm(v, axis=1, keepdims=True)
            axis = np.where(norm > 1e-9, v / np.maximum(norm, 1e-12), axis)
        t = np.einsum("npi,ni->np", d, axis)
        ends = [np.clip(mean + t.min(axis=1)[:, None] * axis, 0, 255), np.clip(mean + t.max(axis=1)[:, None] * axis, 0, 255)]
        # 7 bits per channel and a p-bit of 1 for both ends: odd values, and alpha 255
        q = [np.clip(np.rint((e - 1.0) / 2.0), 0, 127).astype(np.int64) for e in ends]
        r0, r1 = q[0] * 2 + 1, q[1] * 2 + 1
        line = (r1 - r0).astype(np.float64)
        length = np.einsum("ni,ni->n", line, line)
        pos = np.einsum("npi,ni->np", px - r0[:, None, :], line) / np.maximum(length, 1e-9)[:, None]
        index = np.abs(pos[:, :, None] * 64.0 - WEIGHTS[None, None, :]).argmin(axis=2)
        index[length == 0] = 0
        # the first pixel's index must have its top bit clear: else swap the ends
        swap = index[:, 0] >= 8
        q[0][swap], q[1][swap] = q[1][swap].copy(), q[0][swap].copy()
        index[swap] = 15 - index[swap]
        q0, q1 = q[0].astype(np.uint64), q[1].astype(np.uint64)
        lo = np.full(len(px), 1 << 6, dtype=np.uint64)  # mode 6
        for c in range(3):  # R0 R1 G0 G1 B0 B1, then A0 A1 (127), from bit 7
            lo |= q0[:, c] << np.uint64(7 + 14 * c)
            lo |= q1[:, c] << np.uint64(14 + 14 * c)
        lo |= np.uint64(127) << np.uint64(49)
        lo |= np.uint64(127) << np.uint64(56)
        lo |= np.uint64(1) << np.uint64(63)  # P0
        hi = np.ones(len(px), dtype=np.uint64)  # P1
        idx = index.astype(np.uint64)
        hi |= idx[:, 0] << np.uint64(1)  # three bits for the first
        for i in range(1, 16):
            hi |= idx[:, i] << np.uint64(4 * i)
        out.append(np.stack([lo, hi], axis=1).astype("<u8").tobytes())
    return b"".join(out)


def decode_bc7(data, w, h):
    """The image mode 6 blocks hold (encode_bc7's check, and the preview)."""
    words = np.frombuffer(data, dtype="<u8").reshape(-1, 2)
    lo, hi = words[:, 0], words[:, 1]
    ends = []
    for e in range(2):
        channel = [((lo >> np.uint64(7 + 7 * (2 * c + e))) & np.uint64(127)).astype(np.int64) for c in range(3)]
        p = ((lo >> np.uint64(63)) & np.uint64(1)) if e == 0 else (hi & np.uint64(1))
        ends.append(np.stack(channel, axis=1) * 2 + p.astype(np.int64)[:, None])
    index = np.stack([((hi >> np.uint64(1)) & np.uint64(7))] + [((hi >> np.uint64(4 * i)) & np.uint64(15)) for i in range(1, 16)], axis=1)
    weight = WEIGHTS[index.astype(np.int64)][:, :, None]
    px = ((64 - weight) * ends[0][:, None, :] + weight * ends[1][:, None, :] + 32) >> 6
    return px.reshape(h // 4, w // 4, 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(h, w, 3).astype(np.uint8)


def write_dds(path, blocks):
    linear_size = len(blocks)
    header = struct.pack("<4sIIIIIII44x", b"DDS ", 124, 0xA1007, H, W, linear_size, 0, 1)
    # the pixel format names a DX10 header; caps: a texture
    header += struct.pack("<II4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
    header += struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    header += struct.pack("<5I", 98, 3, 0, 1, 0)  # BC7_UNORM, a 2D texture, one of it
    assert len(header) == 148
    with open(path, "wb") as out:
        out.write(header + blocks)


def main():
    args = sys.argv[1:]
    if len(args) >= 2 and args[0] == "--title-font":
        if not os.path.isfile(args[1]):
            sys.exit("no font at " + args[1])
        HEAVY.insert(0, args[1])
        args = args[2:]
    if len(args) not in (1, 2):
        sys.exit(__doc__)
    sys.argv = [sys.argv[0]] + args
    out = sys.argv[1]
    rgb = np.asarray(picture())
    blocks = encode_bc7(rgb)
    decoded = decode_bc7(blocks, W, H)
    error = np.sqrt(np.mean((decoded.astype(np.float64) - rgb) ** 2))
    print(f"pic0.dds: BC7, {len(blocks)} bytes, RMS error {error:.2f}")
    write_dds(os.path.join(out, "pic0.dds"), blocks)
    if len(sys.argv) == 3:
        Image.fromarray(decoded, "RGB").save(sys.argv[2])
    tile().save(os.path.join(out, "icon0.png"))
    print("icon0.png: 512x512")


if __name__ == "__main__":
    main()
