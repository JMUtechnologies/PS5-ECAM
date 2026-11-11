#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tiny games for tools/tests/test-cores.sh: each a valid image of its console that loops on one instruction, so
a core loads it, runs it and draws frames. Nothing of any real game is in them.

    make-test-games.py OUT_DIR
"""
import os
import struct
import sys


def nes():
    # iNES, mapper 0: 32 KiB of program (a jump to itself), 8 KiB of tiles
    prg = bytearray([0xEA] * 0x8000)
    prg[0:3] = bytes([0x4C, 0x00, 0x80])      # 8000: JMP $8000
    prg[3] = 0x40                             # 8003: RTI
    prg[0x7FFA:0x8000] = struct.pack("<HHH", 0x8003, 0x8000, 0x8003)  # NMI, reset, IRQ
    return b"NES\x1a" + bytes([2, 1, 0, 0]) + bytes(8) + prg + bytes(0x2000)


def snes():
    # LoROM, 128 KiB: the reset vector at a branch to itself, a valid header and checksum
    rom = bytearray(0x20000)
    rom[0:2] = bytes([0x80, 0xFE])            # 8000: BRA -2
    rom[2] = 0x40                             # 8002: RTI
    header = 0x7FC0
    rom[header:header + 21] = b"PLAYTENDOH TEST      "
    rom[header + 0x15] = 0x20                 # LoROM
    rom[header + 0x16] = 0x00                 # ROM only
    rom[header + 0x17] = 0x07                 # 128 KiB
    rom[header + 0x19] = 0x01                 # North America
    rom[header + 0x1A] = 0x33
    rom[0x7FEA:0x7FEC] = struct.pack("<H", 0x8002)   # native NMI
    rom[0x7FFA:0x7FFE] = struct.pack("<HH", 0x8002, 0x8000)  # emulation NMI, reset
    rom[0x7FDC:0x7FE0] = struct.pack("<HH", 0xFFFF, 0x0000)
    checksum = sum(rom) & 0xFFFF
    rom[0x7FDC:0x7FE0] = struct.pack("<HH", checksum ^ 0xFFFF, checksum)
    return bytes(rom)


def known_header():
    """The boot logo a Game Boy cartridge carries, as mGBA's own source checks it (read from its checkout, not
    kept here)"""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.deps/libretro/mgba/src/gb/gb.c")
    text = open(path).read()
    body = text[text.index("_knownHeader[") :].split("{", 1)[1].split("}", 1)[0]
    return bytes(int(v, 16) for v in body.replace(",", " ").split())


def snes_input():
    # the joypad, as the SNES reads it each frame (auto-read), copied to work RAM $7E0000-1 again and again
    rom = bytearray(snes())
    code = bytes([0xA9, 0x01, 0x8D, 0x00, 0x42,        # LDA #1; STA $4200 (auto-read on)
                  0xAD, 0x18, 0x42, 0x8D, 0x00, 0x00,  # LDA $4218; STA $0000
                  0xAD, 0x19, 0x42, 0x8D, 0x01, 0x00,  # LDA $4219; STA $0001
                  0x80, 0xF2])                         # BRA back to the reads
    rom[0x100:0x100 + len(code)] = code
    rom[0x7FFC:0x7FFE] = struct.pack("<H", 0x8100)     # reset: the code
    rom[0x7FDC:0x7FE0] = struct.pack("<HH", 0xFFFF, 0x0000)
    checksum = sum(rom) & 0xFFFF
    rom[0x7FDC:0x7FE0] = struct.pack("<HH", checksum ^ 0xFFFF, checksum)
    return bytes(rom)


def gba_input():
    # KEYINPUT (0x04000130) copied to IWRAM's first halfword again and again
    rom = bytearray(gba())
    words = [0xE59F000C, 0xE59F100C, 0xE1D020B0, 0xE1C120B0, 0xEAFFFFFC, 0x04000130, 0x03000000]
    rom[0xC0:0xC0 + 4 * len(words)] = b"".join(struct.pack("<I", w) for w in words)
    return bytes(rom)


def gb(colour):
    rom = bytearray(0x8000)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])  # NOP; JP $0150
    logo = known_header()
    rom[0x104:0x104 + len(logo)] = logo
    rom[0x134:0x143] = b"PLAYTENDOHTEST\x00"
    rom[0x143] = 0x80 if colour else 0x00
    rom[0x147] = 0x00                        # ROM only
    rom[0x148] = 0x00                        # 32 KiB
    check = 0
    for b in rom[0x134:0x14D]:
        check = (check - b - 1) & 0xFF
    rom[0x14D] = check
    rom[0x150:0x152] = bytes([0x18, 0xFE])  # JR -2
    return bytes(rom)


def gba():
    rom = bytearray(0x1000)
    rom[0:4] = struct.pack("<I", 0xEA00002E)  # B $C0
    rom[0xA0:0xAC] = b"PLAYTENDOH  "
    rom[0xAC:0xB0] = b"TEST"
    rom[0xB0:0xB2] = b"01"
    rom[0xB2] = 0x96
    check = 0
    for b in rom[0xA0:0xBD]:
        check = (check - b) & 0xFF
    rom[0xBD] = (check - 0x19) & 0xFF
    rom[0xC0:0xC4] = struct.pack("<I", 0xEAFFFFFE)  # B .
    return bytes(rom)


def vb():
    # 64 KiB of MOV r0,r0 (zero), its header where the Virtual Boy reads it, at the end
    rom = bytearray(0x10000)
    header = len(rom) - 0x220
    rom[header:header + 20] = b"PLAYTENDOH TEST     "
    rom[header + 0x19:header + 0x1B] = b"01"
    rom[header + 0x1B:header + 0x1F] = b"TEST"
    return bytes(rom)


def pokemini():
    rom = bytearray(0x10000)
    rom[0x2100:0x2102] = b"MN"
    rom[0x21AC:0x21B0] = b"TEST"
    rom[0x21B0:0x21BC] = b"PLAYTENDOH  "
    rom[0x21BC:0x21C0] = b"2P\x00\x00"
    return bytes(rom)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    games = {"test.nes": nes(), "test.sfc": snes(), "test.gb": gb(False), "test.gbc": gb(True), "test.gba": gba(),
             "test.vb": vb(), "test.min": pokemini(), "input.sfc": snes_input(), "input.gba": gba_input(), "broken.z64": b"not a Nintendo 64 game" * 100,
             "broken.mgw": b"not a Game & Watch" * 10}
    for name, data in games.items():
        with open(os.path.join(out, name), "wb") as f:
            f.write(data)
    print(f"{len(games)} test games in {out}")


if __name__ == "__main__":
    main()
