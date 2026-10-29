#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if ! docker info >/dev/null; then
    echo 'Docker daemon is inaccessible. Start Docker Desktop and check session socket permission.' >&2
    exit 1
fi
compose=(docker compose -f "$here/compose.yaml")
case "${1:-doctor}" in
    doctor)
        "${compose[@]}" config --quiet
        "${compose[@]}" run --rm builder bash -lc 'set -e; clang-18 --version; ld.lld-18 --version; cmake --version; meson --version; gcc --version; printf "#include <ctype.h>\n_Static_assert(__STDC_VERSION__ >= 201112L, \"C11 required\");\nint main(void) { return 0; }\n" | gcc -std=c11 -x c - -o /tmp/ecam-host-c11; /tmp/ecam-host-c11; uname -m'
        ;;
    image) "${compose[@]}" build builder ;;
    run)
        shift
        "${compose[@]}" run --rm builder "$@"
        ;;
    *) echo 'Usage: bash tools/docker/workstation.sh {image|doctor|run COMMAND...}' >&2; exit 2 ;;
esac
