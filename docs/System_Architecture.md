# UFI System-Architektur

## Übersicht

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│    ┌──────────────┐         ┌──────────────┐         ┌──────────────┐                   │
│    │              │   GbE   │              │  USB HS │              │    Flux           │
│    │      PC      │◄───────►│     CM5      │◄───────►│   STM32H723  │◄──────► Laufwerke │
│    │              │         │              │         │              │                   │
│    │ Hauptprogramm│         │ Verarbeitung │         │ Flux Engine  │                   │
│    │              │         │              │         │              │                   │
│    └──────────────┘         └──────────────┘         └──────────────┘                   │
│                                                                                          │
│         GUI                    Algorithmen              Timing                           │
│         Archiv                 Routinen                 Steuerung                        │
│         Finale Erstellung      Puffern                  Erfassung                        │
│                                Verbessern                                                │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Komponenten-Aufgaben

### STM32H723 - Flux Engine

**Aufgabe:** Nur Echtzeit-kritische Datenerfassung

| Funktion | Beschreibung |
|----------|--------------|
| Flux-Timing | Präzise Erfassung mit 25ns Auflösung |
| Laufwerk-Steuerung | Motor, Step, Side, Drive Select |
| Level-Shifting | 3.3V ↔ 5V für Laufwerke |
| Raw-Stream | Unverarbeitete Flux-Daten → CM5 |

**Keine Analyse, keine Dekodierung!**

```
┌─────────────────────────────────────────────────────────────┐
│                     STM32H723                                │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   INPUT:   Flux-Signale von Laufwerken                      │
│                                                             │
│   PROCESS: Timer-Capture (TIM2/TIM5 32-bit)                │
│            DMA → RAM Buffer                                 │
│            USB HS Bulk Transfer                             │
│                                                             │
│   OUTPUT:  Raw Flux Timing Stream → CM5                    │
│            Format: [timestamp_32bit, timestamp_32bit, ...]  │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

### CM5 - Verarbeitung & Algorithmen

**Aufgabe:** Puffern, Routinen, Algorithmen, Verbessern

| Funktion | Beschreibung |
|----------|--------------|
| Puffern | Komplette Disk auf NVMe/RAM cachen |
| Timing-Analyse | Flux-Zeiten normalisieren |
| Fehlerkorrektur | CRC prüfen, Retries koordinieren |
| Multi-Read | Mehrfach-Lesungen kombinieren |
| Kopierschutz | Pattern erkennen & dokumentieren |
| Qualität | Datenqualität-Score berechnen |
| Web-Interface | Status, Steuerung, Konfiguration |

```
┌─────────────────────────────────────────────────────────────┐
│                        CM5                                   │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   INPUT:   Raw Flux Stream vom STM32                        │
│                                                             │
│   ROUTINEN & ALGORITHMEN:                                   │
│                                                             │
│   ┌─────────────────────────────────────────────────────┐  │
│   │ 1. Puffern                                          │  │
│   │    • Komplette Disk in RAM/NVMe                     │  │
│   │    • Kein Datenverlust bei PC-Lag                   │  │
│   ├─────────────────────────────────────────────────────┤  │
│   │ 2. Timing-Normalisierung                            │  │
│   │    • Drehzahl-Schwankungen ausgleichen              │  │
│   │    • Flux-Zeiten auf Referenz normieren             │  │
│   ├─────────────────────────────────────────────────────┤  │
│   │ 3. Fehler-Erkennung                                 │  │
│   │    • CRC/Checksum Validierung                       │  │
│   │    • Fehlende Sektoren markieren                    │  │
│   │    • Automatische Retries auslösen                  │  │
│   ├─────────────────────────────────────────────────────┤  │
│   │ 4. Multi-Read Kombination                           │  │
│   │    • Mehrere Durchläufe sammeln                     │  │
│   │    • Best-of-N Auswahl                              │  │
│   │    • Weak-Bits durch Vergleich finden               │  │
│   ├─────────────────────────────────────────────────────┤  │
│   │ 5. Kopierschutz-Analyse                             │  │
│   │    • Bekannte Patterns erkennen                     │  │
│   │    • Timing-Anomalien dokumentieren                 │  │
│   │    • Schwache Bits lokalisieren                     │  │
│   ├─────────────────────────────────────────────────────┤  │
│   │ 6. Qualitäts-Bewertung                              │  │
│   │    • Score pro Sektor/Track/Disk                    │  │
│   │    • Empfehlungen für Retries                       │  │
│   │    • Preservation-Confidence Level                  │  │
│   └─────────────────────────────────────────────────────┘  │
│                                                             │
│   OUTPUT:  Aufbereitete, verbesserte Daten → PC            │
│            • Normalisierte Flux-Daten                       │
│            • Sektor-Daten mit Qualitäts-Info               │
│            • Kopierschutz-Metadaten                         │
│            • Fehler-Reports                                 │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

### PC - Hauptprogramm

**Aufgabe:** GUI, finale Image-Erstellung, Archiv

| Funktion | Beschreibung |
|----------|--------------|
| GUI | Benutzeroberfläche, Steuerung |
| Image-Erstellung | D64, G64, ADF, IPF, SCP, etc. |
| Archiv | Datenbank, Katalogisierung |
| Export | Verschiedene Formate |
| Mehrere UFIs | Zentrale Verwaltung |

```
┌─────────────────────────────────────────────────────────────┐
│                    PC HAUPTPROGRAMM                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   INPUT:   Aufbereitete Daten vom CM5                       │
│            • Bereits verbessert & validiert                 │
│            • Mit Qualitäts-Metadaten                        │
│                                                             │
│   FUNKTIONEN:                                               │
│                                                             │
│   ┌─────────────────────────────────────────────────────┐  │
│   │ GUI                                                 │  │
│   │ • Disk einlegen → "Dump" klicken → Fertig          │  │
│   │ • Fortschritt, Status, Qualitäts-Anzeige           │  │
│   │ • Mehrere UFIs parallel steuern                    │  │
│   ├─────────────────────────────────────────────────────┤  │
│   │ Image-Erstellung                                    │  │
│   │ • Finale Formate: D64, G64, ADF, IPF, SCP, etc.    │  │
│   │ • Format-Auswahl je nach Disk-Typ                  │  │
│   │ • Kopierschutz-Preservation                        │  │
│   ├─────────────────────────────────────────────────────┤  │
│   │ Archiv & Datenbank                                  │  │
│   │ • Katalogisierung (Titel, System, Jahr, etc.)      │  │
│   │ • Checksummen, Vergleich mit Datenbanken           │  │
│   │ • Duplikat-Erkennung                               │  │
│   ├─────────────────────────────────────────────────────┤  │
│   │ Export & Teilen                                     │  │
│   │ • Archive.org Upload                               │  │
│   │ • Preservation-Netzwerke                           │  │
│   │ • Lokale Backups                                   │  │
│   └─────────────────────────────────────────────────────┘  │
│                                                             │
│   OUTPUT:  Fertige Disk-Images                              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Datenfluss

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                  DATENFLUSS                                              │
├─────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                          │
│   LAUFWERK              STM32                 CM5                    PC                  │
│                                                                                          │
│   ┌─────┐            ┌─────────┐          ┌─────────┐          ┌─────────────┐          │
│   │     │  Flux      │         │  Raw     │         │  Clean   │             │          │
│   │ FDD │──Pulse────►│  Timer  │──Flux───►│ Buffer  │──Data───►│    GUI      │          │
│   │     │            │  DMA    │  Stream  │ Analyse │          │   Image     │          │
│   └─────┘            │  USB    │          │ Improve │          │   Archiv    │          │
│                      └─────────┘          └─────────┘          └─────────────┘          │
│                                                                                          │
│   Analog              ~10 MB/s             ~5 MB/s              ~1 MB/s                 │
│   Signal              Raw Timing           Processed            Final                   │
│                                            + Metadata           Images                  │
│                                                                                          │
├─────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                          │
│   RÜCKKANAL (Steuerung):                                                                │
│                                                                                          │
│   PC ──"Dump Track 35"──► CM5 ──"Step, Read"──► STM32 ──Motor/Step──► Laufwerk         │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Schnittstellen

### STM32 ↔ CM5: USB High-Speed

```
Protokoll:  USB Bulk Transfer
Geschw.:    480 Mbit/s (max)
Daten:      Raw Flux Timing (32-bit Timestamps)
Format:     Binär-Stream, ~200 KB pro Track
```

### CM5 ↔ PC: Gigabit Ethernet

```
Protokoll:  TCP/IP, REST API, WebSocket
Geschw.:    1 Gbit/s
Daten:      Aufbereitete Sektor-Daten + Metadaten
Format:     JSON (Steuerung), Binary (Daten)
```

### Web-Interface (CM5)

```
URL:        http://ufi.local / http://192.168.x.x
Funktionen: 
  • Status-Anzeige (aktueller Track, Qualität)
  • Manuelle Steuerung (für Debugging)
  • Konfiguration (Algorithmus-Parameter)
  • Firmware-Update

NICHT für normale Bedienung - nur für Setup/Debug!
```

---

## USB-C zum PC (J9)

**NUR für:**

| Funktion | Beschreibung |
|----------|--------------|
| Firmware-Update | STM32 DFU Mode |
| Debug | UART über USB |
| Entwicklung | Direkter Zugriff ohne CM5 |

**NICHT für normalen Betrieb!**

---

## Zusammenfassung

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│   STM32:  Erfassen (Echtzeit, präzise)                     │
│                     ↓                                       │
│   CM5:    Puffern + Routinen + Algorithmen + Verbessern    │
│                     ↓                                       │
│   PC:     Hauptprogramm (GUI, Image, Archiv)               │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```
