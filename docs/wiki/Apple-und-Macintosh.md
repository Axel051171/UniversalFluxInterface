# Apple und Macintosh

**Sprache / Language:** [Deutsch](Apple-und-Macintosh) | [English](Apple-and-Macintosh)

Apple Disk II und Macintosh verwenden verschiedene Laufwerkssteuerungen. Auf v1.0 ist J14 der Disk-II-Port und J23 der Macintosh-Port. Laufwerk, Versorgung, Adapter und optionale Lötbrücken vor dem Anschluss ausdrücklich zuordnen.

## Apple Disk II

```console
ufi select apple
ufi motor on
ufi recal
ufi read 17 0 -r 3 --quarter 2 --read-info -o apple-track.scp
ufi motor off
```

`--quarter 2` bedeutet Spur 17 plus zwei Viertelschritte, also 17,5. Das wird anders kodiert als die Seitenauswahl.

Ohne Sync-Sensor liefert das Disk-II-Laufwerk keinen normalen Index. UFI teilt die Aufnahme dann in Zeitscheiben. Deren Grenzen sind keine feste Winkelposition auf dem Medium. Mit geeignetem Sensor an J19 und passendem `apple_sync`-Aufbau kann die Firmware physische Umdrehungsgrenzen verwenden.

Für ein dekodiertes Apple-II-Abbild:

```console
ufi read-dsk -o apple.dsk --tracks 35 -r 2
```

`.dsk` verwendet standardmäßig die DOS-3.3-Reihenfolge, `.po` die ProDOS-Reihenfolge. Optionen für andere Prologe, Symboltabellen und tolerante Dekodierung sind Spezialfälle; Fehlerbefunde nicht als gute Sektoren umdeuten.

## Macintosh auf v1.0

```console
ufi select mac
ufi mac status
ufi read-mac -o mac.dc42 --speed --retries 2
```

Die Firmware steuert Sony-Register und zonenabhängige Drehzahlen. 400K, 800K und FDHD unterscheiden sich; insbesondere optionale PWM- und −12-V-Brücken nur nach Prüfung des konkreten Laufwerks setzen. Der Name `apple2` aus älteren v0.7-Unterlagen bezeichnet auf v1.0 keine zweite Disk-II-Buchse.

Der FDHD-MFM-Weg besitzt gesonderte Optionen, einschließlich eines nicht am Gerät bestätigten Moduskommandos. Nicht aus der Erkennung des Laufwerks auf vollständige Formatunterstützung schließen.

## Schreiben

`write-mac` erzeugt GCR-Spuren aus geeigneten 400K-/800K-Abbildern und kann zurücklesen. Die Implementierung ist offline getestet; der Gerätebetrieb gehört zu den offenen Messungen. Zuerst [Schreiben und Prüflesen](Schreiben-und-Prueflesen) sowie den laufwerksspezifischen Abnahmeplan beachten.

Quellen: [Mac-Treiber](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_mac.c), [Host-Apple-Decoder](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/apple.py), [Host-Mac-Decoder](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/mac.py).
