#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# An update's files on a PC (tools/tests/test-update.cpp, port/app/update_files.cpp): a release's ZIP of
# a thousand files unpacked four at a time, and the ways it can fail. SANITIZE=thread (or address) builds
# it with that sanitizer, where the compiler (CXX) has its library. Needs clang-18 (or g++), python3, libzip's
# and fmt's libraries (libzip-dev, libfmt-dev). Prints PASS lines; exits 1 on a failure.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

python3 -B - "$work" <<'EOF'
import os, random, sys, zipfile
work = sys.argv[1]
source = os.path.join(work, "source")
random.seed(1)
names = ["eboot.bin", "sce_sys/param.json"] + [f"assets/dir{d}/sub{d % 3}/file{i}.bin" for d in range(40) for i in range(25)]
random.shuffle(names)
with zipfile.ZipFile(os.path.join(work, "release.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("PPSA99360/", b"")
    z.writestr("../evil.txt", b"outside")
    z.writestr("PPSA99360/assets/", b"")
    for name in names:
        data = os.urandom(random.randint(0, 100000)) if random.random() < 0.5 else b"text " * random.randint(0, 20000)
        os.makedirs(os.path.dirname(os.path.join(source, name)), exist_ok=True)
        with open(os.path.join(source, name), "wb") as file:
            file.write(data)
        z.writestr("PPSA99360/" + name, data)
with open(os.path.join(work, "expected.txt"), "w") as file:
    file.write("\n".join(names) + "\n")
with zipfile.ZipFile(os.path.join(work, "noeboot.zip"), "w") as z:
    z.writestr("PPSA99360/readme.txt", b"no program")
EOF

compiler=${CXX:-$(command -v clang++-18 || command -v clang++ || command -v g++)}
sanitize=()
[[ -n ${SANITIZE:-} ]] && sanitize=(-fsanitize="$SANITIZE")
"$compiler" -std=c++20 -O1 -g -Wall -Wextra "${sanitize[@]}" -I"$root/port" "$root/tools/tests/test-update.cpp" \
    "$root/port/app/update_files.cpp" -lzip -lfmt -pthread -o "$work/test-update"
"$work/test-update" "$work"
