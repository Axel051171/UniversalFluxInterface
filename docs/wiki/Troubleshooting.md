# Troubleshooting

**Sprache / Language:** [Deutsch](Fehlerhilfe) | [English](Troubleshooting)

First check the connection, operating mode, firmware and board status. Repeated writes are not a useful way to diagnose an unresolved drive fault.

## Basic information

```console
ufi info
ufi status
ufi psram
ufi diagnostics
ufi features
ufi net
```

Include these outputs with the exact command, board revision, drive model, power supply, cable and expected result when reporting a problem. Remove secrets from LAN.CFG and command lines.

## Common symptoms

| Symptom | Useful next step |
|---|---|
| No device found | Check the USB data cable, port and mode; GW, SD and Floppy modes have no normal UFI CDC port |
| Multiple devices found | Explicitly select the COM/device port or USB serial number |
| PSRAM unavailable | Read the startup message and `ufi psram`; the fallback buffer has much less capacity |
| No index | Check motor, drive selection and INDEX; Disk II needs a sensor or time slicing |
| Seek failed | Check TRK0, direction, step timing and permitted geometry |
| Buffer full | Check capture parameters, data rate and host consumption; account for the fallback buffer |
| `busy` | Check the active job, USB/LAN owner and composite resources |
| No LAN link | Check cable, peer, W5500 detection and `lan=on` |
| DFU does not appear | Documented open issue; use the demonstrated SWD path |

`ufi psram retest` repeats memory startup/write tests. Save the original diagnostics first and ensure that important capture data is no longer needed.

## Common status codes

| Code | Meaning | Response |
|---|---|---|
| 1 | Busy | Check ownership and active job |
| 3 | Seek failed | Check head position and sensors |
| 4 | No index | Check selection, motor and INDEX/sync source |
| 5 | Timeout | Check connection and expected events |
| 6 | DMA/verification error | Save diagnostic counters and report |
| 10 | Buffer full / overflow | Treat the capture as incomplete |
| 11 | Not implemented / unsupported combination | Check capabilities and backend |
| 12 | Write protected | Check front switch and drive write protection |
| 14 | Frame/CRC error | Check transport and protocol |
| 16 | Invalid arguments | Check `ufi refusal`, command mask and parameters |
| 18 | Write interval out of range | Choose a suitable path; do not blindly clamp intervals |
| 19 | Track longer than available index window | Check timing, speed and track length |
| 20 | Aborted | Check cancellation cause and resulting state |
| 21 | Media changed | Check identity and prepare the job again |
| 22 | Session write lock | Remains engaged until MCU reset |
| 23 | Rotational speed mismatch | Compare source period and actual speed |
| 24–26 | LAN access / authentication | Check access type and key |

These are UFI protocol statuses, not operating-system error codes. Complete definition: [ufi_fixes.h](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/include/ufi_fixes.h).

## After cancellation

`ufi abort` stops an active job if the device is reachable. Then check status, motor state and write lock. Keep hardware write protection enabled for original media. Reset only after saving any required diagnostics and retained capture data.

Report issues: [GitHub Issues](https://github.com/Axel051171/UniversalFluxInterface/issues).
