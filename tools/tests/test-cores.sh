#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Every libretro core the app links, run for real on the PC through the app's own frontend
# (tools/tests/test-cores.cpp). Builds the cores for the PC first (LIBRETRO_HOST=1 tools/build-libretro.py,
# into build/libretro-host), then links them as the app does: renamed entry points, found by cores.cpp.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out=$root/build/test-cores
mkdir -p "$out"
if [[ ${SKIP_CORE_BUILD:-0} != 1 ]]; then
    LIBRETRO_HOST=1 python3 "$root/tools/build-libretro.py"
fi
include=$root/.deps/libretro/libretro-common/include
rc=$root/.deps/rcheevos
python3 -B "$root/tools/tests/make-test-games.py" "$out/games"
# rcheevos, as port/CMakeLists.txt builds it (rc_libretro.c: the cores' memory for RetroAchievements)
rcsources=("$rc"/src/rcheevos/*.c "$rc"/src/rapi/*.c "$rc/src/rc_client.c" "$rc/src/rc_compat.c" "$rc/src/rc_util.c" "$rc/src/rc_version.c"
    "$rc/src/rc_libretro.c" "$rc/src/rhash/hash.c" "$rc/src/rhash/hash_rom.c" "$rc/src/rhash/md5.c")
mkdir -p "$out/rc"
objects=()
for source in "${rcsources[@]}"; do
    object=$out/rc/$(basename "${source%.c}").o
    objects+=("$object")
    [[ $object -nt $source ]] || clang-18 -std=gnu11 -fPIC -O1 -w -I"$rc/include" -I"$rc/src" -I"$include" -DRC_CLIENT_SUPPORTS_HASH -DRC_DISABLE_LUA \
        -DRC_HASH_NO_DISC -DRC_HASH_NO_ENCRYPTED -DRC_HASH_NO_ZIP -c "$source" -o "$object"
done
clang++-18 -std=c++20 -fPIE -pie -O1 -g -Wall -Wextra -Wno-unused-parameter -I"$root/port" -I"$include" -I"$rc/include" -I"$rc/src" \
    "$root/tools/tests/test-cores.cpp" "$root/port/libretro/session.cpp" "$root/port/libretro/cores.cpp" "$root/port/libretro/devices.cpp" \
    $(cat "$root/build/libretro-host/objects.txt") "${objects[@]}" -lpthread -lz -o "$out/test-cores"
rm -rf "$out/work"
"$out/test-cores" "$out/games" "$out/work"
