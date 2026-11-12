#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The cheats' files (port/app/cheats.cpp): RetroArch's .cht, melonDS's .mch, Azahar's Gateway file and the Gecko
# codes' text, read, turned on and off and converted as the app does (tools/tests/test-cheats.cpp)
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
compiler=${CXX:-$(command -v clang++-18 || command -v clang++ || command -v g++)}
"$compiler" -std=c++20 -Wall -Wextra -Wno-unused-parameter -DFMT_HEADER_ONLY -I"$root/port" -I"$root/port/app" \
    "$root/tools/tests/test-cheats.cpp" "$root/port/app/cheats.cpp" -o "$work/test-cheats"
"$work/test-cheats" "$work"
