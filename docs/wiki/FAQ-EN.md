# Frequently asked questions

**Sprache / Language:** [Deutsch](FAQ) | [English](FAQ-EN)

## Can I switch languages?

Yes. At the top of every page, “Deutsch | English” opens the same guide in the other language. Content and annotated board illustrations are available in both languages. Choose a language by clicking; it does not switch automatically based on browser language. Technical source documents in the main repository may still be in German.

## What advantages does UFI offer over other flux controllers?

Primarily, its combination of several drive connectors, standalone storage, LAN, power monitoring and open firmware. A finer native timer grid can also help with timing analysis. [Benefits and comparison](Benefits-and-Comparison) discusses KryoFlux, Greaseweazle, SuperCard Pro, Applesauce+ and FluxEngine with sources. Higher recovery rates or speed have not yet been demonstrated.

## Do I need a Raspberry Pi?

No. The current Headless design uses the STM32H723. Older CM5/module concepts belong to the project's history.

## Is USB power sufficient?

Yes for the documented USB board bring-up. Drives need their appropriate supply; in particular, the USB cable alone does not provide 12 V. See [connections](Connections).

## Is this USB High Speed?

The board uses the internal Full-Speed PHY. The STM32 peripheral's name does not indicate the actual connection speed.

## Have all listed formats already been tested with drives?

No. Many paths are implemented and tested offline. Outstanding hardware measurements are listed under [project status](Project-Status).

## Is an SCP file lossless relative to the native capture?

The host exports SCP in 25 ns units. The native timebase is finer. Use `.ufi` for native ticks and retained protocol frames; see [file formats](File-Formats).

## Does verification always mean identical data?

No. Read the relevant evidence level first. Output completed, plausible read-back and content comparison are different statements. See [writing and verification](Writing-and-Verification).

## Does a weak-region analysis prove copy protection?

No. Observed differences can also result from the medium, dirt, the drive or rotational speed variations. The analysis describes unstable regions in the examined capture.

## Does an Apple drive work on the Mac connector?

Ports J14 and J23 serve different purposes. Connector, power and control must match the specific drive. See [Apple and Macintosh](Apple-and-Macintosh).

## Why does ufi stop responding after a mode change?

SD, Floppy and pure Greaseweazle modes do not provide the normal UFI CDC access. USB enumerates again. The access tool must match the operating mode.

## Can I release a session write lock?

`write-lock engage` remains effective until MCU reset. There is no USB command to release it. Also check hardware write protection and the state after reset.

## What should an issue report include?

The exact command, firmware revision, board revision, drive, medium, power supply, cable, status and diagnostic report. Remove keys and other private information before publishing. See [troubleshooting](Troubleshooting).
