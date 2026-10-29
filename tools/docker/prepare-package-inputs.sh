#!/usr/bin/env bash
# Recreate public SDK/link inputs for packaging already compiled ELFs.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
python3 tools/docker/seed-zlib.py .deps/ps5-native-app-boilerplate .deps/PS5_Vulkan
python3 -B tools/deps.py fetch cemu boilerplate ps5-vulkan ps5-payload-sdk-fork forwarder-format
export PS5_PAYLOAD_SDK_FORK="$root/.deps/PS5_PayloadSDK"
bash .deps/PS5_Vulkan/tools/setup-native-dependencies.sh
sdk="$root/.deps/PS5_Vulkan/.deps/native/ps5-payload-sdk"
mkdir -p build/link
# These are the same link-only module import stubs used by tools/link.sh.
for spec in libSceAgc:agc_canary_link_stub libSceAgcDriver:agc_driver_canary_link_stub; do
    library=${spec%%:*}
    source="$root/.deps/PS5_Vulkan/vendor/ps5/sdk/stubs/${spec#*:}.c"
    clang-18 -target x86_64-sie-ps5 -isysroot "$sdk" -isystem "$sdk/target/include" \
        -std=c11 -O2 -fPIC -fvisibility-nodllstorageclass=default \
        -c "$source" -o "build/link/$library.o"
    "$sdk/bin/prospero-lld" --shared -soname "$library.prx" \
        -o "build/link/$library.so" "build/link/$library.o"
done
