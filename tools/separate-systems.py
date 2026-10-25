#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Copy a legacy ECAM install to separate system folders after a verified full backup.

The default is a read-only plan. Close both programs before --apply. No source is changed,
no existing destination is overwritten, and a failed run never deletes its partial copies.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

MARKER = '.ecam-separated-v2'
VERSION = 'ecam-system-layout-v2\n'
TRANSIENT_EDEN = ('launch-request.txt', 'ecam-session.txt', 'ecam-result.txt')
CEMU_NAMES = ('settings.xml', 'controllerProfiles', 'mlc01', 'games', 'updates', 'dlc',
              'keys.txt', 'graphicPacks', 'reshade', 'cache', 'gameProfiles', 'log.txt')
FOLDERS = {
    'cemu': ('games', 'updates', 'dlc', 'mlc01'),
    'azahar': ('games', 'updates', 'dlc', 'sdmc', 'nand', 'sysdata', 'cheats', 'load'),
    'melonds': ('games', 'bios', 'saves', 'states', 'cheats'),
    'eden': ('roms', 'updates', 'keys', 'firmware', 'mods', 'user', 'config'),
}

def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as file:
        for block in iter(lambda: file.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()

def inventory(root, ignore_marker=False):
    """Names, empty directories, sizes and hashes; refuse links and special files."""
    result = {}
    if not root.exists():
        if root.is_symlink():
            raise ValueError(f'Symlink source is not supported: {root}')
        return result
    for path in [root, *sorted(root.rglob('*'))]:
        if path.is_symlink():
            raise ValueError(f'Symlink source is not supported: {path}')
        relative = path.relative_to(root).as_posix()
        if ignore_marker and path.name == MARKER:
            continue
        if path.is_dir():
            result[relative] = {'kind': 'directory'}
        elif path.is_file():
            result[relative] = {'kind': 'file', 'bytes': path.stat().st_size, 'sha256': digest(path)}
        else:
            raise ValueError(f'Special file is not supported: {path}')
    return result

def inside(path, directory):
    return path == directory or directory in path.parents

def copy_entry(source, target):
    if source.is_dir():
        target.mkdir(exist_ok=True)
        for entry in sorted(source.iterdir()):
            if entry.name != MARKER:
                copy_entry(entry, target / entry.name)
    else:
        with source.open('rb') as incoming, target.open('xb') as outgoing:
            shutil.copyfileobj(incoming, outgoing)
        shutil.copystat(source, target)
    original, copied = inventory(source, True), inventory(target, True)
    if any(copied.get(key) != value for key, value in original.items()) or any(value['kind'] == 'file' and key not in original for key, value in copied.items()):
        raise ValueError(f'Copy verification failed: {source} -> {target}')

def migrate(legacy, backup=None, apply=False, eden_legacy=None):
    # Resolve parent paths, but reject source symlinks rather than following them.
    legacy = Path(os.path.abspath(legacy))
    if legacy.is_symlink():
        raise ValueError(f'Symlink source is not supported: {legacy}')
    legacy = legacy.resolve(strict=True)
    eden_legacy = Path(os.path.abspath(eden_legacy)) if eden_legacy else legacy / 'eden'
    if eden_legacy.is_symlink():
        raise ValueError(f'Symlink source is not supported: {eden_legacy}')
    eden_legacy = eden_legacy.resolve(strict=False)
    targets = {name: legacy.parent / name for name in FOLDERS}
    targets['eden'] = eden_legacy.parent.parent / 'eden'
    sources = {'cemu': legacy, 'azahar': legacy / 'azahar',
               'melonds': legacy / 'melonds', 'eden': eden_legacy}
    direct = any((legacy / name).exists() for name in ('settings.xml', 'mlc01', 'keys.txt', 'controllerProfiles', 'graphicPacks', 'reshade'))
    if (legacy / 'cemu').exists():
        if direct:
            raise ValueError('Both root-level and nested Cemu state exist; compare them before migrating.')
        sources['cemu'] = legacy / 'cemu'
    if not legacy.is_dir() or legacy.is_symlink():
        raise ValueError(f'Legacy root must be an existing real directory: {legacy}')
    existing_targets = {}
    for target in targets.values():
        if target.exists() or target.is_symlink():
            state = inventory(target)
            if not target.is_dir() or any(value['kind'] != 'directory' for value in state.values()):
                raise ValueError(f'Destination contains data; nothing will be overwritten: {target}')
            existing_targets[target] = state
    print('Read-only plan:' if not apply else 'Backup and copy plan:')
    for name in FOLDERS:
        print(f'  {name}: {sources[name]} -> {targets[name]}')
    if not apply:
        print('No changes made. To apply, close the app and supply --apply --backup NEW_BACKUP_DIRECTORY.')
        return targets
    if backup is None:
        raise ValueError('--apply requires --backup to a new independent backup directory.')
    backup = Path(os.path.abspath(backup))
    if backup.is_symlink():
        raise ValueError(f'Symlink backup is not supported: {backup}')
    backup = backup.resolve(strict=False)
    protected = [legacy, eden_legacy, *targets.values()]
    if any(inside(backup, path) or inside(path, backup) for path in protected):
        raise ValueError('Backup must be independent of all source and destination trees.')
    if backup.exists() or backup.is_symlink():
        raise ValueError(f'Backup destination already exists: {backup}')
    # Preflight all source content, including every shared launcher file and every system.
    before = inventory(legacy)
    external_eden = not inside(eden_legacy, legacy)
    eden_before = inventory(eden_legacy) if external_eden else None
    backup.mkdir(parents=True, exist_ok=False)
    shutil.copytree(legacy, backup / 'launcher-root')
    if inventory(backup / 'launcher-root') != before or inventory(legacy) != before:
        raise ValueError('Full backup verification failed, or the source changed. Originals remain untouched.')
    if external_eden and eden_legacy.exists():
        shutil.copytree(eden_legacy, backup / 'eden-external')
        if inventory(backup / 'eden-external') != eden_before or inventory(eden_legacy) != eden_before:
            raise ValueError('External Eden backup verification failed.')
    manifest = {'version': 2, 'source': str(legacy), 'launcher-root': before,
                'eden-external': eden_before, 'destinations': {key: str(value) for key, value in targets.items()}}
    with (backup / 'manifest.json').open('x') as file:
        json.dump(manifest, file, indent=2, sort_keys=True)
    print('Full backup verified by paths, sizes and SHA-256 hashes.')
    # Claim each destination exclusively. Interrupted copies have no activation marker.
    for name, target in targets.items():
        if target in existing_targets:
            if inventory(target) != existing_targets[target]:
                raise ValueError(f'Destination changed while backing up: {target}')
        else:
            target.mkdir(exist_ok=False)
        source = sources[name]
        entries = [source / entry for entry in CEMU_NAMES] if name == 'cemu' else sorted(source.iterdir()) if source.exists() else []
        for entry in entries:
            if name == "eden" and any(entry.name == transient or entry.name == transient + ".new" for transient in TRANSIENT_EDEN):
                continue
            if entry.name == MARKER or not entry.exists():
                continue
            copy_entry(entry, target / entry.name)
        for directory in FOLDERS[name]:
            (target / directory).mkdir(parents=True, exist_ok=True)
        if name == 'cemu' and not (target / 'keys.txt').exists():
            (target / 'keys.txt').write_text('# Add your Wii U disc keys here, one per line.\n')
    if inventory(legacy) != before or (external_eden and inventory(eden_legacy) != eden_before):
        raise ValueError('Source changed while copying. No layout has been activated; close the app before retrying.')
    for target in targets.values():
        with (target / MARKER).open('x') as file:
            file.write(VERSION)
            file.flush()
            os.fsync(file.fileno())
    print('All four separate folders activated. Originals and verified full backup retained; nothing deleted.')
    return targets

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--legacy-root', type=Path, default=Path('/data/ps5cemu'))
    parser.add_argument('--eden-legacy', type=Path, help='Old Eden directory when it is outside the launcher root, e.g. with launcher data on USB')
    parser.add_argument('--backup', type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    try:
        migrate(args.legacy_root, args.backup, args.apply, args.eden_legacy)
    except (OSError, ValueError) as error:
        parser.exit(1, f'Stopped: {error}\nOriginal data was not moved or deleted. Keep any partial backup/copies for inspection.\n')
if __name__ == '__main__':
    main()
