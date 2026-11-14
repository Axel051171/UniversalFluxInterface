# Schreiben und Prüflesen

**Sprache / Language:** [Deutsch](Schreiben-und-Prueflesen) | [English](Writing-and-Verification)

Schreibbefehle verändern das eingelegte Medium. Für Einrichtung und Abnahme ausschließlich entbehrliche Disketten verwenden. Der physische Schreibpfad ist nach dem dokumentierten Prüfstand noch nicht mit Laufwerken abgenommen.

## Schreibfreigabe

Auf v1.0 müssen der Frontschalter SCHREIBEN und die Firmware-Freigabe den Hardwarepfad erlauben. Zusätzlich gelten Laufwerksschreibschutz und eine eventuell gesetzte Sitzungssperre.

```console
ufi write-lock
ufi status
ufi write-limits
```

Mit `ufi write-lock engage` lässt sich die Sitzung bis zum MCU-Reset gegen Diskettenschreiben sperren. Diese Sperre kann nicht mit einem USB-Befehl wieder aufgehoben werden. Sie ersetzt den physischen Schreibschutz nicht.

## SCP zurückschreiben

Eine einzelne Spur aus einem vorhandenen SCP-Abbild:

```console
ufi write disk.scp 0 0 --verify
```

Vorher das passende Laufwerk auswählen und den Motor starten. Für ein vollständiges Abbild:

```console
ufi write-disk disk.scp --drive a --verify
```

Die Geometrie und der Laufwerkstyp müssen zum Abbild passen. Standardmäßig wird die erste Umdrehung der jeweiligen SCP-Spur verwendet. HFE-Abbilder werden über Bitzellen in Fluxintervalle umgesetzt.

## Bedeutung der Verify-Stufen

| Stufe | Aussage |
|---|---|
| 0 | Keine entsprechende Bestätigung |
| 1 | Schreibausgabe abgeschlossen |
| 2 | Rücklesen plausibel; keine Inhaltsgleichheit bestätigt |
| 3 | Inhalt der erfassten Referenzsektoren bestätigt |
| 4 | Mit diesem Verfahren nicht verifizierbar |

Die Stufen sind unterschiedliche Nachweisklassen. Stufe 4 ist keine stärkere Bestätigung als Stufe 3. Ein Rohspur-Fingerabdruck vergleicht Zeit-/Dichteverteilungen und bestätigt keinen identischen Sektorinhalt.

## Erweiterter Schreibpfad

```console
ufi --extended write disk.scp 0 0 --verify
```

Der erweiterte Pfad verarbeitet hostseitig vorgegebene Zeiten und lange Lücken. Unterstützte Kombinationen hängen vom Backend, Format und der Firmware ab. Bei erweitertem `--verify` kann der Host das Rücklesen übernehmen. Für spezielle Optionen immer `ufi write --help`, `ufi describe`, `ufi write-limits` und die Protokollreferenz prüfen.

## Optionen mit bewusster Signaländerung

- `--clamp` verändert nicht darstellbare Intervalle und meldet die Anzahl; kein verlustfreier Ersatz für einen geeigneten Schreibpfad.
- `--scale` skaliert Fluxzeiten; nur mit begründeter Anpassung an Quell- und Zieldrehzahl verwenden.
- `--erase-rest` hält das Schreibfenster nach den Daten bis zum nächsten Index offen und löscht den Rest der Spur.
- `--start-offset` verschiebt den Beginn relativ zum Index; die Einheit sind 275-MHz-Ticks.

Diese Optionen nicht pauschal kombinieren. Die Backend-Grenzen und Fehlerstatus müssen vor dem Einsatz auf wertvollen Medien geklärt sein.

## Abnahme

Der [Messplan des Schreibpfads](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Abnahme_Schreibpfad.md) beschreibt Impulsanzahl, DMA-Grenzen, Indexabbruch, Schreibsperre und Rücklesen. Zuerst Loopback/Logikanalysator, danach Testmedium und möglichst ein unabhängiger Controller zur Gegenprüfung.

Quelle: [Verify-Klassen](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/include/ufi_verify.h).
