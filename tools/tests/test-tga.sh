#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The in-game menus' pictures (port/app/tga.h): TGAs read and written, other pictures through a decoder,
# each scaled down. Needs a C++20 compiler.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -fsanitize=address,undefined -I"$root/port" "$root/tools/tests/test-tga.cpp" -o "$work/test"
"$work/test" "$work"
