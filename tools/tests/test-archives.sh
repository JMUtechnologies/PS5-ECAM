#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Games in archives on a PC (port/app/game_archives.cpp): the walk over a .zip (libzip), a .tar.gz and a
# .7z (libarchive), what the 3DS side's library (port/azahar/library.cpp) and the Wii U side's
# (port/app/wiiu_archives.cpp) find in them, and their games unpacked, over archives made here from the
# preview's 3DS samples (tools/launcher-preview/make-3ds-samples.py) and Wii U titles of a few bytes; what
# the 3DS side's library reads of Azahar's compressed files (.zcci, .zcxi, .z3dsx, .zcia), alone and in an
# archive; and a .zip on a network share, through the /net layer linked with tools/link.sh's --wrap list.
# Needs clang-18, python3, pkg-config with libarchive's, libzip's and zstd's (libarchive-dev, libzip-dev,
# libzstd-dev), what make deps fetches (pacbrew's fmt, pugixml), and 7z for the .7z cases (skipped
# without it). Prints "ok".
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source "$root/tools/env.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

python3 -B - "$root" "$work" <<'EOF'
import ctypes, ctypes.util, importlib.util, os, random, shutil, struct, subprocess, sys, tarfile, zipfile

root, work = sys.argv[1], sys.argv[2]
samples = os.path.join(work, "samples")
subprocess.run([sys.executable, "-B", os.path.join(root, "tools/launcher-preview/make-3ds-samples.py"), samples + "/3ds", samples + "/ds"], check=True)
def sample(name):
    with open(os.path.join(samples, "3ds", name), "rb") as file:
        return file.read()
seven = shutil.which("7z")

def seven_zip(archive, folder, names):
    subprocess.run([seven, "a", "-bd", "-bso0", "-bsp0", archive, *names], cwd=folder, check=True)

# -- the 3DS side's -----------------------------------------------------------------------------------
n3ds = os.path.join(work, "3ds")
os.makedirs(n3ds)
# a ROM with a Mac's copy of it before it and a readme after it
with zipfile.ZipFile(n3ds + "/Mario.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("__MACOSX/._Super Mario 3D Land.3ds", bytes(4096))
    z.writestr("Super Mario 3D Land.3ds", sample("Super Mario 3D Land.3ds"))
    z.writestr("readme.txt", b"hello")
# two ROMs: the first by name, though it comes second
with zipfile.ZipFile(n3ds + "/Two.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("b/Mario Kart 7.cci", sample("Mario Kart 7.cci"))
    z.writestr("a/Fire Emblem Awakening.cxi", sample("Fire Emblem Awakening.cxi"))
# a CIA alone: installed, not played
with zipfile.ZipFile(n3ds + "/Kirby.zip", "w") as z:
    z.writestr("Kirby Planet Robobot.cia", sample("Kirby Planet Robobot.cia"))
# homebrew in a tar.gz, in a folder
with tarfile.open(n3ds + "/Checkpoint.tar.gz", "w:gz") as tar:
    tar.add(os.path.join(samples, "3ds", "Checkpoint.3dsx"), "Checkpoint/Checkpoint.3dsx")
# an encrypted dump: listed, as encrypted
with zipfile.ZipFile(n3ds + "/Luigi.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("Luigi's Mansion Dark Moon.3ds", sample("Luigi's Mansion Dark Moon.3ds"))
# a game in a file of its own and in an archive: the file's
shutil.copy(os.path.join(samples, "3ds", "More", "Kid Icarus Uprising.3ds"), n3ds)
with zipfile.ZipFile(n3ds + "/Icarus.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("Kid Icarus Uprising.3ds", sample("More/Kid Icarus Uprising.3ds"))
# no archive at all
with open(n3ds + "/Broken.zip", "wb") as file:
    file.write(b"not a zip " * 100)
if seven:
    # a 7z's names are UTF-16: "Pokémon" does not convert in the C locale, and the ROM is found by its first bytes
    staged = os.path.join(work, "staged-3ds")
    os.makedirs(staged)
    shutil.copy(os.path.join(samples, "3ds", "Pokémon Ultra Sun.3ds"), staged)
    seven_zip(n3ds + "/Pokemon.7z", staged, ["Pokémon Ultra Sun.3ds"])

# -- Azahar's compressed files, on the 3DS side ---------------------------------------------------------
spec = importlib.util.spec_from_file_location("samples", os.path.join(root, "tools/launcher-preview/make-3ds-samples.py"))
make = importlib.util.module_from_spec(spec)
spec.loader.exec_module(make)
libzstd = ctypes.CDLL(ctypes.util.find_library("zstd") or "libzstd.so.1")
libzstd.ZSTD_compressBound.restype = ctypes.c_size_t
libzstd.ZSTD_compressBound.argtypes = [ctypes.c_size_t]
libzstd.ZSTD_compress.restype = ctypes.c_size_t
libzstd.ZSTD_compress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int]

def zstd_frame(data):
    bound = libzstd.ZSTD_compressBound(len(data))
    out = ctypes.create_string_buffer(bound)
    size = libzstd.ZSTD_compress(out, bound, data, len(data), 3)
    assert size <= bound
    return out.raw[:size]

def z3ds(data, magic, frame=256 * 1024, metadata=None, checksums=False):
    """Azahar's compression of a file (common/file_derived.cpp): the Z3DS header, its metadata, then the
    file in zstd's seekable format, frames of frame bytes each, and after them the table of their sizes"""
    frames = [data[at:at + frame] for at in range(0, len(data), frame)]
    compressed = [zstd_frame(part) for part in frames]
    entries = b"".join(struct.pack("<II", len(c), len(f)) + (bytes(4) if checksums else b"") for c, f in zip(compressed, frames))
    footer = struct.pack("<IBI", len(frames), 0x80 if checksums else 0, 0x8F92EAB1)
    body = b"".join(compressed) + struct.pack("<II", 0x184D2A5E, len(entries) + len(footer)) + entries + footer
    meta = b""
    if metadata:
        meta = b"\x01" + b"".join(struct.pack("<BBH", 1, len(name), len(value)) + name.encode() + value for name, value in metadata.items()) + bytes(4)
        meta += bytes(-len(meta) % 16)
    return b"Z3DS" + magic.ljust(4, b"\0") + struct.pack("<BBHIQQ", 1, 0, 0x20, len(meta), len(body), len(data)) + meta + body

def title_info(title_id, version):
    return struct.pack("<QQHHI", title_id, 0, version, 0, 0)

def far_icon(rom, ncch_at, filler, product=None):
    """rom with filler ahead of its ExeFS's icon, as a game's code is: in its compressed copy, the icon is
    frames from the headers; and with that product code"""
    rom = bytearray(rom)
    if product:
        rom[ncch_at + 0x150:ncch_at + 0x160] = product.encode().ljust(16, b"\0")
    exefs = ncch_at + struct.unpack_from("<I", rom, ncch_at + 0x1A0)[0] * 0x200
    offset, size = struct.unpack_from("<II", rom, exefs + 8)
    struct.pack_into("<I", rom, exefs + 8, offset + filler)
    icon = bytes(rom[exefs + 0x200 + offset:exefs + 0x200 + offset + size])
    return bytes(rom[:exefs + 0x200 + offset]) + random.Random(filler).randbytes(filler) + icon

compressed = os.path.join(work, "3ds-z")
os.makedirs(compressed)
def write(path, data):
    with open(path, "wb") as file:
        file.write(data)
# small frames, thousands of them, the icon some way in: the seek table read 512 entries at a time
write(compressed + "/Mario Kart 7.zcci", z3ds(far_icon(sample("Mario Kart 7.cci"), 0x4000, 0x30000), b"NCSD", frame=0x100,
    metadata={"titleinfo": title_info(0x0004000000030600, 0)}))
# a seek table with a checksum in each entry
write(compressed + "/Fire Emblem Awakening.zcxi", z3ds(far_icon(sample("Fire Emblem Awakening.cxi"), 0, 0x8000, "CTR-P-AFEE"), b"NCCH",
    frame=0x1000, checksums=True))
write(compressed + "/Checkpoint.z3dsx", z3ds(sample("Checkpoint.3dsx"), b"3DSX"))
# a CIA's SMDH from the metadata, its own (at its end) blank; an update's, not listed
kirby = make.smdh("Kirby: Planet Robobot", "Nintendo", (240, 120, 180))
write(compressed + "/Kirby Planet Robobot.zcia", z3ds(make.cia(0x000400000017C100, 1040, bytes(len(kirby))), b"CIA", frame=32 << 20,
    metadata={"titleinfo": title_info(0x000400000017C100, 1040), "smdh": kirby}))
write(compressed + "/Kirby Planet Robobot Update.zcia", z3ds(sample("Kirby Planet Robobot Update.cia"), b"CIA", frame=32 << 20,
    metadata={"smdh": kirby}))
# compressed, named as a plain dump is: read by its first bytes, as Azahar does
write(compressed + "/Renamed.cci", z3ds(sample("More/Kid Icarus Uprising.3ds"), b"NCSD"))
# one in an archive: read from its start, as an archive's entry is
with zipfile.ZipFile(compressed + "/Compressed.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("Super Mario 3D Land.zcci", z3ds(far_icon(sample("Super Mario 3D Land.3ds"), 0x4000, 0x8000), b"NCSD", frame=0x1000))
if seven:
    # a name the C locale does not convert: found by its first bytes, a compressed NCSD's
    staged = os.path.join(work, "staged-3ds-z")
    os.makedirs(staged)
    write(os.path.join(staged, "Pokémon Ultra Sun.zcci"), z3ds(sample("Pokémon Ultra Sun.3ds"), b"NCSD", frame=0x1000))
    seven_zip(compressed + "/Pokemon.7z", staged, ["Pokémon Ultra Sun.zcci"])
# a CIA without its SMDH beside it, apart: its own read at its end, then what comes ahead of it
os.makedirs(os.path.join(work, "3ds-z-alone"))
write(os.path.join(work, "3ds-z-alone", "Kirby.zcia"), z3ds(sample("Kirby Planet Robobot.cia"), b"CIA", frame=0x400))

# -- the Wii U side's -----------------------------------------------------------------------------------
def tga(width, height):
    header = bytearray(18)
    header[2] = 2
    struct.pack_into("<HH", header, 12, width, height)
    header[16], header[17] = 32, 0x28
    return bytes(header) + bytes([220, 120, 40, 255]) * (width * height)

def title(prefix, title_id, name, product, code="code", content="content", meta="meta", rpx="Gambit.rpx", with_content=True):
    files = {
        f"{prefix}{code}/app.xml": (f'<?xml version="1.0" encoding="utf-8"?>\n<app type="complex" access="777">\n'
            f'  <title_id type="hexBinary" length="8">{title_id:016X}</title_id>\n'
            f'  <title_version type="hexBinary" length="2">0010</title_version>\n</app>\n').encode(),
        f"{prefix}{code}/cos.xml": f'<?xml version="1.0"?>\n<app><argstr>{rpx}</argstr></app>\n'.encode(),
        f"{prefix}{code}/{rpx}": b"\x7fRPX" + bytes(1000),
        f"{prefix}{meta}/meta.xml": (f'<?xml version="1.0" encoding="utf-8"?>\n<menu type="complex" access="777">\n'
            f'  <title_id type="hexBinary" length="8">{title_id:016X}</title_id>\n'
            f'  <product_code type="string" length="32">{product}</product_code>\n'
            f'  <company_code type="string" length="8">0001</company_code>\n'
            f'  <longname_ja type="string" length="512">スプラトゥーン</longname_ja>\n'
            f'  <longname_en type="string" length="512">{name}</longname_en>\n</menu>\n').encode(),
        f"{prefix}{meta}/iconTex.tga": tga(8, 8),
    }
    if with_content:
        files[f"{prefix}{content}/Common/Pack/x.pack"] = bytes(range(256)) * 40
    return files

def write_zip(path, files):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in files.items():
            z.writestr(name, data)

wiiu = os.path.join(work, "wiiu")
os.makedirs(wiiu)
write_zip(wiiu + "/Splatoon.zip", title("Splatoon [0005000010176900]/", 0x0005000010176900, "Splatoon\nInk", "WUP-P-AGME"))
write_zip(wiiu + "/Update.zip", title("Update/", 0x0005000E10176900, "Splatoon", "WUP-P-AGME"))
write_zip(wiiu + "/NoContent.zip", title("Game/", 0x0005000010172600, "Pikmin 3", "WUP-P-AC3E", with_content=False))
write_zip(wiiu + "/Case.zip", title("Case/", 0x0005000010143500, "The Wind Waker HD", "WUP-P-BCZE", code="Code", content="Content", meta="Meta", rpx="Zelda.RPX"))
evil = title("Evil/", 0x000500001014B800, "Xenoblade", "WUP-P-AX5E")
evil["Evil/content/../../escape.txt"] = b"out"
write_zip(wiiu + "/Evil.zip", evil)
staged = os.path.join(work, "staged-two")
for name, data in {**title("B Game/", 0x0005000010101D00, "B", "WUP-P-ATWE"), **title("A Game/", 0x000500001010EC00, "A", "WUP-P-AMKE")}.items():
    os.makedirs(os.path.dirname(os.path.join(staged, name)), exist_ok=True)
    with open(os.path.join(staged, name), "wb") as file:
        file.write(data)
with tarfile.open(wiiu + "/Two.tar.gz", "w:gz") as tar:
    tar.add(os.path.join(staged, "B Game"), "B Game")
    tar.add(os.path.join(staged, "A Game"), "A Game")
if seven:
    staged = os.path.join(work, "staged-top")
    for name, data in title("", 0x0005000010145D00, "Top", "WUP-P-ARDE").items():
        os.makedirs(os.path.dirname(os.path.join(staged, name)), exist_ok=True)
        with open(os.path.join(staged, name), "wb") as file:
            file.write(data)
    seven_zip(wiiu + "/Top.7z", staged, ["code", "content", "meta"])
EOF

mkdir -p "$work/include"
ln -s "$PS5CEMU_PACBREW/include/fmt" "$work/include/fmt"
wraps=()
for name in open read pread lseek close fstat stat lstat realpath fopen fclose fflush fread fwrite fseek fseeko ftell ftello \
        rewind feof ferror clearerr fgetc getc ungetc fgets fileno access opendir readdir closedir; do
    wraps+=("-Wl,--wrap=$name")
done
clang++-18 -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -DFMT_HEADER_ONLY \
    -I "$work/include" -I "$root/.deps/pugixml-1.15/src" -o "$work/test-archives" "$root/tools/tests/test-archives.cpp" \
    "$root/port/app/game_archives.cpp" "$root/port/app/wiiu_archives.cpp" "$root/port/azahar/library.cpp" \
    "$root/port/ps5/netfs_files.cpp" "$root/.deps/pugixml-1.15/src/pugixml.cpp" "${wraps[@]}" \
    $(pkg-config --cflags --libs libarchive libzip libzstd) -lpthread
"$work/test-archives" "$work" "$root/tools/launcher-preview"
