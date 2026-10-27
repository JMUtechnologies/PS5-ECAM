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
# and again with lstat refused, as the console's /data refuses it to an app without a HEN
# (port/app/own_status.h)
"${CC:-cc}" -shared -fPIC "$root/tools/tests/refuse-lstat.c" -o "$work/refuse-lstat.so" -ldl
rm -rf "$work/data"
echo "lstat refused with EPERM, as /data is in an app without a HEN:"
REFUSE_LSTAT_UNDER="$work/data" LD_PRELOAD="$work/refuse-lstat.so" "$work/test-extras" "$work/data"
