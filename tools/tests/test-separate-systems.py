# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
spec = importlib.util.spec_from_file_location('separate', Path(__file__).parents[1] / 'separate-systems.py')
separate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(separate)

class MigrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name).resolve()
        self.shared = self.base / 'console/ps5cemu'
        content = {
            'games/game.wua': 'Wii U game', 'mlc01/usr/title/update/data': 'Wii U update',
            'mlc01/usr/title/dlc/data': 'Wii U DLC', 'mlc01/usr/save/game/data': 'Wii U save',
            'keys.txt': 'Wii U keys', 'settings.xml': '<settings/>',
            'azahar/games/game.3ds': '3DS game', 'azahar/sdmc/title/update/data': '3DS update',
            'azahar/nand/system/data': '3DS system', 'azahar/sysdata/aes_keys.txt': '3DS keys',
            'melonds/games/game.nds': 'DS game', 'melonds/bios/firmware.bin': 'DS firmware',
            'melonds/bios/bios7.bin': 'DS BIOS', 'melonds/saves/game.sav': 'DS save',
            'eden/roms/game.nsp': 'Switch game', 'eden/updates/dlc.nsp': 'Switch DLC',
            'eden/keys/prod.keys': 'Switch keys', 'eden/firmware/system.nca': 'Switch firmware',
            'eden/user/save/data': 'Switch save', 'ps5cemu.json': '{}',
        }
        for path, value in content.items():
            file = self.shared / path; file.parent.mkdir(parents=True, exist_ok=True); file.write_text(value)
        self.before = separate.inventory(self.shared)
        self.backup = self.base / 'backups/full'
    def run_migration(self):
        return separate.migrate(self.shared, self.backup, True)
    def test_preview_is_read_only(self):
        separate.migrate(self.shared)
        self.assertEqual(separate.inventory(self.shared), self.before)
        self.assertFalse(self.backup.exists())
        self.assertFalse((self.shared.parent / 'cemu').exists())
    def test_all_content_copied_and_originals_retained(self):
        targets = self.run_migration()
        self.assertEqual(separate.inventory(self.shared), self.before)
        self.assertEqual(separate.inventory(self.backup / 'launcher-root'), self.before)
        self.assertEqual(json.loads((self.backup / 'manifest.json').read_text())['launcher-root'], self.before)
        samples = {'cemu': 'mlc01/usr/title/dlc/data', 'azahar': 'sysdata/aes_keys.txt',
                   'melonds': 'bios/firmware.bin', 'eden': 'firmware/system.nca'}
        for system, sample in samples.items():
            self.assertEqual(targets[system].parent, self.shared.parent)
            self.assertTrue((targets[system] / sample).is_file())
            self.assertEqual((targets[system] / separate.MARKER).read_text(), separate.VERSION)
            for folder in separate.FOLDERS[system]: self.assertTrue((targets[system] / folder).is_dir())
        self.assertFalse((targets['cemu'] / 'azahar').exists())
        self.assertFalse((targets['melonds'] / 'dlc').exists())
    def test_stale_switch_launch_protocol_is_backed_up_but_not_replayed(self):
        for name in separate.TRANSIENT_EDEN:
            (self.shared / 'eden' / name).write_text('old request')
        targets = self.run_migration()
        for name in separate.TRANSIENT_EDEN:
            self.assertFalse((targets['eden'] / name).exists())
            self.assertEqual((self.shared / 'eden' / name).read_text(), 'old request')
            self.assertEqual((self.backup / 'launcher-root/eden' / name).read_text(), 'old request')
    def test_existing_destination_refused(self):
        (self.shared.parent / 'azahar').mkdir()
        (self.shared.parent / 'azahar/settings').write_text('existing data')
        with self.assertRaises(ValueError): self.run_migration()
        self.assertFalse(self.backup.exists())
        self.assertEqual(separate.inventory(self.shared), self.before)
    def test_backup_required_and_must_be_independent(self):
        with self.assertRaises(ValueError): separate.migrate(self.shared, apply=True)
        with self.assertRaises(ValueError): separate.migrate(self.shared, self.shared / 'backup', True)
        self.assertEqual(separate.inventory(self.shared), self.before)
    def test_backup_alias_cannot_overlap_the_original(self):
        alias = self.base / 'alias'
        alias.symlink_to(self.shared, target_is_directory=True)
        with self.assertRaises(ValueError): separate.migrate(self.shared, alias / 'backup', True)
        self.assertFalse((self.shared / 'backup').exists())
    def test_corrupt_backup_cannot_activate_any_system(self):
        real = separate.shutil.copytree
        def corrupt(source, destination, *args, **kwargs):
            result = real(source, destination, *args, **kwargs)
            if Path(destination) == self.backup / 'launcher-root':
                (Path(destination) / 'keys.txt').write_text('corrupted')
            return result
        with patch.object(separate.shutil, 'copytree', side_effect=corrupt):
            with self.assertRaises(ValueError): self.run_migration()
        self.assertFalse((self.shared.parent / 'cemu').exists())
        self.assertEqual(separate.inventory(self.shared), self.before)
    def test_symlinks_refused_without_following_them(self):
        (self.shared / 'games/link').symlink_to(self.base / 'missing')
        with self.assertRaises(ValueError): self.run_migration()
        self.assertFalse(self.backup.exists())
    def test_partial_copy_failure_does_not_activate(self):
        with patch.object(separate, 'copy_entry', side_effect=OSError('disk full')):
            with self.assertRaises(OSError): self.run_migration()
        self.assertFalse(any((self.shared.parent / name / separate.MARKER).exists() for name in separate.FOLDERS))
        self.assertEqual(separate.inventory(self.shared), self.before)
    def test_empty_startup_folders_can_be_reused(self):
        for name, folders in separate.FOLDERS.items():
            for folder in folders:
                (self.shared.parent / name / folder).mkdir(parents=True, exist_ok=True)
        targets = self.run_migration()
        self.assertEqual((targets['cemu'] / 'games/game.wua').read_text(), 'Wii U game')
        self.assertTrue(all((target / separate.MARKER).is_file() for target in targets.values()))
    def test_external_eden_is_backed_up_separately(self):
        eden = self.base / 'other-data/ps5cemu/eden'
        eden.mkdir(parents=True); (eden / 'keys').mkdir(); (eden / 'keys/prod.keys').write_text('external keys')
        before = separate.inventory(eden)
        targets = separate.migrate(self.shared, self.backup, True, eden)
        self.assertEqual(separate.inventory(eden), before)
        self.assertEqual(separate.inventory(self.backup / 'eden-external'), before)
        self.assertEqual((targets['eden'] / 'keys/prod.keys').read_text(), 'external keys')
if __name__ == '__main__': unittest.main()
