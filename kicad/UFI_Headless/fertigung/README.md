# Fertigungsdaten UFI Headless v1.0

Erzeugt am 25.10.2026 mit `../scripts/make_fab.sh` aus dem Stand nach Stufe B3 (6 Lagen). Freigabebericht:
[../docs/Freigabe_v1.0.md](../docs/Freigabe_v1.0.md), Bestellanleitung: [../docs/Bestellung_JLC.md](../docs/Bestellung_JLC.md).

| Datei | Inhalt |
|---|---|
| `UFI_Headless_gerber.zip` | 6 Kupferlagen, Maske, Paste, Siebdruck, Umriss, Excellon PTH/NPTH mit Bohrplänen, Gerber-Jobdatei – Upload bei JLC |
| `UFI_Headless-bom-jlc.csv` | BOM mit JLC-Spalten (Comment, Designator, Footprint, LCSC Part #), ohne DNP |
| `UFI_Headless-cpl-jlc.csv` | Bestückungsdaten mit JLC-Spalten und Rotationsoffsets (`../scripts/jlc_cpl.py`) |
| `UFI_Headless-bom.csv`, `UFI_Headless-pos.csv` | dieselben Daten in KiCad-Form |
| `JLC_Rotation_Check.md` | Pin-1-Checkliste für den JLC-Bestückungsviewer (87 Teile) |
| `Lagerpruefung_2026-10-25.md` | Lager, Basic/Extended und Preis je BOM-Zeile laut JLC-Teilesuche am 25.10.2026 |

Nicht im Repository (`.gitignore` hier): der Ordner `gerber/` (steckt vollständig im ZIP) und `UFI_Headless.step`
(17 MB, für die Gehäuseprüfung jederzeit mit `make_fab.sh` neu erzeugbar).

Nach jeder Änderung an Schaltplan oder Platine: `make_fab.sh` neu ausführen, DRC und ERC prüfen, diese Dateien
gemeinsam committen. Das Skript löscht den Ordner vorher; README, `.gitignore` und die Lagerprüfung danach wieder
herstellen (`git checkout -- fertigung/README.md fertigung/.gitignore`).
