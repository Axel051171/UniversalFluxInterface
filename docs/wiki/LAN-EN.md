# LAN

**Sprache / Language:** [Deutsch](LAN) | [English](LAN-EN)

Board v1.0 has a W5500 and RJ45 connector. The firmware implements address configuration, discovery and a UFI v2 session over TCP. A physical LAN cable test remains outstanding in the documented qualification setup.

## Status and discovery

Start over USB:

```console
ufi net
ufi discover --timeout 2
```

Discovery uses UDP port 55100. The session defaults to TCP port 55100. The firewall must allow the required traffic. A link-local fallback is provided when no DHCP server is available; both endpoints must then be on the appropriate local network.

## LAN.CFG

This file is stored internally. Example using DHCP with LAN writes disabled:

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

An empty `lan_key` means **no authentication**. On a shared network, configure a personal key of at most 64 characters, without spaces or `#`. No universal example key is supplied.

For a static IP, replace `dhcp` with the reserved address and supply the correct mask, gateway and DNS. Settings are processed at startup and when configuration is reloaded. Maintain LAN.CFG through USB or SD mode; reading it over LAN is blocked.

## Connect from the host

`192.0.2.10` is a documentation address and must be replaced:

```console
ufi --host 192.0.2.10 info
ufi --host 192.0.2.10 status
```

If a key is configured, also supply your key using `--lan-key`. The challenge uses HMAC-SHA256; this does not encrypt the transport. Operate LAN within a trusted local network.

## Ownership and write permissions

The firmware permits one TCP session at a time. Drive access is also coordinated with USB. `busy` can indicate that USB owns the drive or that another operation is active.

`lan_write=1` enables the corresponding LAN write commands only together with the other required permissions. Bootloader entry and USB mode changes remain blocked over LAN. Do not forward these ports to the public internet.

Sources: [LAN configuration](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_lan_cfg.c), [LAN session](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_lan_session.c).
