# Wiki pflegen

**Sprache / Language:** [Deutsch](Wiki-pflegen) | [English](Wiki-Maintenance)

Die bearbeitbaren Wiki-Seiten liegen im Hauptprojekt unter `docs/wiki`. Das veröffentlichte GitHub-Wiki besitzt ein eigenes Git-Repository. Die Pflege im Hauptprojekt ermöglicht eine gemeinsame Prüfung mit Firmware und Host.

## Änderungen vorbereiten

Jede Inhaltsseite besitzt eine deutsche und eine englische Fassung. Die Zuordnung steht in `docs/wiki-tools/languages.json`. Auf beiden Seiten führt „Deutsch | English“ zur jeweiligen Entsprechung. Die Auswahl erfolgt per Link; es gibt keine automatische Browser-Spracherkennung oder gespeicherte Spracheinstellung. Die gemeinsame Sidebar enthält beide Inhaltsverzeichnisse.

Änderungen in beiden Sprachen nachführen. Neue Seiten paarweise anlegen, die Zuordnung ergänzen und beide Fassungen in der Sidebar verlinken. Englische Inhaltslinks sollen auf englische Seiten zeigen; Befehle bleiben in beiden Fassungen identisch. Für Platinenbilder gibt es eigene deutsche und englische Beschriftungen.

1. Die betroffene Seite in `docs/wiki` aktualisieren.
2. Beispiele am aktuellen CLI-Parser und technische Angaben an den zuständigen Quellen prüfen.
3. Neue Seiten auf Home und in `_Sidebar.md` verlinken.
4. Messungen mit Datum, Aufbau und konkretem Ergebnis auf der Statusseite ergänzen.
5. Lokale Prüfung und Paketierung ausführen.

```console
python docs/wiki-tools/wiki.py check
python docs/wiki-tools/wiki.py package
```

Die Prüfung kontrolliert interne Seitenlinks, Navigation, lokale Ziele der Repository-Verweise, Codezäune und die Syntax der eingebetteten `ufi`-Beispiele. Sie bestätigt keine Hardwarefunktion und keine Erreichbarkeit jedes externen Links.

## Vor Veröffentlichung

Die vollständige USB-Protokollreferenz im Wiki wird aus `docs/USB_Protokoll.md` erzeugt. Dort korrigieren, anschließend `python docs/wiki-tools/wiki.py sync-reference` ausführen. `check` erkennt eine veraltete Kopie. Der englische Entwicklereinstieg ist vollständig englisch; die englische Referenzseite ist ein Wegweiser zur deutschen Gesamtspezifikation.

Die referenzierten Quelldateien müssen im GitHub-Hauptrepository verfügbar sein. Änderungen, die nur lokal vorliegen, zuerst mit dem vorgesehenen Entwicklungsstand abstimmen. Das Quellmanifest unter `docs/wiki-tools/sources.json` hält die beim Abgleich verwendeten Quellen fest.

```console
python docs/wiki-tools/wiki.py check --sources
```

Bei Änderungen der dokumentierten Quellen die betroffenen Aussagen erneut prüfen und das Manifest anschließend bewusst aktualisieren. Veröffentlichungsanleitung und Befehle stehen in [docs/wiki-tools/README.md](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/wiki-tools/README.md).

## GitHub Wiki initialisieren

Wenn das Wiki aktiviert ist, aber sein Git-Repository noch nicht existiert, einmal im GitHub-Webeditor eine erste Seite speichern. Danach ist die separate Clone-URL vorgesehen:

```console
git clone https://github.com/Axel051171/UniversalFluxInterface.wiki.git
```

Die lokale Erstellung dieser Dokumentation veröffentlicht noch keine Seite. Ein späterer Push erfolgt ausdrücklich in das Wiki-Repository. Der vorbereitete Upload übernimmt nur die Dateien dieses Wiki-Pakets, entfernt keine anderen Seiten und verwendet keinen Force-Push.

Quelle: [GitHub-Dokumentation zur lokalen Wiki-Pflege](https://docs.github.com/en/communities/documenting-your-project-with-wikis/adding-or-editing-wiki-pages).
