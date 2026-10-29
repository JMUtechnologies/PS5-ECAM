#!/usr/bin/env python3
"""Restore deleted upstream inputs from exact pinned copies (AI-assisted)."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
EDEN = ROOT / '.deps/ProsperoEdenX'
items = json.loads((EDEN / 'tools/deps.json').read_text())['items']
ui = next(i for i in items if i['name'] == 'ui-kit')
verified = []
for entry in ui['files']:
    source = ROOT / 'tools/docker/recovery/ui' / entry['name']
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != entry['sha256']:
        raise SystemExit('Recovered UI checksum mismatch: ' + entry['name'])
    target = EDEN / ui['dest'] / entry['name']
    if target.exists() and target.read_bytes() != data:
        raise SystemExit('Existing UI file differs: ' + str(target))
    verified.append((target, data))
for target, data in verified:
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)

helper = next(i for i in items if i['name'] == 'lapy-owned-helper')
target = EDEN / helper['path']
if not target.exists():
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['git', 'clone', '--no-checkout',
                    'https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon.git', str(target)], check=True)
    subprocess.run(['git', '-C', str(target), 'checkout', '--detach', helper['commit']], check=True)
actual = subprocess.check_output(['git', '-C', str(target), 'rev-parse', 'HEAD'], text=True).strip()
if actual != helper['commit']:
    raise SystemExit('Daemon recovery differs from pinned commit')

archive = ROOT / '.deps/archives/ps5-opengl-sdk-1.0.2.tar.gz'
expected = '30d8e51dd062739b629346102be5932728ba714bc50b1bdcdf08306c8843d9a4'
archive.parent.mkdir(parents=True, exist_ok=True)
if not archive.exists():
    partial = archive.with_suffix('.download')
    with urllib.request.urlopen('https://github.com/darkxex/ps5-opengl/releases/download/v1.0.2/ps5-opengl-sdk-1.0.2.tar.gz', timeout=180) as source, partial.open('wb') as output:
        shutil.copyfileobj(source, output)
    if hashlib.file_digest(partial.open('rb'), 'sha256').hexdigest() != expected:
        raise SystemExit('SDK archive checksum mismatch')
    partial.rename(archive)
with archive.open('rb') as source:
    if hashlib.file_digest(source, 'sha256').hexdigest() != expected:
        raise SystemExit('SDK archive checksum mismatch')
print('Recovered UI hashes, exact daemon commit, and SDK archive verified')
