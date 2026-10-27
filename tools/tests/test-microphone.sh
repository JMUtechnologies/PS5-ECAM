#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$root/port" "$root/tools/tests/test-microphone.cpp" -o "$work/test-mic"
"$work/test-mic" "$work/data"
# and again with lstat refused, as the console's /data refuses it to an app without a HEN
# (port/app/own_status.h)
"${CC:-cc}" -shared -fPIC "$root/tools/tests/refuse-lstat.c" -o "$work/refuse-lstat.so" -ldl
rm -rf "$work/data"
echo "lstat refused with EPERM, as /data is in an app without a HEN:"
REFUSE_LSTAT_UNDER="$work/data" LD_PRELOAD="$work/refuse-lstat.so" "$work/test-mic" "$work/data"
