#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The port's changes to Mihawk's Mesa fork (RADV, the PS5's Vulkan driver), kept as
# patches/mesa/*.patch against the revision pinned in deps.json ("ps5-mesa"), as patches/azahar and
# patches/cemu are kept for theirs.
#
#   tools/mesa-patches.sh apply    put them on a branch named ps5 in .deps/PS5_Mesa (once)
#   tools/mesa-patches.sh export   write the ps5 branch's commits back to patches/mesa
#   tools/mesa-patches.sh head     print the revision RADV is built from: the ps5 branch's, or the
#                                  pin when there are no patches
#
# To change Mesa: edit .deps/PS5_Mesa on the ps5 branch, commit, then export.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
mesa=$PS5CEMU_ROOT/.deps/PS5_Mesa
patches=$PS5CEMU_ROOT/patches/mesa
pin=$(python3 -c 'import json,sys; print(next(i["commit"] for i in json.load(open(sys.argv[1]))["items"] if i["name"] == "ps5-mesa"))' \
    "$PS5CEMU_ROOT/tools/deps.json")
identity=(-c user.name=ps5cemu -c user.email=ps5cemu@localhost)

case ${1:-} in
apply)
    if git -C "$mesa" rev-parse -q --verify refs/heads/ps5 >/dev/null; then
        exit 0 # already there (and possibly being worked on)
    fi
    git -C "$mesa" checkout -q -b ps5 "$pin"
    shopt -s nullglob
    files=("$patches"/*.patch)
    if ((${#files[@]})); then
        git "${identity[@]}" -C "$mesa" am -q --keep-cr "${files[@]}"
    fi
    echo "==> [mesa] applied ${#files[@]} patches on branch ps5"
    ;;
export)
    [[ -z $(git -C "$mesa" status --porcelain --untracked-files=no) ]] ||
        { echo "commit the changes in .deps/PS5_Mesa first" >&2; exit 1; }
    rm -f "$patches"/*.patch
    mkdir -p "$patches"
    git -C "$mesa" format-patch -q --no-signature --zero-commit --no-numbered -o "$patches" "$pin..ps5"
    ls "$patches"
    ;;
head)
    if git -C "$mesa" rev-parse -q --verify refs/heads/ps5 >/dev/null; then
        git -C "$mesa" rev-parse ps5
    else
        echo "$pin"
    fi
    ;;
*)
    echo "usage: tools/mesa-patches.sh apply|export|head" >&2
    exit 2
    ;;
esac
