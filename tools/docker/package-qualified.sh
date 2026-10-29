#!/usr/bin/env bash
# AI-assisted: assemble a complete test bundle, without release or deployment.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
[[ -s build/cemu/ps5cemu.elf && ! -e build/cemu/ps5cemu.elf.linkcheck ]] || {
    echo 'A real ECAM/RADV executable is required before packaging.' >&2; exit 1;
}
[[ -s build/eden/app/eboot.bin ]] || {
    echo 'A successfully qualified Eden runtime is required before packaging.' >&2; exit 1;
}
mkdir -p build/qualification
python3 tools/docker/verify-staged-runtimes.py build/retro \
    --receipt build/qualification/runtimes.json
bash tools/package.sh
app="$root/build/app/PPSA99360"
tool="$root/build/host/ps5-native-tool"
mkdir -p build/qualification/inspections
for runtime in porpoise n64 nes mupen64plus fceumm gbc gba; do
    "$tool" self --inspect --file "$app/retro/$runtime/eboot.bin" \
        >"build/qualification/inspections/$runtime.txt"
done
"$tool" self --inspect --file "$app/eden/eboot.bin" >build/qualification/inspections/eden.txt
python3 - <<'PY'
import hashlib,json,zipfile
from pathlib import Path
root=Path.cwd(); app=root/'build/app/PPSA99360'; out=root/'build/qualification'
param=json.loads((app/'sce_sys/param.json').read_text())
assert param['titleId']=='PPSA99360',param
for name in ('eboot.bin','sce_module/libc.prx','sandbox-elevator.elf','tile-installer.elf','eden/eboot.bin'):
    assert (app/name).is_file() and (app/name).stat().st_size>0,name
files=sorted(app.rglob('*'))
assert all(not p.is_symlink() for p in files),'Unexpected symlink in test app'
receipt=json.loads((out/'runtimes.json').read_text())
receipt.update(schema='ecam-test-bundle/1',ps5_hardware_tested=False,
               qualification='Native build and structural checks only; PS5 4.50 tests pending')
(app/'ECAM-QUALIFICATION.json').write_text(json.dumps(receipt,indent=2)+'\n')
archive=out/'PS5-ECAM-qualification.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for path in sorted(app.rglob('*')):
        z.write(path,path.relative_to(app.parent))
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None,'ZIP integrity failure'
    for kind in ('porpoise','n64','nes','mupen64plus','fceumm','gbc','gba'):
        assert f'PPSA99360/retro/{kind}/eboot.bin' in z.namelist(),kind
with archive.open('rb') as f: digest=hashlib.file_digest(f,'sha256').hexdigest()
(out/'SHA256SUMS').write_text(digest+'  '+archive.name+'\n')
print('Complete test bundle:',archive,'SHA256',digest)
print('No console installation, release, or hardware qualification performed.')
PY
