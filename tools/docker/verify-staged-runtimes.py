#!/usr/bin/env python3
"""Verify source receipts and executable headers before packaging (AI-assisted).

This does not execute an emulator or establish PS5 hardware compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
pins = {p['name']: p for p in json.loads((ROOT/'tools/retro/sources.json').read_text())['sources']}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
parser.add_argument('--systems', nargs='+', choices=list(pins), default=list(pins))
parser.add_argument('--receipt', type=Path)
args = parser.parse_args()
bridge_hash = hashlib.sha256((ROOT/'port/retro/runtime/ecam_bridge.h').read_bytes()).hexdigest()
receipts = []
for kind in args.systems:
    folder = args.directory/kind
    if not (folder/'ECAM-SOURCE.json').is_file():
        raise SystemExit(f'{kind}: source receipt missing; bundle is incomplete')
    metadata = json.loads((folder/'ECAM-SOURCE.json').read_text())
    pin = pins[kind]
    for key in ('name', 'repository', 'commit'):
        if metadata.get(key) != pin[key]: raise SystemExit(f'{kind}: source pin mismatch for {key}')
    header = ('ps5/coreorbis/ecam_bridge.h' if kind in ('mupen64plus','fceumm')
              else 'src/ecam_bridge.h' if kind == 'porpoise' else kind+'/src/ecam_bridge.h')
    if metadata.get('modified_sources_sha256', {}).get(header) != bridge_hash:
        raise SystemExit(f'{kind}: ECAM bridge hash mismatch')
    for required in ('eboot.bin','ECAM-LICENSE.txt','ECAM-BRIDGE-LICENSE.txt'):
        if not (folder/required).is_file(): raise SystemExit(f'{kind}: missing {required}')
    if not any(p.is_file() for p in (folder/'licenses/source').rglob('*')):
        raise SystemExit(f'{kind}: source notices are missing')
    if (folder/'lapy.elf').exists(): raise SystemExit(f'{kind}: unexpected standalone-title helper')
    executable = folder/'eboot.bin'
    if executable.stat().st_size < 100000 or executable.open('rb').read(4) != b'\x4f\x15\x3d\x1d':
        raise SystemExit(f'{kind}: missing or invalid signed SELF header')
    with executable.open('rb') as source:
        digest = hashlib.file_digest(source,'sha256').hexdigest()
    receipts.append({'system':kind,'repository':pin['repository'],'source_commit':pin['commit'],
                     'ecam_bridge_sha256':bridge_hash,'eboot_sha256':digest,
                     'eboot_bytes':executable.stat().st_size,'ps5_hardware_tested':False})
    print(f'{kind}: PASS pinned source, bridge, notices, signed SELF header; {executable.stat().st_size} bytes')
if args.receipt:
    args.receipt.parent.mkdir(parents=True,exist_ok=True)
    args.receipt.write_text(json.dumps({'schema':'ecam-runtime-verification/1','runtimes':receipts},indent=2)+'\n')
