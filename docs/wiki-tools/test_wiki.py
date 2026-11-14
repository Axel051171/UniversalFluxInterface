"""Offline checks of publication guards and wiki validation; never contact GitHub."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('wiki_tools', Path(__file__).with_name('wiki.py'))
wiki = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wiki)


class WikiChecks(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='ufi-wiki-test-')
        self.root = Path(self.temporary.name)
        self.pages = self.root/'pages'
        self.pages.mkdir()
        for name, content in {'Home.md': '[Anleitung](Anleitung)\n', '_Sidebar.md': '[Start](Home)\n',
                              '_Footer.md': '[Start](Home)\n',
                              'Anleitung.md': '# Anleitung\n\n```console\nufi info\n```\n'}.items():
            (self.pages/name).write_text(content, encoding='utf-8')
        pairs = {'Home': 'Home-EN', 'Anleitung': 'Guide'}
        for de, en in pairs.items():
            original = (self.pages/f'{de}.md').read_text(encoding='utf-8')
            for name in (de, en):
                body = original if name == de else original.replace('(Anleitung)', '(Guide)')
                body = body.removeprefix('# Anleitung\n').lstrip()
                selector = f'**Sprache / Language:** [Deutsch]({de}) | [English]({en})'
                (self.pages/f'{name}.md').write_text(f'# {name}\n\n{selector}\n\n{body}', encoding='utf-8')
        (self.pages/'_Sidebar.md').write_text('[Start](Home) [English](Home-EN) [Guide](Guide)\n', encoding='utf-8')
        self.languages = self.root/'languages.json'
        self.languages.write_text(json.dumps({'schema': 1, 'pages': pairs}), encoding='utf-8')
        self.manifest = self.root/'sources.json'
        self.manifest.write_text(json.dumps({'files': {}}), encoding='utf-8')
        self.patches = [patch.object(wiki, 'PAGES', self.pages), patch.object(wiki, 'SOURCES', self.manifest),
                        patch.object(wiki, 'LANGUAGES', self.languages)]
        for item in self.patches:
            item.start()

    def tearDown(self):
        for item in reversed(self.patches):
            item.stop()
        self.temporary.cleanup()

    def test_accepts_real_cli_syntax_without_opening_device(self):
        with contextlib.redirect_stdout(io.StringIO()):
            wiki.check()

    def test_rejects_missing_page(self):
        (self.pages/'Home.md').write_text('[Nicht vorhanden](Fehlt)\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'fehlende Wiki-Seite'):
            wiki.check()

    def test_rejects_unknown_cli_command(self):
        (self.pages/'Anleitung.md').write_text('```console\nufi kein-befehl\n```\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'kein-befehl'):
            wiki.check()

    def test_rejects_asset_outside_wiki(self):
        (self.pages/'Anleitung.md').write_text('[Asset](../sources.json)\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'ungültiges Asset'):
            wiki.check()

    def test_detects_changed_source(self):
        source = self.root/'source.txt'
        source.write_text('changed', encoding='utf-8')
        self.manifest.write_text(json.dumps({'files': {'source.txt': hashlib.sha256(b'old').hexdigest()}}))
        with patch.object(wiki, 'ROOT', self.root), patch.object(wiki, 'source_paths', return_value=[source]):
            with self.assertRaisesRegex(ValueError, 'Quellen geändert'):
                wiki.check_sources()

    def test_rejects_missing_translation(self):
        (self.pages/'Guide.md').unlink()
        with self.assertRaisesRegex(ValueError, 'Sprachfassung fehlt: Guide'):
            wiki.check()

    def test_rejects_wrong_language_switch(self):
        page = self.pages/'Guide.md'
        page.write_text(page.read_text(encoding='utf-8').replace('[Deutsch](Anleitung)', '[Deutsch](Home)'), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Sprachwahl fehlt oder zeigt auf falsche Seite'):
            wiki.check()

    def test_rejects_english_link_to_german_content(self):
        with (self.pages/'Guide.md').open('a', encoding='utf-8') as stream:
            stream.write('\n[Start](Home)\n')
        with self.assertRaisesRegex(ValueError, 'englischer Inhaltslink'):
            wiki.check()

    def test_rejects_unpaired_new_page(self):
        (self.pages/'New.md').write_text('# New\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Keine Sprachzuordnung: New'):
            wiki.check()

    def test_rejects_changed_command_in_translation(self):
        page = self.pages/'Guide.md'
        page.write_text(page.read_text(encoding='utf-8').replace('ufi info', 'ufi status'), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'CLI-Beispiele unterscheiden sich'):
            wiki.check()

    def test_publish_stops_before_clone_when_sources_dirty(self):
        with patch.object(wiki, 'check'), patch.object(wiki, 'git', return_value=' M source.txt'), \
                patch.object(wiki.subprocess, 'run') as execute:
            with self.assertRaisesRegex(ValueError, 'nicht vollständig committet'):
                wiki.publish()
            execute.assert_not_called()

    def test_publish_stops_before_clone_when_remote_main_differs(self):
        with patch.object(wiki, 'check'), patch.object(wiki, 'git', side_effect=['', 'local', 'remote\trefs/heads/main']), \
                patch.object(wiki.subprocess, 'run') as execute:
            with self.assertRaisesRegex(ValueError, 'unterscheiden sich'):
                wiki.publish()
            execute.assert_not_called()


if __name__ == '__main__':
    unittest.main()
