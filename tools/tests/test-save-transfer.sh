#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$root/port" \
    "$root/tools/tests/test-save-transfer.cpp" "$root/port/eden/save_transfer.cpp" -o "$work/test-transfer"
"$work/test-transfer" "$work/data"
