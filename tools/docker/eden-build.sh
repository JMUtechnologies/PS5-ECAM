#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
mkdir -p build/logs
log="build/logs/eden-$(date +%Y%m%d-%H%M%S).log"
export JOBS=${JOBS:-1}
{
    bash tools/docker/workstation.sh image
    bash tools/docker/workstation.sh doctor
    bash tools/docker/workstation.sh run bash -c '
        set -uo pipefail
        make eden
        result=$?
        if (( result != 0 )); then
            marker=/src/.deps/ProsperoEdenX/.local/headless-cache
            if [[ -f $marker ]]; then
                scratch=$(cat "$marker")
                case "$scratch" in
                    /root/.cache/ps5-eden-headless.*)
                        diagnostic="$scratch/ffmpeg-native/ffbuild/config.log"
                        if [[ -f $diagnostic ]]; then
                            cp "$diagnostic" /src/build/logs/eden-ffmpeg-config.log
                            tail -100 "$diagnostic"
                        fi
                        ;;
                esac
            fi
        fi
        exit "$result"
    '
} 2>&1 | tee "$log"
