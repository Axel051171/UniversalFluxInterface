# UFI - Universal Flux Interface

<p align="center">
  <img src="docs/images/ufi_logo.png" alt="UFI Logo" width="200">
</p>

<p align="center">
  <strong>Professional Floppy Disk & Cartridge Preservation Platform</strong>
</p>

<p align="center">
  <a href="#features">Features</a> •
  <a href="#hardware">Hardware</a> •
  <a href="#firmware">Firmware</a> •
  <a href="#modules">Modules</a> •
  <a href="#getting-started">Getting Started</a>
</p>

---

## Overview

UFI is a professional-grade preservation platform combining a **Raspberry Pi CM5** with an **STM32H723** flux capture engine. Designed for archivists, collectors, and retro enthusiasts who need reliable, high-resolution disk and cartridge preservation.

### Key Specifications

| Parameter | Value |
|-----------|-------|
| Flux Resolution | **10 ns** |
| MCU | STM32H723ZGT6 @ 550 MHz |
| Host | Raspberry Pi CM5 |
| USB | High-Speed 480 Mbit/s |
| Network | Gigabit Ethernet |

---

## Features

### Drive Interfaces

| System | Connector | Status |
|--------|-----------|--------|
| PC/DOS | 34-pin Shugart | ✅ |
| Amiga | DB-23 | ✅ |
| Apple II | 19-pin | ✅ |
| C64 IEC | DIN-6 | ✅ |

### Capabilities

- ✅ High-resolution flux capture (10ns)
- ✅ Write support with precompensation
- ✅ Copy protection analysis
- ✅ Web-based interface
- ✅ Modular expansion system

---

## Hardware

### PCB Designs (KiCad 8)

| Board | Size | Layers | Description |
|-------|------|--------|-------------|
| UFI Mainboard | 150×100mm | 4 | Full-featured with HDMI |
| UFI Headless | 110×85mm | 4 | Network-only, compact |
| UFI Compact | 110×85mm | 4 | Reduced feature set |
| GB/GBA Dumper | 80×50mm | 2 | Standalone cartridge dumper |
| MIG Dumper | 100×60mm | 2 | EPROM/Mask ROM reader |

### Directory Structure

```
kicad/
├── *.kicad_sch          # Schematics
├── *.kicad_pcb          # PCB layouts
├── footprints/          # Custom footprints (16)
├── symbols/             # Custom symbols
└── production/          # BOM & CPL for assembly
```

---

## Firmware

**4,700+ lines of C** for the STM32H723 flux engine.

### Modules

| Module | Lines | Function |
|--------|-------|----------|
| `ufi_main.c` | 617 | Main loop, initialization |
| `ufi_flux.c` | 288 | DMA-based flux capture |
| `ufi_write.c` | 329 | Write with precompensation |
| `ufi_drive.c` | 471 | Multi-interface drive control |
| `ufi_iec.c` | 485 | C64 IEC serial protocol |
| `ufi_usb.c` | 481 | USB CDC communication |
| `ufi_debug.c` | 352 | Diagnostics & testing |

### Building

```bash
cd firmware
mkdir build && cd build
cmake -G Ninja ..
ninja
```

### Flashing

```bash
st-flash write ufi_firmware.bin 0x08000000
```

---

## Modules

Expansion modules connect via the 40-pin header.

### GB/GBA USB Dumper

Standalone Game Boy cartridge dumper.

- **MCU:** RP2040 @ 133 MHz
- **Supports:** GB, GBC, GBA (up to 32MB)
- **MBC:** All types supported
- **Cost:** ~15€

### MIG Dumper

Universal EPROM/Mask ROM reader.

- **MCU:** STM32G0B1 @ 64 MHz
- **Supports:** 2716 to 27C322 (2KB-4MB)
- **Socket:** ZIF-40
- **Cost:** ~13€

### Switch Cartridge *(Specification only)*

Nintendo Switch cartridge interface.

---

## Getting Started

### Requirements

- KiCad 8.0+
- ARM GCC Toolchain
- CMake 3.20+
- STM32CubeH7 HAL

### Clone

```bash
git clone https://github.com/Axel051171/UniversalFluxInterface.git
cd UniversalFluxInterface
```

### Open in KiCad

```bash
kicad kicad/UFI_Headless.kicad_pro
```

### Build Firmware

```bash
cd firmware
mkdir build && cd build
cmake ..
make -j4
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [System Architecture](docs/System_Architecture.md) | Overall system design |
| [USB Protocol](docs/USB_Protocol_Specification.md) | Command reference |
| [API Documentation](docs/API_Documentation.md) | Software API |
| [Manufacturing Guide](docs/Manufacturing_Guide.md) | PCB production |
| [Hardware Testing](docs/Hardware_Test_Procedures.md) | Test procedures |

---

## Project Status

| Component | Status |
|-----------|--------|
| Firmware | ✅ Complete |
| KiCad Schematics | ✅ Complete |
| PCB Layouts | ✅ Complete |
| GB/GBA Module | ✅ Complete |
| MIG Module | ✅ Complete |
| CM5 Software | 🔄 In Progress |
| Web Interface | 🔄 In Progress |

---

## License

MIT License - see [LICENSE](LICENSE)

---

## Contributing

Contributions welcome! See [CONTRIBUTING.md](CONTRIBUTING.md)

---

## Acknowledgments

- Greaseweazle, KryoFlux, Applesauce - Inspiration
- C64 preservation community
- Raspberry Pi Foundation

---

<p align="center">
  <em>Preserving digital history, one flux transition at a time.</em>
</p>
