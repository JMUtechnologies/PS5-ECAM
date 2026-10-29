#!/usr/bin/env python3
"""Build the pinned ECAM shelf runtimes using their donor bootstrap (AI-assisted)."""
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
kind = sys.argv[1]
if kind not in ('n64', 'nes', 'gbc', 'gba'): raise SystemExit('Unknown shelf runtime')
def run(*args, cwd=ROOT, env=None):
    subprocess.run(args, cwd=cwd, env=env, check=True)
def pinned_clone(path, repo, commit):
    if not (path / '.git').is_dir():
        path.mkdir(parents=True, exist_ok=True)
        run('git', 'init', '-q', str(path))
        run('git', '-C', str(path), 'remote', 'add', 'origin', 'https://github.com/'+repo+'.git')
        run('git', '-C', str(path), 'fetch', '--depth=1', 'origin', commit)
        run('git', '-C', str(path), 'checkout', '--detach', commit)
    actual = subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != commit: raise SystemExit('Dependency pin mismatch: '+str(path))

run('python3', 'tools/build-retro.py', '--prepare-only', kind)
tree = ROOT / 'build/retro-source' / kind
environment = dict(os.environ, GITHUB_WORKSPACE=str(tree), RUNNER_TEMP='/tmp/ecam-shelf')
Path(environment['RUNNER_TEMP']).mkdir(exist_ok=True)
# This step is copied verbatim from the pinned PS5GBCX workflow; every asset
# carries the original Porpoise Git blob ID, checked by the donor commands.
assets = json.loads((ROOT / 'tools/docker/porpoise-assets-step.json').read_text())
subprocess.run(['bash', '-euo', 'pipefail', '-c', assets['run']], cwd=tree, env=environment, check=True)

base = tree / ('build/deps' if kind == 'nes' else '.cache/ps5-build')
# Dependency revisions are recorded in PS5NESX's checked-in SOURCES.json.
pinned_clone(base/'PS5_Vulkan', 'mihawk-99/PS5_Vulkan', 'f3cbf875f89978deae9f855b9be2610cec2818ae')
pinned_clone(base/'PS5_PayloadSDK', 'mihawk-99/PS5_PayloadSDK', '95c08f27386fc698f6bbe21dde3030140a41d10b')
pinned_clone(base/'ps5-opengl-sdk-0.3.0', 'mihawk-99/ps5-opengl', '6cb291abea32281571c49705735046425cf000fd')
run('python3', 'tools/docker/seed-zlib.py', str(base/'PS5_Vulkan'))
environment.update(PS5_VULKAN_DIR=str(base/'PS5_Vulkan'),
                   PS5_PAYLOAD_SDK=str(base/'PS5_Vulkan/.deps/native/ps5-payload-sdk'))
if kind == 'nes':
    run('bash', 'scripts/bootstrap_native.sh', cwd=tree, env=environment)
    deps = tree/'deps'
    deps.mkdir(exist_ok=True)
    (deps/'PS5_Vulkan').symlink_to(base/'PS5_Vulkan', target_is_directory=True)
    pinned_clone(deps/'libnfs', 'sahlberg/libnfs', '4379837536d9eac537810dc7b13071136049b22a')
    run('bash', 'ps5/tools/build-libnfs.sh', str(deps/'libnfs'), str(deps/'libnfs-output'),
        str(base/'PS5_Vulkan/.deps/native/ps5-payload-sdk/toolchain/prospero.cmake'), cwd=tree, env=environment)
    environment['PACBREW_ROOT'] = str(deps/'filesystem-helper')
    run('bash', 'ps5/tools/setup-pacbrew-dependencies.sh', '--resolve', 'libcurl', cwd=tree, env=environment)
    run('python3', 'scripts/build_generated_psbc.py', cwd=tree, env=environment)
    run('python3', 'scripts/build_agc_imports.py', cwd=tree, env=environment)
    environment.update(PS5NES_DEPS=str(deps), LIBNFS_PREFIX=str(deps/'libnfs-output/install'),
                       LIBZ_PREFIX=str(deps/'filesystem-helper/.deps/pacbrew/v0.40.2/sysroot/user/homebrew'),
                       PS5NES_PSBC_GENERATED=str(tree/'build/ps5-core/generated-psbc.ps5.a'),
                       PS5NES_AGC_STUB_DIR=str(tree/'build/ps5-core/agc-imports'))
else:
    environment['PS5'+kind.upper()+'_DEPS'] = str(base)
    run('bash', kind+'/tools/bootstrap_ci.sh', cwd=tree, env=environment)
    environment.update(LIBNFS_PREFIX=str(base/'libnfs-install'),
                       LIBZ_PREFIX=str(tree/kind/'.deps/pacbrew/v0.40.2/sysroot/user/homebrew'))
run('python3', 'tools/build-retro.py', '--reuse-prepared', kind, env=environment)
target = ROOT/'build/retro'/kind
if not (target/'eboot.bin').is_file() or not (target/'ECAM-SOURCE.json').is_file():
    raise SystemExit('Runtime staging incomplete')
print('Native build staged:', kind, '; PS5 hardware qualification still required')
