# UFI Software Architecture

## Overview

The UFI consists of two processing units that need software:

1. **Raspberry Pi CM5** - Main host running Linux
2. **STM32H723** - Flux engine running bare-metal firmware

---

## 1. CM5 Operating System

### Recommended: Raspberry Pi OS Lite (64-bit)

```bash
# Download and flash using Raspberry Pi Imager
# Or download directly:
wget https://downloads.raspberrypi.com/raspios_lite_arm64/images/raspios_lite_arm64-2024-11-19/2024-11-19-raspios-bookworm-arm64-lite.img.xz
```

### Alternative: Ubuntu Server 24.04 LTS

```bash
# For Ubuntu on CM5
wget https://cdimage.ubuntu.com/releases/24.04/release/ubuntu-24.04-preinstalled-server-arm64+raspi.img.xz
```

### Boot Configuration (CM5 Carrier Board)

The CM5 uses EEPROM-based boot configuration. Key settings in `/boot/firmware/config.txt`:

```ini
# UFI-specific configuration

# Enable USB Host for STM32 connection
dtoverlay=dwc2,dr_mode=host

# Enable hardware UART for debug
enable_uart=1

# PCIe for NVMe SSD
dtparam=pciex1
dtparam=pciex1_gen=3

# Disable unused interfaces to reduce power
dtoverlay=disable-bt
dtoverlay=disable-wifi  # If using Ethernet only

# Fan control (if using active cooling)
dtoverlay=gpio-fan,gpiopin=14,temp=55000

# Status LED control
dtparam=act_led_trigger=heartbeat
```

---

## 2. UFT Software Installation

### Option A: Pre-built Image (Recommended for Users)

Download a ready-to-use UFI image with everything pre-installed:

```bash
# Flash to eMMC or microSD
wget https://github.com/YOUR_ORG/ufi-images/releases/latest/download/ufi-os.img.xz
xzcat ufi-os.img.xz | sudo dd of=/dev/sdX bs=4M status=progress
```

### Option B: Manual Installation

```bash
# 1. Update system
sudo apt update && sudo apt upgrade -y

# 2. Install dependencies
sudo apt install -y \
    build-essential \
    cmake \
    git \
    libusb-1.0-0-dev \
    libudev-dev \
    python3-pip \
    python3-venv \
    dfu-util \
    openocd

# 3. Clone UFT repository
git clone https://github.com/YOUR_ORG/uft.git
cd uft

# 4. Build UFT
mkdir build && cd build
cmake ..
make -j4
sudo make install

# 5. Install Python tools
pip3 install --user greaseweazle  # For compatibility
pip3 install --user hxcfe         # HxC tools

# 6. Setup udev rules for USB access
sudo cp udev/99-ufi.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

---

## 3. STM32H723 Firmware

### Firmware Location

The STM32 firmware is stored on the CM5 and uploaded via USB DFU on boot:

```
/opt/ufi/firmware/
├── ufi-flux-engine.bin      # Main firmware
├── ufi-flux-engine.elf      # Debug symbols
└── bootloader.bin           # USB DFU bootloader
```

### Initial Flashing (via SWD)

For first-time programming or recovery, use SWD:

```bash
# Using OpenOCD
openocd -f interface/cmsis-dap.cfg \
        -f target/stm32h7x.cfg \
        -c "program ufi-flux-engine.bin 0x08000000 verify reset exit"

# Or using ST-Link
st-flash write ufi-flux-engine.bin 0x08000000
```

### Runtime Upload (USB DFU)

Normal operation uses USB DFU for firmware updates:

```bash
# Put STM32 in DFU mode (BOOT0 high + reset)
# Or software trigger via USB command

# Upload firmware
dfu-util -a 0 -D ufi-flux-engine.bin -s 0x08000000:leave

# Or use built-in UFI tool
ufi firmware update /opt/ufi/firmware/ufi-flux-engine.bin
```

### Firmware Auto-Update on Boot

The UFI daemon checks firmware version on startup:

```python
# /opt/ufi/scripts/check_firmware.py
import usb.core
import subprocess

EXPECTED_VERSION = "1.0.0"

def check_and_update():
    # Find STM32 device
    dev = usb.core.find(idVendor=0x1209, idProduct=0x4F54)  # UFI VID/PID
    
    if dev is None:
        # Device in DFU mode, upload firmware
        subprocess.run(["dfu-util", "-a", "0", "-D", 
                       "/opt/ufi/firmware/ufi-flux-engine.bin"])
        return
    
    # Check version
    version = dev.ctrl_transfer(0xC0, 0x01, 0, 0, 16).tobytes().decode()
    
    if version != EXPECTED_VERSION:
        # Trigger DFU mode and update
        dev.ctrl_transfer(0x40, 0x02, 0, 0)  # DFU trigger command
        time.sleep(1)
        subprocess.run(["dfu-util", "-a", "0", "-D",
                       "/opt/ufi/firmware/ufi-flux-engine.bin"])
```

---

## 4. UFI Daemon Service

### Systemd Service

```ini
# /etc/systemd/system/ufi.service
[Unit]
Description=UFI Flux Interface Daemon
After=network.target

[Service]
Type=simple
ExecStartPre=/opt/ufi/scripts/check_firmware.py
ExecStart=/usr/local/bin/ufid --config /etc/ufi/config.yaml
Restart=always
RestartSec=5
User=ufi
Group=ufi

[Install]
WantedBy=multi-user.target
```

### Configuration File

```yaml
# /etc/ufi/config.yaml
daemon:
  port: 8080
  bind: 0.0.0.0

usb:
  vid: 0x1209
  pid: 0x4F54

storage:
  flux_path: /data/flux
  temp_path: /tmp/ufi

drives:
  - name: "Shugart 0"
    type: shugart
    port: 0
  - name: "Shugart 1"
    type: shugart
    port: 1
  - name: "Apple Disk II"
    type: apple2
    port: 2
  - name: "Amiga External"
    type: amiga_ext
    port: 3
  - name: "IEC Bus"
    type: iec
    port: 4

web:
  enabled: true
  auth: false  # Enable for production
```

---

## 5. Web Interface

### Built-in Web UI

Access via browser at `http://ufi.local:8080` or `http://<IP>:8080`

Features:
- Drive selection and status
- Read/Write disk operations
- Format conversion
- Firmware updates
- System configuration

### REST API

```bash
# List drives
curl http://ufi.local:8080/api/drives

# Read disk to SCP format
curl -X POST http://ufi.local:8080/api/read \
     -H "Content-Type: application/json" \
     -d '{"drive": 0, "format": "scp", "tracks": "0-79", "sides": "0-1"}'

# Download flux file
curl http://ufi.local:8080/api/files/disk001.scp -o disk001.scp

# IEC directory listing
curl http://ufi.local:8080/api/iec/directory?device=8
```

---

## 6. Network Boot (Advanced)

For production environments or diskless operation:

### PXE Boot Setup

```bash
# On TFTP/DHCP server
# Configure DHCP option 66 (TFTP server) and 67 (boot file)

# Boot files structure
/tftpboot/
├── bootcode.bin
├── start4.elf
├── fixup4.dat
├── config.txt
├── cmdline.txt
└── kernel8.img

# cmdline.txt for NFS root
console=serial0,115200 root=/dev/nfs nfsroot=192.168.1.100:/nfs/ufi,vers=4 rw ip=dhcp
```

### Read-Only Root with Overlay

For reliability (prevents SD card corruption):

```bash
# Enable overlayfs
sudo raspi-config nonint do_overlayfs 0

# Or manually in /boot/firmware/cmdline.txt
# Add: boot=overlay
```

---

## 7. Update Mechanism

### OTA Updates

```bash
# Check for updates
ufi update check

# Download and install
ufi update install

# Manual update from file
ufi update install --file ufi-update-1.2.0.tar.gz
```

### Update Package Structure

```
ufi-update-1.2.0.tar.gz
├── manifest.json
├── firmware/
│   └── ufi-flux-engine.bin
├── software/
│   ├── ufi-daemon
│   └── ufi-cli
├── web/
│   └── static/
└── scripts/
    ├── pre-install.sh
    └── post-install.sh
```

---

## 8. Development Setup

### Cross-Compilation for CM5

```bash
# Install ARM64 toolchain
sudo apt install gcc-aarch64-linux-gnu

# Build UFT for ARM64
mkdir build-arm64 && cd build-arm64
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-linux-gnu.cmake ..
make -j$(nproc)
```

### STM32 Firmware Development

```bash
# Install ARM Embedded Toolchain
sudo apt install gcc-arm-none-eabi

# Clone firmware repo
git clone https://github.com/YOUR_ORG/ufi-firmware.git
cd ufi-firmware

# Build
make

# Flash via OpenOCD
make flash
```

---

## 9. Quick Start Summary

### First Boot Checklist

1. ✅ Flash Raspberry Pi OS to eMMC/SD
2. ✅ Configure WiFi/Ethernet (headless: `wpa_supplicant.conf`)
3. ✅ Enable SSH (`ssh` file in boot partition)
4. ✅ Boot and connect via SSH
5. ✅ Run UFI installer script
6. ✅ STM32 firmware auto-uploads
7. ✅ Access web UI at `http://ufi.local:8080`

### One-Line Installer

```bash
curl -sSL https://raw.githubusercontent.com/YOUR_ORG/ufi/main/install.sh | sudo bash
```

---

## 10. Troubleshooting

### STM32 Not Detected

```bash
# Check USB devices
lsusb | grep -i "1209:4f54\|0483:df11"

# Check dmesg for USB errors
dmesg | grep -i usb

# Reset STM32 via GPIO (optional hardware line)
gpioset gpiochip0 17=1 && sleep 0.1 && gpioset gpiochip0 17=0
```

### Firmware Upload Failed

```bash
# Manual DFU mode entry
# 1. Hold BOOT0 button (directly connect to STM32, directly connect to button on carrier)
# 2. Press RESET
# 3. Release BOOT0

# Then upload
dfu-util -l  # List DFU devices
dfu-util -a 0 -D firmware.bin -s 0x08000000:leave
```

### Web Interface Not Accessible

```bash
# Check daemon status
sudo systemctl status ufi

# Check port binding
sudo netstat -tlnp | grep 8080

# Check firewall
sudo ufw status
```
