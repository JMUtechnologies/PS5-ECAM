#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw the launcher's glyphs, with nothing but the standard library.

    render-glyphs.py OUTPUT_DIR

Writes OUTPUT_DIR/<name>-<size>.tga, white on transparent (32-bit, top row first, as the launcher
reads them): the stylesheet tints them (image-color), so one set serves the blue side and the gold
one. Each is drawn at the sizes the layouts show it at, so none is scaled.

  the DualSense's buttons, for the hints: cross circle square triangle dpad updown leftright
      options touchpad l1 r1 l2 r2 (and play, a filled triangle)
  arrows: chevron-right chevron-down chevron-left
  Settings' categories: video audio controls borders system folder install online diagnostics about
      packs
  each side's mark, beside its name in the top bar: wiiu (the GamePad) and 3ds
  usb: Settings' USB devices (the USB trident)

Shapes are signed distance functions, strokes of an even width, anti-aliased over a pixel.
"""

import math
import os
import struct
import sys

SIZES = {"hint": 30, "small": 24, "rail": 36, "chevron": 20, "mark": 36}


def segment(x, y, ax, ay, bx, by):
    """Distance to the segment from (ax, ay) to (bx, by)."""
    dx, dy = bx - ax, by - ay
    length = dx * dx + dy * dy
    t = 0.0 if length == 0 else max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / length))
    return math.hypot(x - ax - t * dx, y - ay - t * dy)


def rounded_box(x, y, cx, cy, hw, hh, r):
    qx, qy = abs(x - cx) - hw + r, abs(y - cy) - hh + r
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r


def polygon(x, y, points):
    """Signed distance to a closed polygon (negative inside)."""
    d = min(segment(x, y, *points[i], *points[(i + 1) % len(points)]) for i in range(len(points)))
    inside = False
    for i in range(len(points)):
        (ax, ay), (bx, by) = points[i], points[(i + 1) % len(points)]
        if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
            inside = not inside
    return -d if inside else d


def ring(d, width):
    """A stroke of width centred on a shape's outline (d: its signed distance)."""
    return abs(d) - width / 2


def stroke(d, width):
    """A stroke of width along a path (d: its unsigned distance)."""
    return d - width / 2


# Each glyph: a function of (x, y) in its unit square, returning a signed distance (negative inside),
# and its stroke width in the unit square
W = 0.085  # most strokes


def cross(x, y):
    return stroke(min(segment(x, y, 0.24, 0.24, 0.76, 0.76), segment(x, y, 0.24, 0.76, 0.76, 0.24)), W)


def circle_glyph(x, y):
    return ring(math.hypot(x - 0.5, y - 0.5) - 0.28, W)


def square(x, y):
    return ring(rounded_box(x, y, 0.5, 0.5, 0.26, 0.26, 0.04), W)


def triangle(x, y):
    return ring(polygon(x, y, [(0.5, 0.2), (0.8, 0.74), (0.2, 0.74)]), W)


def play(x, y):
    return polygon(x, y, [(0.3, 0.2), (0.8, 0.5), (0.3, 0.8)])


def dpad(x, y):
    plus = min(rounded_box(x, y, 0.5, 0.5, 0.32, 0.11, 0.03), rounded_box(x, y, 0.5, 0.5, 0.11, 0.32, 0.03))
    return ring(plus, W * 0.8)


def arrows(x, y, vertical):
    if vertical:
        x, y = y, x
    line = segment(x, y, 0.18, 0.5, 0.82, 0.5)
    heads = min(segment(x, y, 0.18, 0.5, 0.36, 0.32), segment(x, y, 0.18, 0.5, 0.36, 0.68),
                segment(x, y, 0.82, 0.5, 0.64, 0.32), segment(x, y, 0.82, 0.5, 0.64, 0.68))
    return stroke(min(line, heads), W)


def options(x, y):
    return stroke(min(segment(x, y, 0.28, y0, 0.72, y0) for y0 in (0.32, 0.5, 0.68)), W)


def touchpad(x, y):
    return ring(rounded_box(x, y, 0.5, 0.5, 0.36, 0.22, 0.08), W)


def shoulder(x, y, label):
    """L1, R1, L2, R2: a rounded tab with its name drawn in strokes."""
    outline = ring(rounded_box(x, y, 0.5, 0.5, 0.42, 0.27, 0.12), W * 0.7)
    w = W * 0.75
    letter = segment(x, y, 0.3, 0.36, 0.3, 0.64) if label[0] == "L" else min(
        segment(x, y, 0.27, 0.36, 0.27, 0.64), segment(x, y, 0.27, 0.36, 0.38, 0.36), segment(x, y, 0.38, 0.36, 0.42, 0.42),
        segment(x, y, 0.42, 0.42, 0.38, 0.49), segment(x, y, 0.38, 0.49, 0.27, 0.49), segment(x, y, 0.33, 0.49, 0.43, 0.64))
    if label[0] == "L":
        letter = min(letter, segment(x, y, 0.3, 0.64, 0.42, 0.64))
    if label[1] == "1":
        digit = min(segment(x, y, 0.62, 0.36, 0.62, 0.64), segment(x, y, 0.62, 0.36, 0.56, 0.42))
    else:
        digit = min(segment(x, y, 0.55, 0.40, 0.6, 0.36), segment(x, y, 0.6, 0.36, 0.68, 0.38), segment(x, y, 0.68, 0.38, 0.69, 0.45),
                    segment(x, y, 0.69, 0.45, 0.55, 0.64), segment(x, y, 0.55, 0.64, 0.71, 0.64))
    return min(outline, stroke(min(letter, digit), w))


def chevron(x, y, direction):
    if direction == "down":
        x, y = y, x
    if direction == "left":
        x = 1 - x
    return stroke(min(segment(x, y, 0.36, 0.2, 0.66, 0.5), segment(x, y, 0.66, 0.5, 0.36, 0.8)), W * 1.4)


# Settings' categories, line drawings in the same strokes
def video(x, y):
    screen = ring(rounded_box(x, y, 0.5, 0.44, 0.36, 0.25, 0.06), W)
    return min(screen, stroke(segment(x, y, 0.34, 0.82, 0.66, 0.82), W))


def arc(x, y, cx, cy, r, half):
    """Distance to an arc around (cx, cy) of radius r, opening to the right, half an angle each way."""
    angle = math.atan2(y - cy, x - cx)
    if abs(angle) <= half:
        return abs(math.hypot(x - cx, y - cy) - r)
    ends = [(cx + r * math.cos(a), cy + r * math.sin(a)) for a in (-half, half)]
    return min(math.hypot(x - ex, y - ey) for ex, ey in ends)


def audio(x, y):
    speaker = ring(polygon(x, y, [(0.12, 0.4), (0.27, 0.4), (0.45, 0.22), (0.45, 0.78), (0.27, 0.6), (0.12, 0.6)]), W * 0.9)
    waves = stroke(min(arc(x, y, 0.45, 0.5, 0.16, 0.85), arc(x, y, 0.45, 0.5, 0.32, 0.85)), W * 0.9)
    return min(speaker, waves)


def controls(x, y):
    body = ring(rounded_box(x, y, 0.5, 0.52, 0.38, 0.2, 0.18), W)
    plus = stroke(min(segment(x, y, 0.27, 0.52, 0.41, 0.52), segment(x, y, 0.34, 0.45, 0.34, 0.59)), W * 0.85)
    buttons = min(math.hypot(x - 0.64, y - 0.48), math.hypot(x - 0.72, y - 0.56)) - 0.04
    return min(body, plus, buttons)


def borders(x, y):
    outer = ring(rounded_box(x, y, 0.5, 0.5, 0.36, 0.3, 0.05), W)
    inner = rounded_box(x, y, 0.5, 0.5, 0.18, 0.13, 0.02)
    return min(outer, inner)


def system(x, y):
    shell = min(ring(rounded_box(x, y, 0.5, 0.31, 0.27, 0.15, 0.04), W), ring(rounded_box(x, y, 0.5, 0.69, 0.27, 0.15, 0.04), W))
    return shell


def folder(x, y):
    return ring(polygon(x, y, [(0.16, 0.26), (0.42, 0.26), (0.5, 0.35), (0.84, 0.35), (0.84, 0.76), (0.16, 0.76)]), W)


def install(x, y):
    arrow = min(segment(x, y, 0.5, 0.18, 0.5, 0.6), segment(x, y, 0.5, 0.6, 0.33, 0.43), segment(x, y, 0.5, 0.6, 0.67, 0.43))
    tray = min(segment(x, y, 0.2, 0.62, 0.2, 0.8), segment(x, y, 0.2, 0.8, 0.8, 0.8), segment(x, y, 0.8, 0.8, 0.8, 0.62))
    return stroke(min(arrow, tray), W)


def online(x, y):
    globe = ring(math.hypot(x - 0.5, y - 0.5) - 0.32, W)
    meridian = ring(math.hypot((x - 0.5) / 0.45, y - 0.5) - 0.32, W * 0.8) * 0.45
    equator = stroke(segment(x, y, 0.18, 0.5, 0.82, 0.5), W * 0.8)
    return min(globe, meridian, equator)


def diagnostics(x, y):
    return stroke(min(segment(x, y, 0.16, 0.6, 0.34, 0.6), segment(x, y, 0.34, 0.6, 0.44, 0.3), segment(x, y, 0.44, 0.3, 0.56, 0.72),
                      segment(x, y, 0.56, 0.72, 0.66, 0.48), segment(x, y, 0.66, 0.48, 0.84, 0.48)), W)


def about(x, y):
    outline = ring(math.hypot(x - 0.5, y - 0.5) - 0.33, W)
    dot = math.hypot(x - 0.5, y - 0.33) - 0.055
    bar = stroke(segment(x, y, 0.5, 0.47, 0.5, 0.68), W * 1.1)
    return min(outline, dot, bar)


def packs(x, y):
    # stacked layers: the graphic packs
    top = ring(polygon(x, y, [(0.5, 0.2), (0.82, 0.36), (0.5, 0.52), (0.18, 0.36)]), W)
    lower = stroke(min(segment(x, y, 0.18, 0.52, 0.5, 0.68), segment(x, y, 0.5, 0.68, 0.82, 0.52)), W)
    lowest = stroke(min(segment(x, y, 0.18, 0.66, 0.5, 0.82), segment(x, y, 0.5, 0.82, 0.82, 0.66)), W)
    return min(top, lower, lowest)


def usb(x, y):
    """The USB trident: a stem with an arrow on top, a branch to a ring and one to a square."""
    stem = stroke(segment(x, y, 0.5, 0.86, 0.5, 0.2), W)
    head = polygon(x, y, [(0.5, 0.06), (0.39, 0.22), (0.61, 0.22)])
    left = stroke(min(segment(x, y, 0.5, 0.66, 0.28, 0.52), segment(x, y, 0.28, 0.52, 0.28, 0.42)), W)
    right = stroke(min(segment(x, y, 0.5, 0.56, 0.72, 0.44), segment(x, y, 0.72, 0.44, 0.72, 0.36)), W)
    ring = abs(math.hypot(x - 0.28, y - 0.36) - 0.06) - W / 2
    square = rounded_box(x, y, 0.72, 0.31, 0.06, 0.06, 0.01)
    foot = math.hypot(x - 0.5, y - 0.86) - 0.075
    return min(stem, head, left, right, ring, square, foot)


def wiiu(x, y):
    """The Wii U GamePad: its body, its screen, the sticks."""
    body = ring(rounded_box(x, y, 0.5, 0.5, 0.45, 0.26, 0.13), W)
    screen = rounded_box(x, y, 0.5, 0.5, 0.17, 0.13, 0.03)
    sticks = min(math.hypot(x - 0.2, y - 0.42), math.hypot(x - 0.8, y - 0.42)) - 0.045
    return min(body, screen, sticks)


def n3ds(x, y):
    """The 3DS, open: its two halves and their screens."""
    halves = min(ring(rounded_box(x, y, 0.5, 0.28, 0.31, 0.2, 0.06), W), ring(rounded_box(x, y, 0.5, 0.73, 0.31, 0.2, 0.06), W))
    screens = min(rounded_box(x, y, 0.5, 0.28, 0.17, 0.1, 0.02), rounded_box(x, y, 0.5, 0.73, 0.12, 0.09, 0.02))
    return min(halves, screens)


GLYPHS = {
    "cross": (cross, ("hint", "small")),
    "circle": (circle_glyph, ("hint", "small")),
    "square": (square, ("hint", "small")),
    "triangle": (triangle, ("hint", "small")),
    "play": (play, ("hint", "small")),
    "dpad": (dpad, ("hint", "small")),
    "updown": (lambda x, y: arrows(x, y, True), ("hint", "small")),
    "leftright": (lambda x, y: arrows(x, y, False), ("hint", "small")),
    "options": (options, ("hint", "small")),
    "touchpad": (touchpad, ("hint", "small")),
    "l1": (lambda x, y: shoulder(x, y, "L1"), ("hint", "small")),
    "r1": (lambda x, y: shoulder(x, y, "R1"), ("hint", "small")),
    "l2": (lambda x, y: shoulder(x, y, "L2"), ("hint", "small")),
    "r2": (lambda x, y: shoulder(x, y, "R2"), ("hint", "small")),
    "chevron-right": (lambda x, y: chevron(x, y, "right"), ("chevron",)),
    "chevron-left": (lambda x, y: chevron(x, y, "left"), ("chevron",)),
    "chevron-down": (lambda x, y: chevron(x, y, "down"), ("chevron",)),
    "video": (video, ("rail",)),
    "audio": (audio, ("rail",)),
    "controls": (controls, ("rail",)),
    "borders": (borders, ("rail",)),
    "system": (system, ("rail",)),
    "folder": (folder, ("rail", "small")),
    "install": (install, ("rail",)),
    "online": (online, ("rail",)),
    "diagnostics": (diagnostics, ("rail",)),
    "about": (about, ("rail",)),
    "packs": (packs, ("rail",)),
    "usb": (usb, ("rail",)),
    "wiiu": (wiiu, ("mark",)),
    "3ds": (n3ds, ("mark",)),
}


def render(shape, size):
    """Rows of alpha, the shape's coverage of each pixel (supersampled 4x4 at the edges)."""
    pixel = 1.0 / size
    rows = []
    for py in range(size):
        row = []
        for px in range(size):
            d = shape((px + 0.5) * pixel, (py + 0.5) * pixel)
            if d > pixel:
                row.append(0.0)
            elif d < -pixel:
                row.append(1.0)
            else:
                inside = sum(shape((px + (sx + 0.5) / 4) * pixel, (py + (sy + 0.5) / 4) * pixel) <= 0 for sx in range(4) for sy in range(4))
                row.append(inside / 16)
        rows.append(row)
    return rows


def write_tga(path, alphas):
    height, width = len(alphas), len(alphas[0])
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, 32, 0x28)
    body = b"".join(bytes((255, 255, 255, int(round(a * 255)))) for row in alphas for a in row)
    with open(path, "wb") as out:
        out.write(header + body)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, (shape, kinds) in GLYPHS.items():
        for kind in kinds:
            size = SIZES[kind]
            write_tga(os.path.join(out, f"{name}-{size}.tga"), render(shape, size))


if __name__ == "__main__":
    main()
