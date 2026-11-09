#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A title screen made a full-screen picture (port/app/banner_compose.h). Needs a C++20 compiler.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -fsanitize=address,undefined -I"$root/port" "$root/tools/tests/test-banner-compose.cpp" -o "$work/test"
"$work/test"
