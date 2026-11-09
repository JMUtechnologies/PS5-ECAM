#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Porpoise-X's saves a game at a time (port/retro/porpoise_saves.cpp). Needs a C++20 compiler. Prints PASS
# lines; exits 1 on a failure.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -fsanitize=address,undefined -I"$root/port" \
    "$root/tools/tests/test-porpoise-saves.cpp" "$root/port/retro/porpoise_saves.cpp" -o "$work/test"
mkdir "$work/files"
"$work/test" "$work/files"
