# UFI Hardware Test-Prozeduren

## Übersicht

Dieses Dokument beschreibt die Test-Prozeduren für das UFI Mainboard nach Fertigung und Bestückung.

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                              TEST-ABLAUF                                                 │
├─────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                          │
│   1. Visuelle Inspektion ──► 2. Stromversorgung ──► 3. STM32 ──► 4. CM5                │
│                                                                                          │
│   ──► 5. USB ──► 6. Ethernet ──► 7. Laufwerke ──► 8. IEC ──► 9. System-Test            │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Benötigte Ausrüstung

| Gerät | Beschreibung |
|-------|--------------|
| Multimeter | Spannung, Widerstand, Durchgang |
| Oszilloskop | Min. 100 MHz, 2 Kanäle |
| Labornetzteil | 12V/3A einstellbar mit Strombegrenzung |
| USB-Kabel | USB-C |
| Ethernet-Kabel | Cat5e oder besser |
| ST-Link V2 | Oder kompatibel für SWD |
| Testdisk | Bekannte, saubere Disk |
| 1541 | Für IEC-Tests |

---

## Test 1: Visuelle Inspektion

### Vor dem Einschalten!

**Checkliste:**

- [ ] Alle SMD-Bauteile vorhanden und richtig orientiert
- [ ] Keine Lötbrücken (besonders bei STM32, CM5-Stecker)
- [ ] Keine kalten Lötstellen
- [ ] Keine fehlenden Durchkontaktierungen
- [ ] Alle THT-Stecker richtig eingelötet
- [ ] Keine Beschädigungen am PCB
- [ ] Polarität der Elkos korrekt
- [ ] LED-Polarität korrekt

**Besonders prüfen:**

```
┌─────────────────────────────────────────────────────────────┐
│                 KRITISCHE BEREICHE                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   STM32H723 (U1)                                           │
│   • 144 Pins, 0.5mm Pitch                                  │
│   • Pin 1 Markierung prüfen                                │
│   • Auf Lötbrücken unter dem IC prüfen                    │
│                                                             │
│   CM5 Stecker (J6, J7)                                     │
│   • 100 Pins, 0.4mm Pitch                                  │
│   • Keine verbogenen Pins                                  │
│   • Richtige Ausrichtung                                   │
│                                                             │
│   Buck-Regler (U10, U11)                                   │
│   • Thermal Pad unter IC                                   │
│   • Richtige Orientierung                                  │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Test 2: Stromversorgung

### 2.1 Kurzschluss-Test (ohne Spannung!)

Mit Multimeter auf Durchgang:

| Test | Erwartung | ✓/✗ |
|------|-----------|-----|
| +12V gegen GND | > 100Ω | |
| +5V gegen GND | > 10Ω | |
| +3V3 gegen GND | > 10Ω | |
| +1V8 gegen GND | > 10Ω | |

**Bei Kurzschluss: NICHT einschalten! Fehler suchen.**

### 2.2 Einschalten (ohne CM5!)

1. Strombegrenzung auf 500mA setzen
2. 12V anlegen
3. Strom beobachten

**Erwartete Werte:**

| Messung | Erwartung | Toleranz | Gemessen |
|---------|-----------|----------|----------|
| Einschaltstrom | < 200mA | | |
| Ruhestrom | ~50mA | ±20mA | |
| +5V Rail | 5.0V | ±0.25V | |
| +3V3 Rail | 3.3V | ±0.1V | |
| +1V8 Rail | 1.8V | ±0.1V | |

### 2.3 LED-Check

| LED | Farbe | Status |
|-----|-------|--------|
| PWR | Grün | AN |
| NET | Gelb | AUS |
| FDD | Rot | AUS |
| USB | Blau | AUS |

### 2.4 Ripple-Messung (Oszilloskop)

| Rail | Max. Ripple | Gemessen |
|------|-------------|----------|
| +5V | 50mV pp | |
| +3V3 | 30mV pp | |
| +1V8 | 20mV pp | |

---

## Test 3: STM32 Mikrocontroller

### 3.1 Quarz-Test

Mit Oszilloskop an HSE (25 MHz Quarz):

- [ ] Schwingung vorhanden
- [ ] Amplitude > 500mV
- [ ] Frequenz: 25.000 MHz ±50ppm

### 3.2 SWD Verbindung

1. ST-Link anschließen (J17)
2. STM32CubeProgrammer öffnen
3. Verbinden

**Erwartung:**
```
Connected to STM32H723ZGT6
Device ID: 0x483
Flash: 1024 KB
```

### 3.3 Firmware Flash

1. `ufi_firmware.bin` laden
2. Flash
3. Verify

```bash
# Alternativ mit st-flash:
st-flash write ufi_firmware.bin 0x08000000
st-flash verify ufi_firmware.bin 0x08000000
```

### 3.4 Boot-Test

Nach Flash sollte:
- [ ] PWR LED leuchten
- [ ] USB LED kurz blinken
- [ ] Keine Error-LED

---

## Test 4: CM5 Modul

### 4.1 CM5 einsetzen

1. Stromversorgung AUS
2. CM5 vorsichtig auf Stecker setzen
3. Auf Ausrichtung achten!
4. Leicht andrücken bis es einrastet

### 4.2 CM5 Boot

1. Strombegrenzung auf 2A setzen
2. 12V anlegen
3. Strom beobachten

**Erwartete Werte:**

| Phase | Strom | Zeit |
|-------|-------|------|
| Initial | ~500mA | 0-2s |
| Boot | ~800mA | 2-30s |
| Idle | ~600mA | >30s |

### 4.3 CM5 SSH Zugang

```bash
# Nach vollständigem Boot (~30s)
ssh ufi@ufi.local
# Oder mit IP
ssh ufi@192.168.x.x
```

Passwort: `ufi`

---

## Test 5: USB Verbindung

### 5.1 USB zu STM32 (Device Mode)

1. USB-C Kabel an J9 anschließen
2. Am PC prüfen:

```bash
# Linux
lsusb | grep "1209:4f54"
# Erwartung: Bus xxx Device xxx: ID 1209:4f54 UFI Flux Engine

# Windows: Geräte-Manager → COM-Ports
```

### 5.2 USB High-Speed Test

```bash
# Bandbreiten-Test
dd if=/dev/zero bs=1M count=10 | nc ufi.local 12345
# Erwartung: >30 MB/s
```

---

## Test 6: Gigabit Ethernet

### 6.1 Link-Test

1. Ethernet-Kabel an J10 anschließen
2. Link-LED am RJ45 prüfen

| LED | Bedeutung | Status |
|-----|-----------|--------|
| Grün | Link | AN |
| Gelb | Activity | Blinkt |

### 6.2 Ping-Test

```bash
ping ufi.local
# Erwartung: <1ms Latenz

# Speed-Test
iperf3 -c ufi.local
# Erwartung: >900 Mbit/s
```

### 6.3 Web-Interface

Browser: `http://ufi.local`

- [ ] Seite lädt
- [ ] Status wird angezeigt
- [ ] STM32 als "verbunden" gemeldet

---

## Test 7: Floppy-Laufwerke

### 7.1 FDD Interface Test

**Ausrüstung:**
- 3.5" HD Floppy-Laufwerk
- Flachbandkabel 34-pin
- Testdisk (leer oder bekannter Inhalt)

**Anschluss:**
1. Laufwerk an J1 oder J2
2. Jumper für Drive Select prüfen (DS1)

**Test-Sequenz:**

```python
# Via API oder Direkt-Befehle

# 1. Laufwerk wählen
POST /api/drive/select {"drive": 1}

# 2. Motor an
POST /api/drive/motor {"on": true}
# → Motor-LED am Laufwerk leuchtet

# 3. Recalibrate
POST /api/drive/recalibrate
# → Stepper bewegt sich zu Track 0

# 4. Seek Test
POST /api/drive/seek {"track": 40}
# → Stepper bewegt sich

# 5. Track lesen
POST /api/read/track {"track": 0, "side": 0}
# → Flux-Daten werden zurückgegeben
```

**Oszilloskop-Messung:**

| Signal | Pin | Erwartung |
|--------|-----|-----------|
| INDEX | 8 | ~300ms Periode, 1-5ms Puls |
| RDATA | 30 | Flux-Pulse, ~2µs typisch |
| STEP | 20 | 3µs Pulse bei Seek |

### 7.2 Apple Disk II Test

1. Laufwerk an J3 anschließen
2. Drive Select auf Apple stellen

```python
POST /api/drive/select {"drive": 3}
POST /api/drive/motor {"on": true}
POST /api/drive/recalibrate
POST /api/read/track {"track": 0, "side": 0}
```

### 7.3 Amiga External Test

1. Über Adapter-Kabel (2x12 Header → DB-23)
2. Amiga-Laufwerk anschließen

```python
POST /api/drive/select {"drive": 4}
POST /api/drive/motor {"on": true}
# Amiga-Laufwerke sind meist 80 Tracks, Double-Sided
POST /api/read/track {"track": 0, "side": 0}
POST /api/read/track {"track": 0, "side": 1}
```

---

## Test 8: IEC Bus (C64/1541)

### 8.1 Hardware-Test

**Anschluss:**
- DIN-6 Kabel zwischen UFI (J5) und 1541
- 1541 mit eigenem Netzteil

**Bus-Pegel messen:**

| Signal | Pin | Idle-Zustand |
|--------|-----|--------------|
| ATN | 3 | High (~5V) |
| CLK | 4 | High (~5V) |
| DATA | 5 | High (~5V) |
| RESET | 6 | High (~5V) |

### 8.2 Kommunikations-Test

```python
# Reset
POST /api/iec/reset
# → 1541 LED blinkt kurz

# Status lesen
GET /api/iec/status {"device": 8}
# Erwartung: "73, CBM DOS V2.6 1541,00,00"

# Directory lesen
GET /api/iec/directory {"device": 8}
# → Verzeichnis-Listing

# Block lesen
POST /api/iec/read {"device": 8, "track": 18, "sector": 0}
# → 256 Bytes BAM-Sektor
```

### 8.3 Timing-Analyse (Oszilloskop)

| Signal | Timing | Toleranz |
|--------|--------|----------|
| Bit-Zeit | ~70µs | ±20µs |
| EOI-Delay | >200µs | |
| ATN Response | <1000µs | |

---

## Test 9: System-Integrations-Test

### 9.1 Vollständiger Dump-Test

1. Testdisk in Laufwerk einlegen
2. Kompletten Dump durchführen

```python
# Komplette Disk lesen
POST /api/read/disk {
    "tracks": 80,
    "sides": 2,
    "revolutions": 3
}
```

**Erwartung:**
- Alle Tracks gelesen
- Qualität > 90%
- Keine Timeouts
- Dauer: ~90 Sekunden für 160 Tracks

### 9.2 Stress-Test

```bash
# 10 Disks hintereinander dumpen
for i in {1..10}; do
    curl -X POST http://ufi.local:5000/api/read/disk -d '{"tracks":80,"sides":2}'
    echo "Disk $i fertig"
done
```

### 9.3 Langzeit-Test

- 1 Stunde kontinuierlicher Betrieb
- Temperatur-Überwachung (max. 70°C für STM32, 80°C für CM5)

---

## Fehlersuche

### Problem: Kein USB erkannt

1. USB-Kabel prüfen (Datenkabel, nicht nur Ladekabel)
2. USB-Stecker auf Lötstellen prüfen
3. STM32 Firmware prüfen
4. Boot-Jumper prüfen (BOOT0 = Low)

### Problem: Laufwerk reagiert nicht

1. Kabelverbindung prüfen
2. Drive-Select Einstellung
3. Motor-Signale mit Oszilloskop prüfen
4. Level-Shifter Spannungen prüfen (5V Seite)

### Problem: Flux-Daten fehlerhaft

1. RDATA Signal am Oszilloskop prüfen
2. Timer-Clock verifizieren (275 MHz)
3. DMA-Buffer Überlauf prüfen
4. Index-Puls Timing prüfen

### Problem: IEC kommuniziert nicht

1. Bus-Pegel messen (alle High im Idle)
2. 1541 Reset durchführen
3. ATN-Timing prüfen (<1000µs Response)
4. Kabel-Qualität prüfen

---

## Test-Protokoll

```
UFI Test-Protokoll
==================

Datum: _______________
Seriennummer: UFI-_______
Tester: _______________

[ ] Test 1: Visuelle Inspektion      PASS / FAIL
[ ] Test 2: Stromversorgung          PASS / FAIL
[ ] Test 3: STM32                    PASS / FAIL
[ ] Test 4: CM5                      PASS / FAIL
[ ] Test 5: USB                      PASS / FAIL
[ ] Test 6: Ethernet                 PASS / FAIL
[ ] Test 7: Floppy-Laufwerke         PASS / FAIL
[ ] Test 8: IEC Bus                  PASS / FAIL
[ ] Test 9: System-Test              PASS / FAIL

Messwerte:
+5V:  _______ V
+3V3: _______ V
+1V8: _______ V
Ruhestrom: _______ mA

Bemerkungen:
____________________________________________
____________________________________________

Ergebnis: BESTANDEN / NICHT BESTANDEN

Unterschrift: _______________
```
