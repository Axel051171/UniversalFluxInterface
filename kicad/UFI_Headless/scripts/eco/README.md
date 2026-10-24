# ECO-Handwerkzeuge (KiCad-Python, `"C:/Program Files/KiCad/10.0/bin/python.exe"`)

Für das, was Freerouting nach einer ECO offen lässt. Alle arbeiten direkt auf dem Board; vorher eine Kopie ziehen.
Netznamen mit `/` (z. B. `/MCU_Core/SD_D3`) unter Git Bash mit `MSYS_NO_PATHCONV=1` und Windows-Pfaden aufrufen.

| Skript | Zweck |
|---|---|
| `net_clusters.py board NET` | zusammenhängende Pad-Gruppen eines Netzes (Zonen werden vorher gefüllt) |
| `occupancy.py board` | ASCII-Belegungskarte 1 mm (Courtyards, Leiterbahndichte) – freien Platz finden |
| `free_map.py board NET W LAYER x0 y0 x1 y1 [step]` | wo eine Bahn der Breite W verlaufen darf (`.` frei, `v` Via passt, `+` eigenes Kupfer) |
| `maze_route.py board NET W x0 y0 x1 y1 sx,sy,L tx,ty,L [step] [via_cost]` | A*-Router F/In2/B für eine Verbindung, keine Vias in Pads; gibt eine Punktliste für `route_try.py` aus |
| `route_try.py board NET W check\|apply x,y,L ...` | Polylinie prüfen/einsetzen, Via bei Lagenwechsel (`L` = F, I2, B) |
| `via_scan.py board NET x0 y0 x1 y1` | freie Via-Punkte auf den F.Cu-Bahnen eines Netzes |
| `who_blocks.py board NET LAYER x y r` | welches Fremdkupfer einen Punkt blockiert |
| `copy_tracks.py old new NET x0 y0 x1 y1 check\|apply` | Bahnen eines Netzes aus einem früheren Board übernehmen (z. B. Versorgung nach einer ECO) |
| `drop_seg.py`, `drop_box.py`, `renet_box.py`, `move_fp.py` | Segmente/Bereiche löschen, Kupfer umnetzen, Bauteil versetzen und darunter freiräumen |
| `remove_part.py board REF[,REF] [NET ...]` | im Schaltplan entfernte Bauteile vom Board nehmen und Netze komplett aufreißen (vor `make_pcb.sh eco`) |
| `set_value.py board REF VALUE [FELD=WERT ...]` | geänderten Bauteilwert ins Layout übernehmen (Schaltplan-Parität) |
| `hide_ref.py board REF [REF ...]` | Referenz kleiner Bauteile im Bestückungsdruck ausblenden (Platzmangel), F.Fab behält sie |
| `extend_outline.py board Y_FROM DY [SRC:NEU:x:y ...]` | Platine nach unten verlängern: Umriss und flächige Ebenen unterhalb Y_FROM um DY schieben, Befestigungslöcher kopieren (v0.7: `80 12 H3:H5:3.5:93.5 H4:H6:106.5:93.5`) |
| `probe_region.py board x0 y0 x1 y1` | Courtyards und Kupfer in einem Fenster auflisten |
| `check_via_in_pad.py board` | Vias, die ein SMD-Pad berühren (muss 0 sein) |
| `renet_copper.py board` | nach einem Sync, der Netznamen abgeschnitten hat (`BUCK_FB` statt `/Power/BUCK_FB`): Bahnen/Zonen auf das Netz der Pads zurückbenennen |
| `cleanup_copper.py board [apply]` | Kupfer-Cluster löschen, die keine zwei Pads mehr verbinden (Reste versetzter oder entfernter Teile); GND/+3V3-Vias bleiben |
| `trim_dangling.py board [apply]` | eine Runde hängende Bahnenden und einseitige Vias entfernen; in der Shell wiederholen, bis `removed 0` (pcbnew verträgt kein zweites LoadBoard nach Remove) |
| `place_v10.py board` | Stufe B2 der v1.0: feste Steckerlagen (J1/J22 gedreht, Öffnung über der Kante) plus Nächster-freier-Platz-Suche für die neuen Teile, reißt Kupfer darunter auf, erneuert die USB-C-Auffächerung |
| `silk_v10.py board` | Siebdruck v1.0: Texte entfernter Teile löschen, J8/J2-Beschriftung versetzen, Versionstext |
| `hide_small_refs.py board` | Referenzen von R/C/FB/TP/JP/Q/D (außer LED D4) aus dem Siebdruck nehmen (Regel dieser Platine), "FDD PWR" unter F2 |
| `fix_via_annular.py board [min]` | Bohrung ungesperrter Vias verkleinern, deren Restring unter dem Minimum liegt (Freerouting setzt gelegentlich 0,6/0,4) |
| `batch_route.py board drc.json [--apply] [--only/--skip NET,..] [--order NET,..] [--gnd REF.PAD,..] [--step] [--margin]` | A*-Router für alle offenen Verbindungen eines kicad-cli-DRC-Berichts (JSON): Pad-, Bahnenden- und Via-Enden werden selbst bestimmt, Raster am Startpunkt ausgerichtet, bei Fehlschlag Wiederholung mit größerem Fenster und 0,1-mm-Raster; `--gnd` setzt für GND-Pads ohne Flächenkontakt einen Via zur In1-Ebene; bei Fehlschlag Blockade-Bericht je Achse. Zonen vorher nicht nötig; danach `route.py import <board> none` und DRC |
| `rip_net.py board NET[:x0,y0,x1,y1] ...` | ungesperrtes Kupfer eines Netzes (oder `*` = alle Netze) löschen, wahlweise nur im Fenster – Korridore für `fanout.py`/`batch_route.py` freiräumen |
| `fanout.py board REF PAD,PAD,.. +x\|-x\|+y\|-y D1 D2 [check]` | Dogbone-Auffächerung einer 0,5-mm-QFP-Pinreihe: Stich plus 0,5/0,25-Via, Vias in zwei versetzten Reihen (D1/D2 vom Pad-Mittelpunkt), Kollisionsprüfung |
| `shrink_vias.py board x0 y0 x1 y1 ...` | Vias im Fenster auf 0,5/0,25 setzen – ein 0,6-mm-Dogbone neben einer 0,5-mm-Pinreihe sperrt die Nachbarbahn (0,1 mm Rest) |
| `relayout_lan.py board` | v1.0 B2: Quarz-Cluster und 3V3A-Filter unter U23 neu gelegt (Y2 links, C60 zwischen Quarz und SPI-Spalte), SW3 um 90° gedreht; reißt betroffenes Kupfer auf |
| `set_in_bom.py yes\|no REF ...` | `in_bom`-Flag von Schaltplansymbolen setzen (Testpunkte/Lötbrücken: `no`, sonst meldet die DRC-Parität den Footprint-Unterschied) |
| `add_vias.py board NET x,y .. [dia drill] [check]` | Vias eines Netzes an festen Punkten setzen (GND-Insel, isoliertes GND-Pad), mit Kollisionsprüfung gegen Fremdkupfer, Löcher und SMD-Pads |
| `net_tracks.py board NET [x0 y0 x1 y1]` | Bahnen und Vias eines Netzes als Text (Lage, Endpunkte, Breite, Länge) – Route lesen ohne Bild |
| `drop_pours.py board` | Versorgungs-Pours (Priorität ≥ 5) löschen, damit `power_pours.py` sie nach einer Umroutung neu entlang der Bahnen baut (alte Pours überbrücken sonst fehlende Bahnen und verdecken offene Verbindungen) |
| `lock_except.py board out NET[,NET] [rip]` | alle Bahnen außer den genannten Netzen sperren (DSN-Export schreibt sie als `fix`) – Freerouting nur für diese Netze; 24.10.: lief auf dem vollen Board in den Timeout |
| `place_silk.py board REF[,REF] ['TEXT=..'] [REF@x:y[:winkel]] [check] [maxd=MM]` | Referenzen/Platinentexte auf den nächsten freien Siebdruckplatz um die Pad-Box setzen (Ringe bis maxd, auch 90° gedreht), `@x:y` setzt fest und meldet Kollisionen; F- und B-Siebdruck |
| `pin_labels.py board [check] [REF ...]` | Pin-Beschriftung der internen Stiftleisten (Tabelle im Skript): 0,8-mm-Texte neben jedem Pin außerhalb des Steckerumrisses, bei Platzmangel auf der Rückseite gespiegelt neben den Lötpins; Gruppe `pinlabels REF` je Stecker, erneuter Lauf ersetzt sie; J12-Massepins als Legende auf der Rückseite |
| `set_netclasses.py project.kicad_pro` | Netzklasse `Load` 1,0 mm (0,8/0,4-Vias) für die 16 Lastnetze nach Skill `pcb-routing` eintragen (idempotent) |

Reihenfolge nach einer Umplatzierung: `rip_net.py` (Korridore), `fanout.py` (QFP-Reihen), `batch_route.py` mit `--order` (die engen Netze zuerst), `trim_dangling.py` erst ganz am Ende – es entfernt auch einseitige Auffächerungs-Vias noch ungerouteter Netze.

Abstand: `ROUTE_CLR` (mm, Standard 0.17; Regel ist 0.15 – neben 0,5-mm-Vias bleibt genau 0,15, dann `ROUTE_CLR=0.1499` (unter der DRC-Epsilon von 0,0005), die DRC prüft danach). `ROUTE_W` überschreibt die Netzklassenbreite (GND-Stich 0,2 mm in einer Enge). `batch_route.py` prüft jeden Rasterschritt als Segment, zieht den Pfad je Lage gerade und rechnet Vias in mm (`--via-cost`, Standard 4).
Lastpfade danach messen: `python ~/.claude/skills/pcb-routing/scripts/check_routing.py UFI_Headless.kicad_pcb --rules routing_rules.json`.
Nach Handrouten: `scripts/route.py import <board> none` (Zonen füllen), `scripts/finish_pcb.py` (Insel-Vias), DRC.
