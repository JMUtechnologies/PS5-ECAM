#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the Switch side's program (build/eden): ProsperoEdenX, premohq's fork of Sword's ProsperoEden
# (Eden, the Switch emulator, for the PS5; GPL-3.0-or-later), from .deps/ProsperoEdenX with
# patches/eden, by its own build (its Makefile's package: its pinned inputs, RADV, FFmpeg, Eden). The
# app carries it as eden/ (tools/package.sh): the Switch side starts eden/eboot.bin with LoadExec, and
# it starts PS5-ECAM's eboot.bin again when its game ends or its own launcher is left (patches/eden).
# Every ProsperoEden feature is in it, its own launcher with them (mods, cheats, updates and DLC, save
# sync, rooms, downloads, per-game settings): the Switch side opens it for what it has no page of its
# own for.
#
#   tools/build-eden.sh    apply patches/eden if needed, build, copy the program to build/eden
#
# ProsperoEdenX's build wants CMake 3.31 or later, where Ubuntu 24.04 has 3.28: EDEN_CMAKE_DIR is a
# folder with a newer cmake (default ~/hosttools/cmake-3.31.8/bin, Kitware's own build), put first on
# the PATH of its scripts (EDEN_HOST_PATH, patches/eden). RADV_BUILD_JOBS (default 4) and EDEN_JOBS
# (default 5) keep its compiles within a 7 GB WSL.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
eden=$PS5CEMU_ROOT/.deps/ProsperoEdenX
out=$PS5CEMU_BUILD/eden

bash "$PS5CEMU_ROOT/tools/eden-patches.sh" apply

# its inputs this repository has too, at the same pins: pacbrew beside it, the boilerplate and the
# PS5_Mesa and PS5_PayloadSDK checkouts as its ../ (tools/deps.json)
mkdir -p "$eden/.deps" "$out"
[[ -e $eden/.deps/pacbrew-0.40.2 ]] || ln -s ../../pacbrew-0.40.2 "$eden/.deps/pacbrew-0.40.2"
# The PS5 OpenGL SDK 1.0.2 it pins, whose release went with blackbearreloaded's account: the same
# archive, checked against the pin's SHA-256, from EDEN_GL_SDK_ARCHIVE (default ~/dl-glsdk, where
# it was fetched from darkxex/ps5-opengl's copy of the release)
gl=$eden/.deps/ps5-opengl-sdk-1.0.2
if [[ ! -f $gl/sdk/lib/libps5_opengl_core33.a ]]; then
    archive=${EDEN_GL_SDK_ARCHIVE:-$HOME/dl-glsdk/ps5-opengl-sdk-1.0.2.tar.gz}
    pin=30d8e51dd062739b629346102be5932728ba714bc50b1bdcdf08306c8843d9a4
    if [[ ! -f $archive || $(sha256sum "$archive" | cut -d' ' -f1) != "$pin" ]]; then
        echo "ProsperoEdenX needs ps5-opengl-sdk-1.0.2.tar.gz (SHA-256 $pin), no longer at its pinned URL:" >&2
        echo "set EDEN_GL_SDK_ARCHIVE to a copy of it" >&2
        exit 2
    fi
    tar -xzf "$archive" -C "$eden/.deps"
fi

export EDEN_HOST_PATH=${EDEN_CMAKE_DIR:-$HOME/hosttools/cmake-3.31.8/bin}
[[ -x $EDEN_HOST_PATH/cmake ]] || { echo "no cmake in $EDEN_HOST_PATH (EDEN_CMAKE_DIR): ProsperoEdenX wants 3.31 or later" >&2; exit 2; }
export RADV_BUILD_JOBS=${RADV_BUILD_JOBS:-4}
make -C "$eden" package JOBS="${EDEN_JOBS:-5}"

# The program and the files it reads from its own folder (storage_paths.h: AppDir() is the app's
# eden/). Left out: its sce_sys and sce_module (the app's are PS5-ECAM's), its self-updater (the
# app updates eden/ with everything else, and the check is off in patches/eden) and the test
# program it carries.
package=$eden/build/release/PPSA99008
[[ -f $package/eboot.bin ]] || { echo "$package/eboot.bin is missing" >&2; exit 2; }
rm -rf "$out/app"
mkdir -p "$out/app"
(cd "$package" && find . -mindepth 1 -maxdepth 1 \
    ! -name sce_sys ! -name sce_module ! -name self-updater.elf ! -name core-homebrew.nro \
    ! -name 'lapy*' -exec cp -a {} "$out/app/" \;)
cp "$eden/LICENSE" "$out/app/LICENSE.ProsperoEden.txt"
cp "$eden/THIRD_PARTY_NOTICES.md" "$out/app/THIRD_PARTY_NOTICES.md"
# the unstripped program, for its crash reports (ProsperoEdenX's tools/symbolize-crash.py)
cp "$eden/build/headless-native/llvm-pie.elf" "$out/eden.elf" 2>/dev/null || true
git -C "$eden" rev-parse HEAD >"$out/commit"
echo "==> [eden] $out/app ($(du -sh "$out/app" | cut -f1), eboot.bin $(stat -c %s "$out/app/eboot.bin") bytes)"
