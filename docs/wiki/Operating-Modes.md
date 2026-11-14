# Operating modes

**Sprache / Language:** [Deutsch](Betriebsarten) | [English](Operating-Modes)

UFI can enumerate as different USB devices. Changing mode disconnects the previous connection; applications must reopen the appropriate interface.

| Mode | Purpose | Host access |
|---|---|---|
| `flux` | UFI protocol, raw capture, analysis and writing | `ufi`, USB CDC |
| `gw` | Greaseweazle-compatible protocol | Greaseweazle tools |
| `composite` | Greaseweazle plus a separate native UFI interface | Greaseweazle and `ufi --port usb`, with ownership coordination |
| `sd` | Internal storage as USB mass storage | PC filesystem |
| `floppy` | Disk as a USB block device | PC operating system |

## Change mode from the host

From an accessible UFI mode, for example:

```console
ufi mode sd
```

The old CDC connection is then unavailable to `ufi`. Eject mass storage on the PC before switching back. The mode switch or documented button procedure provides a return path when no native command interface is accessible.

`protocol=ufi` or `protocol=gw` selects the flux protocol associated with the corresponding switch position. See [SD and standalone](SD-and-Standalone).

## USB floppy

In `floppy`, the inserted disk appears as a block device. Firmware maps supported sector formats to physical disk access. Use this mode for ordinary filesystem operations, and `flux` for raw flux and unusual protection structures.

Mounting a filesystem can itself trigger writes. Keep physical write protection enabled for original media. Host filesystem changes may also be written back to disk after a delay.

## USB and LAN together

An active session owns the shared drive resources. Other transports must not independently change the head position or flux store. On `busy`, inspect active operations and the owner using `ufi net` and status queries.

Next: [Greaseweazle](Greaseweazle-EN) · [LAN](LAN-EN) · [Troubleshooting](Troubleshooting).

Reference: [USB composite device and modes, German](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md).
