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
| `probe_region.py board x0 y0 x1 y1` | Courtyards und Kupfer in einem Fenster auflisten |
| `check_via_in_pad.py board` | Vias, die ein SMD-Pad berühren (muss 0 sein) |

Abstand: `ROUTE_CLR` (mm, Standard 0.2; Regel ist 0.15 – an 0,5-mm-QFP-Pads mit 0.15 routen, die DRC prüft danach).
Nach Handrouten: `scripts/route.py import <board> none` (Zonen füllen), `scripts/finish_pcb.py` (Insel-Vias), DRC.
