"""Check/package the UFI wiki; publish is an explicit, separate operation."""
from __future__ import annotations

import argparse
import ast
import contextlib
from datetime import date
import hashlib
import importlib
import io
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import quote, unquote, urlsplit
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PAGES = ROOT / 'docs/wiki'
SOURCES = Path(__file__).with_name('sources.json')
LANGUAGES = Path(__file__).with_name('languages.json')
PROTOCOL = ROOT / 'docs/USB_Protokoll.md'
REFERENCE = 'USB-Protokoll-Referenz'
REPO = 'https://github.com/Axel051171/UniversalFluxInterface'
REMOTE = REPO + '.wiki.git'
LINK = re.compile(r'!?\[[^\]\n]*\]\(([^\s)]+)(?:\s+"[^"]*")?\)')


def git(*args, cwd=ROOT):
    return subprocess.check_output(['git', *args], cwd=cwd, text=True, encoding='utf-8').strip()


def content_files():
    return sorted(p for p in PAGES.rglob('*') if p.is_file() and p.suffix.lower() in {'.md', '.png', '.jpg', '.jpeg', '.svg'})


def repo_target(url):
    parsed = urlsplit(url)
    prefix = '/Axel051171/UniversalFluxInterface/'
    if parsed.netloc != 'github.com' or not parsed.path.startswith(prefix):
        return None
    tail = unquote(parsed.path[len(prefix):])
    for kind in ('blob/main/', 'tree/main/'):
        if tail.startswith(kind):
            return ROOT / tail[len(kind):]
    return None


def build_cli_parser():
    # Execute only main()'s parser construction, stopping BEFORE parse_args and all
    # transport/file operations. All example checks are pure argparse validation.
    sys.path.insert(0, str(ROOT/'software/ufi_host'))
    cli = importlib.import_module('ufi_host.cli')
    tree = ast.parse(Path(cli.__file__).read_text(encoding='utf-8'))
    main = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'main')
    stop = next(i for i, n in enumerate(main.body)
                if isinstance(n, ast.Assign) and isinstance(n.value, ast.Call)
                and isinstance(n.value.func, ast.Attribute) and n.value.func.attr == 'parse_args')
    env = dict(vars(cli))
    exec(compile(ast.Module(body=main.body[:stop], type_ignores=[]), str(cli.__file__), 'exec'), env)
    return env['ap']


def source_paths():
    result = set()
    for page in PAGES.glob('*.md'):
        for link in LINK.findall(page.read_text(encoding='utf-8')):
            target = repo_target(link)
            if target and target.is_file() and not target.is_relative_to(PAGES):
                if not target.is_relative_to(Path(__file__).parent):
                    result.add(target)
    result.add(ROOT/'software/ufi_host/ufi_host/cli.py')
    result.add(ROOT/'firmware/include/ufi_firmware.h')
    return sorted(result)


def check_sources():
    if not SOURCES.exists():
        raise ValueError('Quellmanifest fehlt; nach Inhaltsprüfung record-sources ausführen.')
    manifest = json.loads(SOURCES.read_text(encoding='utf-8'))
    current = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths()}
    changed = sorted(k for k in current.keys() | manifest['files'].keys() if current.get(k) != manifest['files'].get(k))
    if changed:
        raise ValueError('Dokumentierte Quellen geändert; Inhalt erneut prüfen:\n'+'\n'.join(changed))
    print(f"Quellabgleich: {len(current)} Dateien unverändert zum Manifest.")


def render_reference():
    source = PROTOCOL.read_text(encoding='utf-8')
    _, body = source.split('\n', 1)
    # Relative links were written for docs/, not the separate wiki repository.
    def relocate(match):
        target = match[2]
        parsed = urlsplit(target)
        if not parsed.scheme and not parsed.netloc and parsed.path:
            path = (PROTOCOL.parent/unquote(parsed.path)).resolve()
            if not path.is_relative_to(ROOT.resolve()):
                raise ValueError(f'Protokoll-Link außerhalb des Repositories: {target}')
            if path.is_relative_to(PAGES.resolve()) and path.suffix == '.md':
                target = path.stem
            else:
                target = REPO+'/blob/main/'+quote(path.relative_to(ROOT.resolve()).as_posix())
            if parsed.fragment:
                target += '#'+parsed.fragment
        return match[1]+target+match[3]
    body = re.sub(r'(!?\[[^\]\n]*\]\()([^\s)]+)(\))', relocate, body)
    # GitHub heading slugs: keep Unicode letters, strip punctuation and count duplicates.
    toc, seen, fenced = [], {}, False
    for line in body.splitlines():
        if line.startswith('```'):
            fenced = not fenced
        match = None if fenced else re.match(r'^(#{2,4}) (.+)$', line)
        if not match or match[2] == 'Inhalt':
            continue
        label = match[2].replace('`', '')
        slug = re.sub(r'[^\w\- ]', '', label.lower()).replace(' ', '-')
        count = seen.get(slug, 0)
        seen[slug] = count + 1
        if count:
            slug += f'-{count}'
        toc.append('  '*(len(match[1])-2)+f'- [{label}](#{slug})')
    body = re.sub(r'(?ms)^## Inhalt\n.*?(?=^## )', '## Inhalt\n\n'+'\n'.join(toc)+'\n\n', body, count=1)
    header = ('# USB-Protokoll: vollständige Referenz\n\n'
              '**Sprache / Language:** [Deutsch](USB-Protokoll-Referenz) | [English](USB-Protocol-Reference)\n\n'
              '> Vollständige deutschsprachige Spezifikation, automatisch aus '
              f'[docs/USB_Protokoll.md]({REPO}/blob/main/docs/USB_Protokoll.md) erzeugt. '
              'Hier nicht separat bearbeiten. [Entwicklereinstieg](USB-Protokoll) · '
              '[English developer guide](USB-Protocol). Die englische Referenzseite ist eine Orientierung, '
              'keine vollständige Übersetzung dieser Detailtabellen.\n\n')
    return header+body.lstrip().rstrip()+'\n'


def sync_reference():
    (PAGES/f'{REFERENCE}.md').write_text(render_reference(), encoding='utf-8')
    print('Vollständige USB-Referenz mit Wiki-Links und Inhaltsverzeichnis aktualisiert.')


def check_languages(pages):
    manifest = json.loads(LANGUAGES.read_text(encoding='utf-8'))
    pairs = manifest.get('pages', {})
    if manifest.get('schema') != 1 or not pairs or not all(
            isinstance(de, str) and isinstance(en, str) for de, en in pairs.items()):
        raise ValueError('Ungültiges Sprachmanifest.')
    names = [*pairs, *pairs.values()]
    if len(set(names)) != len(names):
        raise ValueError('Sprachmanifest enthält doppelte Seitenzuordnungen.')
    errors = []
    content_pages = {name for name in pages if not name.startswith('_')}
    for name in sorted(set(names) - content_pages):
        errors.append(f'Sprachfassung fehlt: {name}')
    for name in sorted(content_pages - set(names)):
        errors.append(f'Keine Sprachzuordnung: {name}')
    for de, en in pairs.items():
        selector = f'**Sprache / Language:** [Deutsch]({de}) | [English]({en})'
        texts = {}
        for name in (de, en):
            if name not in pages:
                continue
            content = pages[name].read_text(encoding='utf-8')
            if content.count(selector) != 1 or content.splitlines()[2:3] != [selector]:
                errors.append(f'{name}: Sprachwahl fehlt oder zeigt auf falsche Seite')
            texts[name] = content.replace(selector, '')
        if en in texts:
            for link in LINK.findall(texts[en]):
                parsed = urlsplit(link)
                if not parsed.scheme and not parsed.netloc and unquote(parsed.path).removesuffix('.md') in pairs:
                    errors.append(f'{en}: englischer Inhaltslink führt zur deutschen Seite {link}')
        if de in texts and en in texts:
            def commands(content):
                return [line.strip() for block in re.findall(r'```[^\n]*\n(.*?)```', content, re.S)
                        for line in block.splitlines() if line.strip().startswith('ufi ')]
            if commands(texts[de]) != commands(texts[en]):
                errors.append(f'{de}/{en}: CLI-Beispiele unterscheiden sich zwischen den Sprachen')
    return errors


def check(sources=False):
    pages = {p.stem: p for p in PAGES.glob('*.md')}
    for required in ('Home', '_Sidebar', '_Footer'):
        if required not in pages:
            raise ValueError(f'Pflichtseite fehlt: {required}')
    parser = build_cli_parser()
    errors, examples, links = check_languages(pages), 0, 0
    if REFERENCE in pages and pages[REFERENCE].read_text(encoding='utf-8') != render_reference():
        errors.append('USB-Referenz veraltet/geändert: Quelle prüfen und sync-reference ausführen')
    navigation = pages['Home'].read_text(encoding='utf-8')+'\n'+pages['_Sidebar'].read_text(encoding='utf-8')
    nav_targets = {unquote(urlsplit(x).path).removesuffix('.md') for x in LINK.findall(navigation)}
    for name, page in sorted(pages.items()):
        if not name.startswith('_') and name != 'Home' and name not in nav_targets:
            errors.append(f'{page.name}: nicht in Home/Sidebar verlinkt')
        content = page.read_text(encoding='utf-8')
        if content.count('```') % 2:
            errors.append(f'{page.name}: nicht geschlossener Codeblock')
        for target in LINK.findall(content):
            links += 1
            external_target = repo_target(target)
            if external_target is not None:
                if not external_target.exists():
                    errors.append(f'{page.name}: fehlende Repository-Datei {target}')
                continue
            parsed = urlsplit(target)
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            local = unquote(parsed.path)
            if '/' in local or Path(local).suffix and not local.endswith('.md'):
                asset = (page.parent/local).resolve()
                if not asset.is_relative_to(PAGES.resolve()) or not asset.is_file():
                    errors.append(f'{page.name}: fehlendes/ungültiges Asset {target}')
            elif local.removesuffix('.md') not in pages:
                errors.append(f'{page.name}: fehlende Wiki-Seite {target}')
        for block in re.findall(r'```[^\n]*\n(.*?)```', content, re.S):
            for line in block.splitlines():
                if not line.strip().startswith('ufi '):
                    continue
                args = shlex.split(line, comments=True)[1:]
                examples += 1
                stderr = io.StringIO()
                try:
                    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(stderr):
                        parser.parse_args(args)
                except SystemExit as error:
                    if error.code:
                        errors.append(f'{page.name}: {line}\n{stderr.getvalue().strip()}')
    if errors:
        raise ValueError('\n'.join(errors))
    if sources:
        check_sources()
    print(f'Wiki OK: {len(pages)-2} Inhaltsseiten, Sidebar/Footer, {links} Links, {examples} CLI-Beispiele.')


def record_sources():
    manifest = {'schema': 1, 'source_revision': git('rev-parse', 'HEAD'),
                'reviewed_date': date.today().isoformat(),
                'files': {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths()}}
    SOURCES.write_text(json.dumps(manifest, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    print(f'Quellmanifest gespeichert: {len(manifest["files"])} Dateien.')


def package():
    check(sources=True)
    target = ROOT/'build/wiki/UniversalFluxInterface-wiki.zip'
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in content_files():
            archive.write(path, path.relative_to(PAGES).as_posix())
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    target.with_suffix('.zip.sha256').write_text(digest+'  '+target.name+'\n', encoding='utf-8')
    print(f'Paket: {target}\nSHA256: {digest}')


def publish():
    # No network writes occur in check/package/record-sources. Explicit publish
    # uses a fresh wiki clone, stages only owned files, and never force-pushes.
    check(sources=True)
    manifest = json.loads(SOURCES.read_text(encoding='utf-8'))
    paths = list(manifest['files'])
    if git('status', '--porcelain', '--', *paths):
        raise ValueError('Dokumentierte Quellen sind noch nicht vollständig committet. Vor Veröffentlichung abstimmen.')
    revision = git('rev-parse', 'HEAD')
    remote_main = git('ls-remote', REPO+'.git', 'refs/heads/main').split()
    if not remote_main or remote_main[0] != revision:
        raise ValueError('Lokaler HEAD und GitHub main unterscheiden sich. Quellstand zuerst abgleichen; kein Wiki-Push erfolgt.')
    # This read also verifies that the first Wiki page has been initialized.
    refs = git('ls-remote', REMOTE, 'HEAD')
    if not refs:
        raise ValueError('Wiki zuerst auf GitHub mit einer gespeicherten Home-Seite initialisieren.')
    # Resolve the author from the user's normal Git configuration, not an invented identity.
    author_name = git('var', 'GIT_AUTHOR_IDENT').rsplit(' <', 1)[0]
    author_email = git('var', 'GIT_AUTHOR_IDENT').rsplit('<', 1)[1].split('>', 1)[0]
    with tempfile.TemporaryDirectory(prefix='ufi-wiki-publish-') as temporary:
        checkout = Path(temporary)/'wiki'
        subprocess.run(['git', 'clone', REMOTE, str(checkout)], check=True)
        branch = git('branch', '--show-current', cwd=checkout)
        if not branch:
            raise ValueError('Wiki-Standardbranch nicht erkannt; kein Push erfolgt.')
        files = content_files()
        for path in files:
            target = checkout/path.relative_to(PAGES)
            if target.is_symlink():
                raise ValueError(f'Wiki-Ziel ist ein Symlink: {target.name}')
            if target.parent.resolve() != checkout.resolve() and not target.parent.resolve().is_relative_to(checkout.resolve()):
                raise ValueError('Wiki-Ziel liegt außerhalb des Checkouts.')
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
        subprocess.run(['git', 'add', '--', *[p.relative_to(PAGES).as_posix() for p in files]], cwd=checkout, check=True)
        if not git('diff', '--cached', '--name-only', cwd=checkout):
            print('Wiki entspricht bereits dem lokalen Inhalt.')
            return
        subprocess.run(['git', '-c', f'user.name={author_name}', '-c', f'user.email={author_email}',
                        'commit', '-m', 'Document UFI Headless v1.0 in German and English with annotated board views'], cwd=checkout, check=True)
        subprocess.run(['git', 'push', 'origin', f'HEAD:refs/heads/{branch}'], cwd=checkout, check=True)
        print(f'Veröffentlicht: {REPO}/wiki\nWiki-Commit: {git("rev-parse", "HEAD", cwd=checkout)}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('check', 'sync-reference', 'record-sources', 'package', 'publish'), nargs='?', default='check')
    parser.add_argument('--sources', action='store_true', help='check: auch Quellmanifest vergleichen')
    args = parser.parse_args()
    try:
        if args.action == 'check': check(args.sources)
        elif args.action == 'sync-reference': sync_reference()
        elif args.action == 'record-sources': record_sources()
        elif args.action == 'package': package()
        elif args.action == 'publish': publish()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'Fehler: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
