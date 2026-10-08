#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write sample 3DS files for the launcher's preview, with nothing but the standard library.

    make-3ds-samples.py FOLDER [DATA]

Each is only what Azahar's side's library reads (port/azahar/library.cpp): an SMDH (names,
publisher, a 48x48 icon) where each kind of file keeps it, in an NCSD's NCCH ExeFS (.3ds, .cci),
an NCCH's (.cxi), a CIA's meta section, a 3DSX's extended header, and one encrypted dump, which
the library lists by its file name. They hold no game. With DATA (the preview's data folder), an
update and DLC are also "installed" there, as Settings > Installs would leave their TMDs.
"""

import os
import struct
import sys


def smdh(title, publisher, colour):
    data = bytearray(0x36C0)
    data[0:4] = b"SMDH"
    for language in range(16):
        entry = 0x8 + language * 0x200
        for offset, text, size in ((0, title, 0x80), (0x80, title, 0x100), (0x180, publisher, 0x80)):
            encoded = text.encode("utf-16-le")[:size - 2]
            data[entry + offset:entry + offset + len(encoded)] = encoded
    # the large icon: the colour, lighter towards the top, with a white disc; RGB565 in 8x8 tiles
    for y in range(48):
        for x in range(48):
            shade = 1.0 - y / 96.0
            r, g, b = (int(c * shade) for c in colour)
            if (x - 24) ** 2 + (y - 24) ** 2 < 11 ** 2:
                r, g, b = 250, 250, 250
            value = (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)
            tile = (y // 8) * 6 + x // 8
            tx, ty = x % 8, y % 8
            morton = (tx & 1) | (ty & 1) << 1 | (tx & 2) << 1 | (ty & 2) << 2 | (tx & 4) << 2 | (ty & 4) << 3
            struct.pack_into("<H", data, 0x24C0 + (tile * 64 + morton) * 2, value)
    return bytes(data)


def ncch(title_id, icon, encrypted=False, product=""):
    """An NCCH with an ExeFS holding the icon, 0x400 + the ExeFS; its product code (CTR-P-AREE)
    gives the box art's ID."""
    header = bytearray(0x400)
    header[0x100:0x104] = b"NCCH"
    header[0x150:0x160] = product.encode().ljust(16, b"\0")
    struct.pack_into("<Q", header, 0x108, title_id)
    struct.pack_into("<Q", header, 0x118, title_id)
    header[0x18F] = 0 if encrypted else 0x4  # NoCrypto
    struct.pack_into("<II", header, 0x1A0, 2, (0x200 + len(icon) + 0x1FF) // 0x200)
    exefs = bytearray(0x200)
    exefs[0:4] = b"icon"
    struct.pack_into("<II", exefs, 8, 0, len(icon))
    return bytes(header) + bytes(exefs) + icon


def ncsd(title_id, icon, encrypted=False, product=""):
    header = bytearray(0x4000)
    header[0x100:0x104] = b"NCSD"
    partition = ncch(title_id, icon, encrypted, product)
    struct.pack_into("<II", header, 0x120, 0x4000 // 0x200, (len(partition) + 0x1FF) // 0x200)
    return bytes(header) + partition


def cia(title_id, version, icon, encrypted=False):
    align = lambda value: (value + 63) & ~63
    certs, ticket, tmd, content, meta = 0xA00, 0x350, 0xB34, 0x100, 0x3AC0
    header = bytearray(0x2020)
    struct.pack_into("<IHHIIIIQ", header, 0, 0x2020, 0, 0, certs, ticket, tmd, meta, content)
    data = bytearray(align(0x2020) + align(certs) + align(ticket) + align(tmd) + align(content) + meta)
    data[0:len(header)] = header
    tmd_at = align(0x2020) + align(certs) + align(ticket)
    data[tmd_at:tmd_at + 4] = b"\x00\x01\x00\x04"  # RSA-2048, SHA-256
    struct.pack_into(">Q", data, tmd_at + 0x140 + 0x4C, title_id)
    struct.pack_into(">H", data, tmd_at + 0x140 + 0x9C, version)
    if encrypted:  # one content, encrypted with the title key: its chunk record's type has bit 0
        struct.pack_into(">H", data, tmd_at + 0x140 + 0x9E, 1)
        struct.pack_into(">H", data, tmd_at + 0x140 + 0xC4 + 0x900 + 6, 0x0001)
    meta_at = tmd_at + align(tmd) + align(content)
    data[meta_at + 0x400:meta_at + 0x400 + len(icon)] = icon
    return bytes(data)


def threedsx(icon):
    header = struct.pack("<4sHHIIIIIIIII", b"3DSX", 0x2C, 8, 0, 0, 0, 0, 0, 0, 0x2C, len(icon), 0)
    return header + icon


SAMPLES = [
    ("Super Mario 3D Land.3ds", lambda i: ncsd(0x0004000000053F00, i, product="CTR-P-AREE"), "Super Mario 3D Land", "Nintendo", (220, 40, 40)),
    ("Mario Kart 7.cci", lambda i: ncsd(0x0004000000030600, i, product="CTR-P-AMKE"), "Mario Kart 7", "Nintendo", (240, 160, 20)),
    ("Pokémon Ultra Sun.3ds", lambda i: ncsd(0x00040000001B5000, i), "Pokémon Ultra Sun", "Nintendo", (240, 120, 30)),
    ("Fire Emblem Awakening.cxi", lambda i: ncch(0x00040000000A0500, i), "Fire Emblem Awakening", "Nintendo", (40, 80, 180)),
    ("Kirby Planet Robobot.cia", lambda i: cia(0x000400000017C100, 1040, i), "Kirby: Planet Robobot", "Nintendo", (240, 120, 180)),
    ("Kirby Planet Robobot Update.cia", lambda i: cia(0x0004000E0017C100, 1040, i), "Kirby: Planet Robobot", "Nintendo", (240, 120, 180)),
    ("Pilotwings Resort.cia", lambda i: cia(0x0004000000032000, 0, i, encrypted=True), "Pilotwings Resort", "Nintendo", (60, 150, 220)),
    ("Luigi's Mansion Dark Moon.3ds", lambda i: ncsd(0x0004000000055F00, i, encrypted=True), "", "", (40, 160, 60)),
    ("Checkpoint.3dsx", lambda i: threedsx(i), "Checkpoint", "Bernardo Giordano, FlagBrew", (60, 60, 60)),
    ("FBI.3dsx", lambda i: threedsx(i), "FBI", "Steveice10", (30, 120, 200)),
    ("More/Kid Icarus Uprising.3ds", lambda i: ncsd(0x0004000000030200, i), "Kid Icarus: Uprising", "Nintendo", (230, 210, 90)),
]


def installed_tmd(data, high, low, version):
    """A title's TMD in the 3DS's storage under DATA (the app's data folder), as an install leaves it."""
    folder = os.path.join(data, "azahar", "sdmc", "Nintendo 3DS", "0" * 32, "0" * 32, "title", f"{high:08x}", f"{low:08x}", "content")
    os.makedirs(folder, exist_ok=True)
    tmd = bytearray(0xB34)
    tmd[0:4] = b"\x00\x01\x00\x04"  # RSA-2048, SHA-256
    struct.pack_into(">Q", tmd, 0x140 + 0x4C, high << 32 | low)
    struct.pack_into(">H", tmd, 0x140 + 0x9C, version)
    with open(os.path.join(folder, "00000000.tmd"), "wb") as file:
        file.write(tmd)


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    for name, make, title, publisher, colour in SAMPLES:
        path = os.path.join(sys.argv[1], name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as file:
            file.write(make(smdh(title, publisher, colour)))
    if len(sys.argv) == 3:
        # an update installed for Super Mario 3D Land, and DLC for Fire Emblem Awakening
        installed_tmd(sys.argv[2], 0x0004000E, 0x00053F00, 1040)
        installed_tmd(sys.argv[2], 0x0004008C, 0x000A0500, 0)


if __name__ == "__main__":
    main()
