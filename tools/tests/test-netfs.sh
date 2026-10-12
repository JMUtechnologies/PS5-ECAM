#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The /net layer (port/ps5/netfs_files.cpp) on a PC: built with the same --wrap list as tools/link.sh,
# libstdc++ linked in so its file calls are wrapped as libc++'s are in the app, over test-netfs.cpp's
# stand-in share. Prints "ok".
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
wraps=()
for name in open read pread lseek close fstat stat lstat realpath fopen fclose fflush fread fwrite fseek fseeko ftell ftello \
        rewind feof ferror clearerr fgetc getc ungetc fgets fileno access opendir readdir closedir; do
    wraps+=("-Wl,--wrap=$name")
done
mkdir -p "$work/share"
clang++-18 -std=c++20 -O1 -g -Wall -o "$work/test-netfs" "$root/tools/tests/test-netfs.cpp" \
    "$root/port/ps5/netfs_files.cpp" -static-libstdc++ -static-libgcc "${wraps[@]}"
NETFS_ROOT=$work/share "$work/test-netfs"
