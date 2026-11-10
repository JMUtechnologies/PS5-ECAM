#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The translation gate must reject incomplete or unsafe tables, in isolation."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('translation_tool', Path(__file__).resolve().parents[1] / 'lang.py')
lang = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lang)


class TranslationGate(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.old = lang.LANG, lang.TEMPLATE
        lang.LANG = Path(self.directory.name)
        lang.TEMPLATE = lang.LANG / 'template.pot'

    def tearDown(self):
        lang.LANG, lang.TEMPLATE = self.old
        self.directory.cleanup()

    def result(self, source, forms, plural=None, fuzzy=False, code='cs'):
        entry = lang.Entry(None, source, plural)
        lang.write(lang.TEMPLATE, [entry])
        entry.forms, entry.fuzzy = forms, fuzzy
        lang.write(lang.LANG / (code + '.po'), [entry], code)
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(SystemExit) as exit:
            lang.check([code])
        return exit.exception.code

    def test_complete_translation(self):
        self.assertEqual(self.result('Open {0}', ['Otevřít {0}']), 0)

    def test_missing_translation(self):
        self.assertEqual(self.result('Open', ['']), 1)

    def test_missing_table(self):
        lang.write(lang.TEMPLATE, [lang.Entry(None, 'Open')])
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(SystemExit) as exit:
            lang.check(['cs'])
        self.assertEqual(exit.exception.code, 1)

    def test_missing_placeholder(self):
        self.assertEqual(self.result('Open {0}', ['Otevřít']), 1)

    def test_changed_format_specifier(self):
        self.assertEqual(self.result('{0:02d}', ['{0}']), 1)

    def test_preserved_relative_directory(self):
        self.assertEqual(self.result('Kept in saves/before.', ['Uloženo v saves/before.']), 0)

    def test_translated_relative_directory(self):
        self.assertEqual(self.result('Kept in saves/before.', ['Uloženo v uložení/před.']), 1)

    def test_translated_game_directory(self):
        self.assertEqual(self.result('Use games/gba/port1.', ['Použijte hry/gba/port1.']), 1)

    def test_directory_suffix_is_not_the_same_path(self):
        self.assertEqual(self.result('Use games/gba/port1.', ['Použijte games/gba/port10.']), 1)

    def test_slash_in_words_is_not_a_directory(self):
        self.assertEqual(self.result('On/Off', ['Zapnuto/Vypnuto']), 0)

    def test_missing_plural_form(self):
        self.assertEqual(self.result('{0} save', ['{0} pozice', '{0} pozice', ''], '{0} saves'), 1)

    def test_complete_plural(self):
        self.assertEqual(self.result('{0} save', ['{0} pozice', '{0} pozice', '{0} pozic'], '{0} saves'), 0)

    def test_changed_line_break(self):
        self.assertEqual(self.result('Open\n{0}', ['Otevřít {0}']), 1)

    def test_fuzzy_translation(self):
        self.assertEqual(self.result('Open', ['Otevřít'], fuzzy=True), 1)

    def test_non_normalized_text(self):
        self.assertEqual(self.result('Coffee', ['Cafe\u0301']), 1)

    def test_existing_partial_locale_policy(self):
        self.assertEqual(self.result('Open', [''], code='en-US'), 0)


if __name__ == '__main__':
    unittest.main()
