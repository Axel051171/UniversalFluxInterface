# Laufwerke

**Sprache / Language:** [Deutsch](Laufwerke) | [English](Drives)

Die Laufwerksauswahl beschreibt den elektrischen Anschluss. Sie legt nicht automatisch das Diskettenformat fest. Vor einer Aufnahme müssen Anschluss, Auswahl, Versorgung, Geometrie und gegebenenfalls Doppelschritt zusammenpassen.

| Auswahl | Anschluss bzw. Familie | Besonderheit |
|---|---|---|
| `a`, `b` | PC-Laufwerke am 34-poligen Kabel | Position und Twist beachten |
| `ds0` bis `ds3` | Gerader Shugart-Bus | Auswahljumper am Laufwerk; gemeinsame Motorleitung |
| `amiga`, `amiga2` | Externe Amiga-Laufwerke über J7 | Zweite Auswahl benötigt passende JP3-/Kabelkonfiguration |
| `apple` | Apple Disk II an J14 | Viertelspuren; ohne Sync-Sensor keine physischen Indexgrenzen |
| `mac` | Macintosh an J23 auf v1.0 | Zonendrehzahl, andere Registersteuerung |
| `iec` | Commodore-Laufwerk am IEC-Bus | Zugriff über Laufwerkscontroller bzw. Nibble-Helfer |

## Auswahl und Bewegung

```console
ufi select a
ufi motor on
ufi recal
ufi seek 20
ufi side 0
ufi status
ufi motor off
```

`recal` sucht die Referenzposition; `seek` bewegt den Kopf. `scan` prüft mehrere Auswahlen und kann ebenfalls Köpfe bewegen. Einen Scan erst nach sicherer Verkabelung durchführen.

## 40 Spuren im 80-Spur-Laufwerk

Hier liegen benachbarte logische Spuren zwei mechanische Schritte auseinander. Für einen einzelnen READ unterstützt der aktuelle Entwicklungsstand:

```console
ufi read 17 0 -r 3 --double-step --read-info -o track17.scp
```

Für eine allgemeine Timing-Einstellung:

```console
ufi timing double_step=1
```

Diese Einstellung wirkt auch auf folgende Operationen und sollte nach dem Wechsel zu einem 80-Spur-Medium zurückgesetzt werden. Der Schalter `--double-step` gilt nur für den einzelnen Lesebefehl und ist nicht für Apple/Mac vorgesehen.

Die Halbspur `--quarter 2` eines Shugart-Laufwerks ist im Doppelschrittmodus die mechanische Spur `2n+1`. Viertel 1 und 3 kann ein normaler Shugart-Schrittmotor nicht anfahren.

## Drehzahl und Timing

```console
ufi rpm
ufi monitor --drive a --seconds 60
ufi timing
ufi profile
```

Profile und Zusatzzeiten sind laufwerksspezifisch. Ein konservatives Profil kann beim Einrichten helfen; ein erfolgreiches `seek` ersetzt keinen Test mit bekannten Spurkennungen. 8-Zoll-Laufwerke benötigen zusätzlich einen passenden Adapter und eine passende TG43-/Timingkonfiguration.

Quelle: [Laufwerkstreiber](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_drive.c), [Host-Befehle](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/README.md).
