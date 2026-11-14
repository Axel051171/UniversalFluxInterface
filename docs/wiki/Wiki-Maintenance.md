# Wiki maintenance

**Sprache / Language:** [Deutsch](Wiki-pflegen) | [English](Wiki-Maintenance)

Editable wiki pages live in the main project under `docs/wiki`. The published GitHub wiki has its own Git repository. Maintaining sources in the main project allows documentation to be reviewed alongside firmware and host changes.

## Preparing changes

Every content page has German and English versions, paired in `docs/wiki-tools/languages.json`. Each page's “Deutsch | English” links open the corresponding version. Selection is through links; there is no automatic browser-language detection or saved language preference. The shared sidebar contains both tables of contents.

Maintain both versions when changing content. Add new pages in pairs, update the mapping and link both versions from the sidebar. English content links should lead to English pages; commands remain identical in both versions. Board illustrations have separate German and English annotations.

1. Update the relevant page in `docs/wiki`.
2. Check examples against the current CLI parser and technical statements against their sources.
3. Link new pages from Home and `_Sidebar.md`.
4. Add measurements to the status page with date, setup and actual result.
5. Run local validation and packaging.

```console
python docs/wiki-tools/wiki.py check
python docs/wiki-tools/wiki.py package
```

Validation checks internal page links, navigation, local targets of repository references, code fences and the syntax of embedded `ufi` examples. It does not establish hardware functionality or the availability of every external link.

## Before publication

Referenced sources must be available in the GitHub main repository. Coordinate changes that exist only locally with the intended development revision first. The source manifest at `docs/wiki-tools/sources.json` records the sources used during review.

```console
python docs/wiki-tools/wiki.py check --sources
```

When documented sources change, review affected statements again and then deliberately update the manifest. Publication instructions and commands are in [docs/wiki-tools/README.md](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/wiki-tools/README.md).

## Initialize the GitHub wiki

If the wiki is enabled but its Git repository does not yet exist, save an initial page in GitHub's web editor. The separate clone URL can then be used:

```console
git clone https://github.com/Axel051171/UniversalFluxInterface.wiki.git
```

Creating this documentation locally does not publish any pages. A later push explicitly targets the wiki repository. The prepared upload copies only this package's files, preserves other pages and uses no force push.

Source: [GitHub documentation on local wiki maintenance](https://docs.github.com/en/communities/documenting-your-project-with-wikis/adding-or-editing-wiki-pages).
