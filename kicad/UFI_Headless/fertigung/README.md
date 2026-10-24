# Fertigungsdaten

Leer bis zur Fertigungsfreigabe (Stufe C in `../PLAN.md`, Skill `pcb-fertigungsfreigabe`).

Die Daten vom 7.10.2026 (Gerber, BOM, CPL, Upload-ZIP der v0.x) wurden am 24.10.2026 entfernt:
sie passten nicht mehr zur Platine v1.0 und hätten versehentlich bestellt werden können
(Befund 3 des Reviews vom 24.10.). Historie: `git log -- kicad/UFI_Headless/fertigung`.

Stufe C erzeugt hier neu: Gerber + Excellon (`UFI_Headless_gerber.zip`), `UFI_Headless-bom.csv`,
`UFI_Headless-cpl-jlc.csv` (`scripts/jlc_cpl.py`), `JLC_Rotation_Check.md`.
