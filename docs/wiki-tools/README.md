# UFI Wiki vorbereiten und veröffentlichen

Die Wiki-Quellen liegen in `docs/wiki`: Markdown-Seiten, `_Sidebar.md`, `_Footer.md` und lokale Bilder unter `images/`. Der Inhalt ist zur späteren Veröffentlichung vorbereitet. Die Erstellung führt keinen Push aus.

## Lokal prüfen

Aus dem Projektstamm, mit Python ab 3.10:

```console
python docs/wiki-tools/wiki.py check
python docs/wiki-tools/wiki.py check --sources
python docs/wiki-tools/wiki.py package
```

`check` kontrolliert Seitenlinks, Navigation, Asset-Dateien, Codezäune und die lokalen Ziele der Repository-Links. Alle eingebetteten `ufi`-Zeilen werden gegen den tatsächlichen CLI-Parser geprüft. Dafür wird ausschließlich der Parser-Aufbau aus `main()` ausgeführt, nicht die Befehlsausführung: kein Board wird geöffnet, kein Medium beschrieben. Die semantische Eignung von Parametern, externe URLs und die Darstellung auf GitHub erfordern eine separate Durchsicht.

`package` erzeugt `build/wiki/UniversalFluxInterface-wiki.zip` mit den Wiki-Dateien direkt auf der obersten Ebene sowie eine SHA256-Datei. Entwicklungswerkzeuge und Quellmanifest gehören nicht in die Wiki-Clone-Wurzel und sind nicht im ZIP enthalten.

## Deutsch und English

`languages.json` ordnet jede deutsche Inhaltsseite genau einer englischen zu. Die Sprachwahl steht unmittelbar nach dem Seitentitel und führt zur entsprechenden Anleitung. Die gemeinsame Sidebar hat ein deutsches und ein englisches Inhaltsverzeichnis. Sprache wird per Link gewählt; eine automatische Erkennung oder dauerhafte Browsereinstellung ist nicht vorgesehen.

Inhaltliche Änderungen in beiden Fassungen nachführen. Neue Seiten paarweise anlegen und die Zuordnung sowie Sidebar ergänzen. `check` erkennt fehlende Übersetzungsseiten, falsche Sprachschalter, englische Inhaltslinks auf deutsche Seiten und abweichende `ufi`-Beispiele. Eine semantisch vollständige Übersetzung prüft es nicht automatisch.

Die beiden zusätzlichen Dateien `*-annotated-en.png` enthalten englische Platinenbeschriftungen mit derselben Nummerierung wie die deutsche Legende. Originalbilder werden gemeinsam verwendet.

## USB-Protokoll für eigene Tools

`USB-Protokoll` und `USB-Protocol` bilden den zweisprachigen Entwicklereinstieg. `USB-Protokoll-Referenz` enthält die **vollständige deutsche** Spezifikation aus `docs/USB_Protokoll.md`; `USB-Protocol-Reference` ist deren englischer Wegweiser, keine vollständige Übersetzung der umfangreichen Detailtabellen. Das ist auf beiden Referenzseiten ausdrücklich angegeben.

Die Kopie hat ein erzeugtes, klickbares Inhaltsverzeichnis und für das Wiki angepasste Repository-Links. Nach einer Änderung der Quelle deren Inhalt prüfen und die Wiki-Kopie erneuern:

```console
python docs/wiki-tools/wiki.py sync-reference
python docs/wiki-tools/wiki.py check
python docs/wiki-tools/test_protocol_examples.py
python docs/wiki-tools/test_wiki.py
```

`check` lehnt eine veraltete oder separat bearbeitete Referenzkopie ab. `sync-reference` veröffentlicht nichts. `examples/ufi_v2_probe.py` ist ein kleiner nativer CDC-Client auf Basis des Python-Hosts: nur PING/INFO, keine Laufwerks- oder Medienbefehle; der CLI-Einstieg prüft den Port und setzt seinen Kanal zurück. Die Beispieltests prüfen die veröffentlichten Hex-Vektoren gegen den Referenzcodec und den Probe gegen das vorhandene FakeV2. Dabei wird kein USB-Gerät geöffnet.

## Quellabgleich

`sources.json` speichert SHA256 und Quellrevision für dokumentierte Repository-Dateien. Nach Änderungen die entsprechenden Wiki-Aussagen prüfen, dann das Manifest ausdrücklich neu erfassen:

```console
python docs/wiki-tools/wiki.py record-sources
python docs/wiki-tools/wiki.py check --sources
```

Ein neu erfasstes Manifest ist kein automatischer Inhaltsnachweis. Die aktuelle Version wurde während laufender Firmwareänderungen vorbereitet; vor Upload deren endgültigen Stand abgleichen.

## UnifiedFloppyTool als Begleitanwendung

Die Seiten `UnifiedFloppyTool` und `UnifiedFloppyTool-EN` dokumentieren die lokal untersuchte native UFI-Anbindung. `companion-sources.json` hält UFT-Revision, interne Version und SHA256 der gelesenen Dateien fest. Der lokale Ordnername 4.1.0 stimmt nicht mit `VERSION.txt` (4.1.6) überein. Der öffentliche Projektlink bestätigt nicht, dass dessen Downloadpakete dieselben Entwicklungsfunktionen enthalten.

`check --sources` prüft die UFI-Repository-Quellen aus `sources.json`; das UFT-Quellmanifest ist ein separater Prüfbezug und wird dabei nicht automatisch kontrolliert. Vor Veröffentlichung oder späteren UFT-Änderungen die beschriebenen GUI-Funktionen, Transportwege, Sprache und Testnachweise erneut mit diesem Stand abgleichen. Der fremde Arbeitsbaum wird von den Wiki-Werkzeugen nicht bearbeitet oder veröffentlicht.

## Platinenbilder

Die Originale `UFI_Headless-front.png` und `UFI_Headless-back.png` des Nutzers wurden bytegleich in den Wiki-Assetordner kopiert. Das zuerst genannte JPG wurde nach der Korrektur durch den Nutzer nicht als v1.0-Vorderseite verwendet.

Die Dateien `*-annotated.png` und `*-annotated-en.png` wurden mit dem eingebauten `imagegen`-Werkzeug erstellt und visuell geprüft. `image-prompts.json` und `image-prompts-en.json` enthalten die verwendeten deutschen bzw. englischen Prompts. Die Legende folgt den Referenzen im v1.0-KiCad-Layout; die Bilder dienen der Orientierung, die Originale bleiben für Detailvergleiche verfügbar.

## Später auf GitHub aufspielen

1. Die Inhalte durchsehen und den endgültigen Firmware-/Dokumentationsstand abstimmen.
2. Dokumentierte Quelldateien im Hauptrepository committen und nach GitHub `main` übertragen. Dieser Schritt gehört nicht zum Wiki-Publisher.
3. Wiki auf GitHub öffnen. Falls noch kein Wiki-Git-Repository existiert, einmal eine erste Seite namens `Home` speichern. Bei der Vorbereitung war `has_wiki=true`, aber `git ls-remote` lieferte `Repository not found`.
4. Git-Anmeldung und Git-Autor müssen eingerichtet sein.
5. Nach ausdrücklichem Auftrag zur Veröffentlichung ausführen:

```console
python docs/wiki-tools/wiki.py publish
```

Der Publisher prüft die Quellen und verlangt, dass lokaler HEAD und GitHub `main` übereinstimmen. Danach klont er **das separate Wiki** frisch, übernimmt nur die vorbereiteten Dateien, erstellt einen Commit auf dessen erkanntem Standardbranch und pusht ohne Force. Andere Seiten werden nicht gelöscht; namensgleiche Seiten wie `Home` und Sidebar werden durch die vorbereitete Fassung ersetzt. Bei zwischenzeitlichen Remote-Änderungen wird der Push regulär abgewiesen.

Die lokale Arbeitskopie und ihre Git-Indexdatei werden für den Wiki-Commit nicht verwendet. Das Skript enthält keinen Token und fragt keine Zugangsdaten ab; die normale Git-Anmeldung ist zuständig. Es veröffentlicht nicht automatisch zu einem späteren Zeitpunkt.

Ziel: [UniversalFluxInterface Wiki](https://github.com/Axel051171/UniversalFluxInterface/wiki).

Referenz: [GitHub Docs zur lokalen Wiki-Bearbeitung](https://docs.github.com/en/communities/documenting-your-project-with-wikis/adding-or-editing-wiki-pages).
