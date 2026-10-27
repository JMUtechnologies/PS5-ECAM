#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -I"$root/port" -I"$root/.deps/rapidjson/include" \
    "$root/tools/tests/test-eden-extras.cpp" "$root/port/eden/extras.cpp" -o "$work/test-extras"
"$work/test-extras" "$work/data"
