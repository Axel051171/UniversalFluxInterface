# C64 PSU Guard v0.2 – Stufenplan (Review des Owners vom 2026-10-24)

| Stufe | Ziel | Abschluss | Stand |
|---|---|---|---|
| G1 Schaltplan | J1 und J3 als JST XH B4B-XH-A (C144395), D6 SMAJ13A unidirektional (C110519), R20/R21 100k FLT-Pull-ups (C25803), Texte J20 statt J9 | ERC 0/0 | erledigt 2026-10-26 |
| G2 Layout | XH-Footprints platziert, Netzklasse Load (1,0 mm) mit `/V5_P` `/V12_P`, Lastpfade Q → TVS → C → U(IN) 1,0 mm vorgeroutet, Rest Freerouting, `routing_rules.json`, Siebdruck v0.2 | DRC 0/0/0, check_routing 0 FEHLER | erledigt 2026-10-26 |
| G3 Fertigung + Doku | Gerber-ZIP, BOM, CPL, Rotationsliste, erc.rpt/drc.rpt in `fertigung/`, README (Anschlüsse, Kabel, Inbetriebnahme, UFI J20), Lager geprüft | alle Exporte nach der letzten Boardänderung | |

Offen nach v0.2: nichts gebaut, alle Werte nur berechnet bzw. aus Datenblättern.
