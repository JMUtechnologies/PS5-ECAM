#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
sanitize=()
if [[ ${ECAM_TEST_SANITIZE:-0} == 1 ]]; then sanitize=(-fsanitize=address,undefined -fno-omit-frame-pointer -g); fi
"${CXX:-c++}" "${sanitize[@]}" -std=c++20 -pthread -Wall -Wextra -Werror -Wno-deprecated-declarations \
 -DPS5CEMU_DATA="\"$work/data\"" -I"$root/port" -I"${ECAM_RAPIDJSON_INCLUDE:-$root/.deps/rapidjson/include}" \
 "$root/tools/tests/test-retro.cpp" "$root/port/retro/library.cpp" "$root/port/retro/launch.cpp" \
 "$root/port/frontend/settings.cpp" "$root/port/app/catalog.cpp" -o "$work/test-retro"
"$work/test-retro" "$work/files"
if [[ $(uname -s) == Linux ]]; then
    "${CC:-cc}" -shared -fPIC "$root/tools/tests/refuse-lstat.c" -o "$work/refuse-lstat.so" -ldl
    echo "Retro discovery with lstat refused as on the PS5:"
    REFUSE_LSTAT_UNDER="$work" LD_PRELOAD="$work/refuse-lstat.so" "$work/test-retro" "$work/refused-files"
    "${CC:-cc}" -shared -fPIC "$root/tools/tests/collapse-realpath.c" -o "$work/collapse-realpath.so" -ldl
    echo "Retro discovery with collapsed realpath results:"
    COLLAPSE_REALPATH_UNDER="$work" LD_PRELOAD="$work/collapse-realpath.so" "$work/test-retro" "$work/collapsed-files"
fi
