# UFI Boot Control Hardware

## STM32H723 Boot Control via CM5 GPIO

The CM5 needs to control the STM32 for:
1. **Reset** - Restart the flux engine
2. **BOOT0** - Force USB DFU mode for firmware updates
3. **Status** - Read STM32 ready state

---

## Schematic Addition: Boot Control Circuit

```
                                    STM32H723
                                   ┌─────────┐
CM5 GPIO17 ───[10k]───┬──────────►│ NRST    │
(Reset)               │           │         │
                    [100nF]       │         │
                      │           │         │
                     GND          │         │
                                  │         │
CM5 GPIO27 ───[10k]───┬──────────►│ BOOT0   │
(Boot)                │           │         │
                    [10k]         │         │
                      │           │         │
                     GND          │         │
                                  │         │
CM5 GPIO22 ◄──────────────────────│ PA15    │
(Ready)                           │ (Ready) │
                                  └─────────┘
```

### Component Values

| Component | Value | Purpose |
|-----------|-------|---------|
| R_RST | 10kΩ | Reset pull-up |
| C_RST | 100nF | Debounce/filter |
| R_BOOT_PU | 10kΩ | Boot0 pull-down (normal boot) |
| R_BOOT_DRV | 10kΩ | Series resistor for GPIO drive |

---

## GPIO Pinout on CM5

| CM5 GPIO | Function | Direction | Description |
|----------|----------|-----------|-------------|
| GPIO17 | STM32_RESET | Output | Active-low reset |
| GPIO27 | STM32_BOOT0 | Output | High = DFU mode |
| GPIO22 | STM32_READY | Input | High = Ready |
| GPIO23 | STM32_IRQ | Input | Interrupt from STM32 |

---

## Boot Sequence

### Normal Boot (Power-On)

```
1. Power applied
2. BOOT0 = LOW (pulled down by R_BOOT_PU)
3. STM32 boots from internal Flash
4. STM32 sets READY high
5. CM5 detects USB device (VID:1209 PID:4F54)
6. UFI daemon starts communication
```

### Firmware Update (DFU Mode)

```
1. CM5 sets GPIO27 (BOOT0) = HIGH
2. CM5 pulses GPIO17 (RESET) LOW for 10ms
3. CM5 releases RESET (HIGH)
4. STM32 enters USB DFU bootloader
5. CM5 detects DFU device (VID:0483 PID:DF11)
6. dfu-util uploads new firmware
7. CM5 sets GPIO27 (BOOT0) = LOW
8. CM5 pulses GPIO17 (RESET) to restart
9. STM32 boots with new firmware
```

---

## Linux GPIO Control

### Device Tree Overlay

```dts
// ufi-gpio.dtso
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2712";

    fragment@0 {
        target-path = "/";
        __overlay__ {
            ufi_gpios {
                compatible = "gpio-leds";
                
                stm32-reset {
                    gpios = <&gpio 17 1>;  // Active low
                    label = "ufi:stm32:reset";
                    default-state = "off";
                };
            };
        };
    };

    fragment@1 {
        target = <&gpio>;
        __overlay__ {
            ufi_pins: ufi_pins {
                brcm,pins = <17 22 23 27>;
                brcm,function = <1 0 0 1>;  // Out, In, In, Out
                brcm,pull = <0 2 2 0>;      // None, Up, Up, None
            };
        };
    };
};
```

### Python Control Script

```python
#!/usr/bin/env python3
"""UFI STM32 Boot Control"""

import gpiod
import time
import usb.core

# GPIO chip and lines
CHIP = "gpiochip0"
GPIO_RESET = 17
GPIO_BOOT0 = 27
GPIO_READY = 22

# USB IDs
UFI_VID = 0x1209
UFI_PID = 0x4F54
DFU_VID = 0x0483
DFU_PID = 0xDF11


class STM32Control:
    def __init__(self):
        self.chip = gpiod.Chip(CHIP)
        self.reset_line = self.chip.get_line(GPIO_RESET)
        self.boot0_line = self.chip.get_line(GPIO_BOOT0)
        self.ready_line = self.chip.get_line(GPIO_READY)
        
        # Configure outputs
        self.reset_line.request(consumer="ufi", type=gpiod.LINE_REQ_DIR_OUT, 
                                default_val=1)  # Inactive (high)
        self.boot0_line.request(consumer="ufi", type=gpiod.LINE_REQ_DIR_OUT,
                                default_val=0)  # Normal boot (low)
        # Configure input
        self.ready_line.request(consumer="ufi", type=gpiod.LINE_REQ_DIR_IN)
    
    def reset(self):
        """Perform hardware reset"""
        self.reset_line.set_value(0)  # Assert reset
        time.sleep(0.01)              # 10ms
        self.reset_line.set_value(1)  # Release reset
        time.sleep(0.1)               # Wait for boot
    
    def enter_dfu_mode(self):
        """Enter USB DFU bootloader mode"""
        self.boot0_line.set_value(1)  # BOOT0 high
        self.reset()                   # Reset into DFU
        time.sleep(0.5)               # Wait for USB enumeration
        
        # Verify DFU device appeared
        dev = usb.core.find(idVendor=DFU_VID, idProduct=DFU_PID)
        if dev is None:
            raise RuntimeError("DFU device not found")
        return True
    
    def exit_dfu_mode(self):
        """Exit DFU mode and boot normally"""
        self.boot0_line.set_value(0)  # BOOT0 low
        self.reset()                   # Reset to normal boot
        
        # Wait for UFI device
        for _ in range(50):  # 5 second timeout
            dev = usb.core.find(idVendor=UFI_VID, idProduct=UFI_PID)
            if dev is not None:
                return True
            time.sleep(0.1)
        raise RuntimeError("UFI device not found after reset")
    
    def is_ready(self):
        """Check if STM32 is ready"""
        return self.ready_line.get_value() == 1
    
    def wait_ready(self, timeout=5.0):
        """Wait for STM32 to become ready"""
        start = time.time()
        while time.time() - start < timeout:
            if self.is_ready():
                return True
            time.sleep(0.05)
        return False
    
    def close(self):
        self.reset_line.release()
        self.boot0_line.release()
        self.ready_line.release()


def main():
    import argparse
    parser = argparse.ArgumentParser(description="UFI STM32 Control")
    parser.add_argument("command", choices=["reset", "dfu", "status"])
    args = parser.parse_args()
    
    ctrl = STM32Control()
    try:
        if args.command == "reset":
            print("Resetting STM32...")
            ctrl.reset()
            if ctrl.wait_ready():
                print("STM32 ready")
            else:
                print("STM32 not responding")
                
        elif args.command == "dfu":
            print("Entering DFU mode...")
            ctrl.enter_dfu_mode()
            print("DFU mode active. Use dfu-util to upload firmware.")
            
        elif args.command == "status":
            ready = ctrl.is_ready()
            print(f"STM32 Ready: {ready}")
            
            # Check USB
            ufi = usb.core.find(idVendor=UFI_VID, idProduct=UFI_PID)
            dfu = usb.core.find(idVendor=DFU_VID, idProduct=DFU_PID)
            
            if ufi:
                print("USB: UFI device connected")
            elif dfu:
                print("USB: DFU bootloader active")
            else:
                print("USB: No device found")
                
    finally:
        ctrl.close()


if __name__ == "__main__":
    main()
```

---

## Carrier Board Connections

### CM5 GPIO Header (directly directly directly directly directly directly directly directly directly directly directly 40-pin HAT compatible)

```
Pin | GPIO | Function      | Direction
----|------|---------------|----------
 11 | 17   | STM32_RESET   | Out
 13 | 27   | STM32_BOOT0   | Out  
 15 | 22   | STM32_READY   | In
 16 | 23   | STM32_IRQ     | In
```

### Recommended PCB Routing

1. Keep GPIO traces short (<50mm)
2. Add 100Ω series resistor on RESET for ESD
3. Add ESD diodes on all GPIO lines
4. Use ground plane under GPIO traces

---

## Fallback: Manual DFU Entry

If GPIO control fails, provide physical access:

### Option 1: Boot Button
- Tactile switch connecting BOOT0 to VDD
- Hold during power-on or reset

### Option 2: DFU Jumper
- 2-pin header with jumper
- Short to force DFU mode

### Schematic

```
        VDD (3.3V)
           │
           ┤ SW1 (Tactile)
           │
 GPIO27 ───┼───[10k]──── BOOT0
           │
         [10k]
           │
          GND
```

---

## USB Connection Details

### Internal USB HS Connection

```
CM5 USB3.0 Port 0          STM32H723 USB HS
┌─────────────┐            ┌─────────────┐
│  USB0_DP   ─┼────────────┼─ PA12/DP    │
│  USB0_DM   ─┼────────────┼─ PA11/DM    │
│  GND       ─┼────────────┼─ GND        │
│  VBUS (5V) ─┼────────────┼─ VBUS sense │
└─────────────┘            └─────────────┘
```

### USB Descriptor (Firmware)

```c
// UFI USB Descriptor
#define UFI_VID         0x1209  // pid.codes VID
#define UFI_PID         0x4F54  // "OT" - Open Tool (registered)
#define UFI_VERSION     0x0100  // 1.0.0

static const uint8_t device_descriptor[] = {
    0x12,                       // bLength
    0x01,                       // bDescriptorType (Device)
    0x00, 0x02,                 // bcdUSB 2.00
    0xFF,                       // bDeviceClass (Vendor Specific)
    0x00,                       // bDeviceSubClass
    0x00,                       // bDeviceProtocol
    0x40,                       // bMaxPacketSize0 64
    UFI_VID & 0xFF, UFI_VID >> 8,  // idVendor
    UFI_PID & 0xFF, UFI_PID >> 8,  // idProduct
    UFI_VERSION & 0xFF, UFI_VERSION >> 8,  // bcdDevice
    0x01,                       // iManufacturer
    0x02,                       // iProduct
    0x03,                       // iSerialNumber
    0x01                        // bNumConfigurations
};
```
