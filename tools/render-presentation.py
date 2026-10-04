#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw PS5CEMU-HAR's home-screen art: its background, sce_sys/pic0.dds, and its tile,
sce_sys/icon0.png.

    render-presentation.py SCE_SYS_DIR [PREVIEW_PNG]

The PS5 shows pic0.dds behind the app while it is selected on the home screen, and pic1.dds while
it starts; tools/package.sh installs this one image as both. It is the README's banner
(tools/render-banner.py) made the size of the screen: the app's two sides split down the middle as
its start screen is, Cemu's dark Wii U Homebrew Launcher blue with its bubbles on the left
(tools/render-background.py) and Azahar's dark gold with the 3DS Homebrew Launcher's waves on the
right; the Wii U GamePad and the 3DS either side (tools/render-icons.py's), and between them the
app's name in the banner's heavy type, lit blue from the left and gold from the right, over the
tagline. It sits above the middle: the shell draws its own title, a Play button and its shading
over the lower left. icon0.png, the tile, is the same in a square: the two devices above the name.

The console takes a single 3840x2160 BC7_UNORM DX10 DDS without mipmaps
(ps5-native-app-boilerplate's tools/validate-assets.sh), which this encodes itself in BC7's mode 6:
per 4x4 block, a line between two colours, along the block's principal axis, and a 4-bit position
on it for each pixel. The tile is a 512x512 opaque PNG; the console rounds its corners.

This needs Pillow and numpy, and the banner's fonts (Segoe UI Black and Semibold, from Windows;
DejaVu Sans stands in elsewhere); its output is committed (sce_sys/pic0.dds, sce_sys/icon0.png), so
the build needs none of them. The devices take a minute or so: render-icons.py draws them a pixel at
a time.
"""

import importlib.util
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 3840, 2160
HALF = W // 2
# the banner's darkness: as render-banner.py's OVERLAY and WAVE_OVERLAY
DARK, DARK_WAVES = 0.7, 0.8
FONT_DIRS = ["C:/Windows/Fonts", "/mnt/c/Windows/Fonts"]
HEAVY = [os.path.join(d, "seguibl.ttf") for d in FONT_DIRS] + ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]
SEMIBOLD = [os.path.join(d, "seguisb.ttf") for d in FONT_DIRS] + ["/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]
BLUE, GOLD = (0x5a, 0xa9, 0xff), (0xf4, 0xb6, 0x3f)
TAGLINE = (0xe6, 0xee, 0xf6)
# render-banner.py's waves (top, wavelength, amplitude, colour, alpha), in its 1280x320 pixels
BANNER_WAVES = [(196, 430, 12, (0xf4, 0xb6, 0x3f), 0.16), (228, 320, 9, (0xf8, 0xc6, 0x5a), 0.18),
                (262, 240, 7, (0xff, 0xd9, 0x8a), 0.20)]


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(os.path.dirname(os.path.abspath(__file__)), file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ICONS = load("render_icons", "render-icons.py")
HBL = load("render_background", "render-background.py")


class Banner:
    """render-banner.py's picture at scale, its (0, 0) at origin: where its pieces go."""

    def __init__(self, scale, origin):
        self.scale, self.origin = scale, origin

    def x(self, value):
        return self.origin[0] + value * self.scale

    def y(self, value):
        return self.origin[1] + value * self.scale


def bubbles(width, height, region):
    """The Homebrew Launcher's background: region (x, y, w, h) of its 1280x720 screen at this size,
    darkened as the banner is."""
    rx, ry, rw, rh = region
    sx, sy = width / rw, height / rh
    t = (ry + (np.arange(height, dtype=np.float64)[:, None] + 0.5) / sy) / 720.0
    image = np.empty((height, width, 3))
    for c in range(3):
        image[:, :, c] = HBL.TOP[c] + (HBL.BOTTOM[c] - HBL.TOP[c]) * t
    for x, y, radius, alpha in HBL.particles():
        cx, cy, r = (x * 1280 - rx) * sx, (y * 720 - ry) * sy, radius * 1280 * sx
        if r < 0.5 or cx + r < 0 or cx - r > width or cy + r < 0 or cy - r > height:
            continue
        x0, x1 = max(0, int(cx - r - 1)), min(width, int(cx + r + 2))
        y0, y1 = max(0, int(cy - r - 1)), min(height, int(cy + r + 2))
        px = np.arange(x0, x1) + 0.5 - cx
        py = np.arange(y0, y1)[:, None] + 0.5 - cy
        a = alpha * np.clip(r - np.hypot(px, py) + 0.5, 0.0, 1.0)
        region_pixels = image[y0:y1, x0:x1]
        region_pixels += (255.0 - region_pixels) * a[:, :, None]
    return image * (1.0 - DARK)


def sand(width, height, banner, left):
    """Azahar's side as the banner has it: the 3DS Homebrew Launcher's gold, darkened, and the
    banner's waves over it, from its middle (left: this picture's x there)."""
    t = (np.arange(height, dtype=np.float64)[:, None] + 0.5) / height
    image = np.empty((height, width, 3))
    for c in range(3):
        image[:, :, c] = (ICONS.WAVE_TOP[c] + (ICONS.WAVE_BOTTOM[c] - ICONS.WAVE_TOP[c]) * t) * (1.0 - DARK_WAVES)
    fx = (np.arange(width, dtype=np.float64)[None, :] + 0.5 + left - banner.x(640)) / banner.scale  # banner pixels from its middle
    fy = np.arange(height, dtype=np.float64)[:, None] + 0.5
    ground = image.copy()
    for top, length, amplitude, colour, alpha in BANNER_WAVES:
        surface = banner.y(top + amplitude * (1.0 - np.cos(2.0 * np.pi * fx / length)))
        a = np.clip(fy - surface + 0.5, 0.0, 1.0) * alpha
        image += (np.array(colour, dtype=np.float64) - image) * a[:, :, None]
    # below the banner's bottom the waves fade back into the dark ground, as the start screen's do
    fade = np.clip((fy - banner.y(320)) / (180.0 * banner.scale), 0.0, 1.0) * 0.75
    return image + (ground - image) * fade[:, :, None]


def background(width, height, banner, region):
    half = width // 2
    left = bubbles(half, height, region)
    right = sand(width - half, height, banner, half)
    image = np.concatenate([left, right], axis=1)
    # the soft line between the two halves, brightest at the banner's middle
    seam = max(2, round(banner.scale * 2))
    fade = np.clip(1.0 - np.abs(np.arange(height) + 0.5 - banner.y(160)) / (height * 0.5), 0.0, 1.0)[:, None, None] * 0.45
    image[:, half - seam // 2:half + seam // 2] += (255.0 - image[:, half - seam // 2:half + seam // 2]) * fade
    return Image.fromarray(np.clip(np.rint(image), 0, 255).astype(np.uint8), "RGB").convert("RGBA")


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


def font(candidates, size):
    for path in candidates:
        if os.path.isfile(path):
            return ImageFont.truetype(path, size)
    sys.exit("no font found: " + ", ".join(candidates))


def wordmark(size, banner, centre_x, baseline, mark_size, tagline_size=0):
    """The banner's middle: the blue and gold glows, the dark plate, the app's name and the tagline
    under it, with their soft shadow."""
    s = banner.scale
    glow = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(glow)
    middle = baseline - 0.18 * mark_size
    for side, colour, alpha in ((-1, BLUE, 0.42), (1, GOLD, 0.38)):
        cx = centre_x + side * 170 * s
        draw.ellipse((cx - 190 * s, middle - 62 * s, cx + 190 * s, middle + 62 * s), fill=colour + (int(255 * alpha),))
    glow = glow.filter(ImageFilter.GaussianBlur(34 * s))
    plate = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(plate).rounded_rectangle((centre_x - 330 * s, middle - 56 * s, centre_x + 330 * s, middle + 72 * s), radius=40 * s,
                                            fill=(4, 8, 16, 102))
    plate = plate.filter(ImageFilter.GaussianBlur(14 * s))
    text = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(text)
    draw.text((centre_x, baseline), "PS5CEMU-HAR", font=font(HEAVY, round(mark_size)), fill=ICONS.BODY + (255,), anchor="ms")
    if tagline_size:
        draw.text((centre_x, baseline + 0.48 * mark_size), "Wii U and Nintendo 3DS on PlayStation 5", font=font(SEMIBOLD, round(tagline_size)),
                  fill=TAGLINE + (255,), anchor="ms")
    shadow = Image.new("RGBA", size, (0, 0, 0, 0))
    shadow.putalpha(text.getchannel("A").point(lambda a: a * 0.45))
    shadow = shadow.transform(size, Image.AFFINE, (1, 0, 0, 0, 1, -max(1, round(2 * s)))).filter(ImageFilter.GaussianBlur(3 * s))
    return Image.alpha_composite(Image.alpha_composite(Image.alpha_composite(glow, plate), shadow), text)


def picture():
    """pic0: the banner three times its size, its middle a little above the screen's."""
    banner = Banner(3, (0, 384))
    image = background(W, H, banner, (373, 0, 640, 720))
    # render-banner.py's devices: each one's unit square, 272 banner pixels, at its corner there
    for kind, corner in (("wiiu", (34, 18)), ("3ds", (974, 30))):
        image = Image.alpha_composite(image, device(kind, 272 * banner.scale, (banner.x(corner[0] + 136), banner.y(corner[1] + 136)), (W, H)))
    image = Image.alpha_composite(image, wordmark((W, H), banner, HALF, banner.y(176), 88 * banner.scale, 26 * banner.scale))
    return image.convert("RGB")


def tile():
    """icon0: the two devices side by side above the app's name, on the two sides' grounds."""
    size = 512
    scale = size / 1280 * 1.15
    banner = Banner(scale, (size / 2 - 640 * scale, 232))
    image = background(size, size, banner, (480, 0, 360, 720))
    for kind, scale, centre in (("wiiu", 236, (128, 196)), ("3ds", 216, (384, 200))):
        image = Image.alpha_composite(image, device(kind, scale, centre, (size, size)))
    mark = 72
    while font(HEAVY, mark).getlength("PS5CEMU-HAR") > 452:  # the name fits, with room for its shadow
        mark -= 1
    image = Image.alpha_composite(image, wordmark((size, size), Banner(0.72, (0, 0)), size // 2, 430, mark))
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
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
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
