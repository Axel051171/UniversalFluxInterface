# Greaseweazle

**Sprache / Language:** [Deutsch](Greaseweazle) | [English](Greaseweazle-EN)

UFI provides a Greaseweazle-compatible protocol mode, allowing Greaseweazle tools to operate the board. This is a separate transport with its own command set; native UFI analysis functions do not automatically become Greaseweazle features.

## Select the mode

From UFI mode:

```console
ufi mode gw
```

Alternatively, set `protocol=gw` in `UFI.CFG`. The board enumerates again after switching. Pure GW mode uses VID/PID `1209:4F57`.

Install Greaseweazle software according to its own instructions. For an initial check:

```console
gw info
gw read --help
```

Drive and format parameters depend on the installed Greaseweazle version. Before writing, also check the UFI [write permissions](Writing-and-Verification).

## Composite mode

In `composite` mode, Greaseweazle and a separate native UFI bulk interface are available. For the latter, install the USB extra and use:

```console
ufi --port usb info
```

One interface must not interrupt the other by resetting or reconfiguring the entire USB device. Firmware coordinates ownership of drive resources. Two applications must not expect independent simultaneous reads or writes on the same drive.

## Switching back

Pure GW mode does not provide a normal UFI CDC port for `ufi mode flux`. To return, use the intended hardware mode selector or adjust the configuration through SD mode.

References: [UFI GW implementation](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_gw.c), [Greaseweazle project](https://github.com/keirf/greaseweazle).
