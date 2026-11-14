# LAN

**Sprache / Language:** [Deutsch](LAN) | [English](LAN-EN)

Platine v1.0 besitzt einen W5500 mit RJ45. Die Firmware enthält Adressvergabe, Discovery und eine UFI-v2-Sitzung über TCP. Ein Test mit LAN-Kabel am Gerät ist im dokumentierten Prüfstand noch offen.

## Zustand und Discovery

Zunächst über USB:

```console
ufi net
ufi discover --timeout 2
```

Discovery verwendet UDP-Port 55100. Die Sitzung läuft standardmäßig über TCP-Port 55100. Eine Firewall muss den jeweils benötigten Verkehr zulassen. Ohne DHCP-Server ist ein Link-Local-Fallback vorgesehen; die Verbindung muss dann im passenden lokalen Netz liegen.

## LAN.CFG

Die Datei liegt im internen Speicher. Beispiel für DHCP und gesperrte LAN-Schreibzugriffe:

```ini
lan=on
ip=dhcp
mask=255.255.255.0
gw=
dns=
hostname=ufi-labor
port=55100
lan_write=0
lan_key=
```

Ein leerer `lan_key` bedeutet **keine Authentifizierung**. Für einen gemeinsamen Netzbereich einen eigenen Schlüssel mit höchstens 64 Zeichen ohne Leerzeichen oder `#` konfigurieren. Es wird bewusst kein allgemeingültiger Beispielschlüssel vorgegeben.

Bei fester IP statt `dhcp` die tatsächlich reservierte Adresse und passende Maske, Gateway und DNS eintragen. Einstellungen werden beim Start und Konfigurations-Neuladen verarbeitet. LAN.CFG über USB bzw. im SD-Modus pflegen; Lesen dieser Datei über LAN ist gesperrt.

## Vom Host verbinden

`192.0.2.10` ist eine Dokumentationsadresse und muss ersetzt werden:

```console
ufi --host 192.0.2.10 info
ufi --host 192.0.2.10 status
```

Bei gesetztem Schlüssel zusätzlich `--lan-key` mit dem eigenen Schlüssel verwenden. Die Challenge nutzt HMAC-SHA256; der Transport ist dadurch nicht verschlüsselt. LAN daher im vertrauenswürdigen lokalen Netz betreiben.

## Besitz und Schreibrechte

Die Firmware lässt eine TCP-Sitzung gleichzeitig zu. Laufwerkszugriffe sind außerdem mit USB koordiniert. `busy` kann bedeuten, dass USB das Laufwerk besitzt oder eine andere Operation läuft.

`lan_write=1` erlaubt entsprechende LAN-Schreibbefehle nur zusammen mit den weiteren Freigaben. Bootloaderwechsel und USB-Moduswechsel bleiben über LAN gesperrt. Keine Ports zum öffentlichen Internet weiterleiten.

Quelle: [LAN-Konfiguration](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_lan_cfg.c), [LAN-Sitzung](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_lan_session.c).
