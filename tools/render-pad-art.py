#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# The in-game menus' DualSense (the Buttons page, port/app/side_menu.h's DrawPad): port/ui/dualsense.png,
# Zacksly's drawing as Porpoise X renders it (CC BY 3.0, port/ui/CC-BY-3.0-Zacksly.txt), written as the
# 32-bit top-down TGA the menus read (port/app/tga.h). Plain Python: an 8-bit RGBA PNG, not interlaced.
#
#   tools/render-pad-art.py port/ui/dualsense.png OUT.tga
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")
    at, idat, width, height = 8, b"", 0, 0
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or colour != 6 or interlace:
                raise SystemExit(f"{path}: needs 8-bit RGBA, not interlaced")
        elif kind == b"IDAT":
            idat += body
        at += 12 + length
    raw, stride = zlib.decompress(idat), width * 4
    rows, previous = [], bytearray(stride)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = line[i - 4] if i >= 4 else 0
            up = previous[i]
            corner = previous[i - 4] if i >= 4 else 0
            if kind == 1:
                line[i] = (line[i] + left) & 255
            elif kind == 2:
                line[i] = (line[i] + up) & 255
            elif kind == 3:
                line[i] = (line[i] + ((left + up) >> 1)) & 255
            elif kind == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 255
        rows.append(bytes(line))
        previous = line
    return width, height, rows


def main():
    width, height, rows = read_png(sys.argv[1])
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, 32, 0x28)
    out = bytearray(header)
    for row in rows:
        for x in range(0, len(row), 4):
            r, g, b, a = row[x:x + 4]
            out += bytes((b, g, r, a))
    open(sys.argv[2], "wb").write(out)


if __name__ == "__main__":
    main()
