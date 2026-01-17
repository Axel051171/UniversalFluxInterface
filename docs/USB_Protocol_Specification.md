# UFI USB Communication Protocol Specification

**Version:** 1.0  
**Date:** 2026-01-16  
**Status:** Draft

---

## 1. Overview

The UFI uses USB High-Speed (480 Mbit/s) for communication between the Raspberry Pi CM5 (host) and the STM32H723 (device). The protocol is designed for:

- High-throughput flux data transfer
- Low-latency control commands
- Reliable error handling
- Extensibility for future features

```
┌──────────────────┐                    ┌──────────────────┐
│  Raspberry Pi    │                    │    STM32H723     │
│      CM5         │                    │   Flux Engine    │
│                  │                    │                  │
│  ┌────────────┐  │      USB HS       │  ┌────────────┐  │
│  │    UFT     │  │    (480 Mbit/s)   │  │  Firmware  │  │
│  │  Software  │◄─┼───────────────────┼─►│            │  │
│  └────────────┘  │                    │  └────────────┘  │
│                  │  Bulk Endpoints    │                  │
│                  │  EP1 OUT (Cmd)     │                  │
│                  │  EP1 IN  (Resp)    │                  │
│                  │  EP2 IN  (Flux)    │                  │
│                  │  EP3 OUT (Write)   │                  │
└──────────────────┘                    └──────────────────┘
```

---

## 2. USB Descriptors

### 2.1 Device Descriptor

```c
Device Descriptor:
  bLength                 18
  bDescriptorType          1 (Device)
  bcdUSB               2.00  (USB 2.0 High-Speed)
  bDeviceClass          0xFF (Vendor Specific)
  bDeviceSubClass       0x00
  bDeviceProtocol       0x00
  bMaxPacketSize0         64
  idVendor            0x1209 (pid.codes)
  idProduct           0x4F54 ("OT" - Open Tool)
  bcdDevice            1.00
  iManufacturer           1  "UFI Project"
  iProduct                2  "Universal Flux Interface"
  iSerialNumber           3  <unique per device>
  bNumConfigurations      1
```

### 2.2 Configuration Descriptor

```c
Configuration Descriptor:
  bLength                  9
  bDescriptorType          2 (Configuration)
  wTotalLength           46
  bNumInterfaces          1
  bConfigurationValue     1
  iConfiguration          0
  bmAttributes         0x80 (Bus Powered)
  bMaxPower             250 (500mA)

Interface Descriptor:
  bLength                  9
  bDescriptorType          4 (Interface)
  bInterfaceNumber         0
  bAlternateSetting        0
  bNumEndpoints            4
  bInterfaceClass       0xFF (Vendor Specific)
  bInterfaceSubClass    0x01 (UFI Flux Interface)
  bInterfaceProtocol    0x01 (Protocol Version 1)
  iInterface               4  "UFI Flux Engine"
```

### 2.3 Endpoint Descriptors

| Endpoint | Direction | Type | Max Packet | Purpose |
|----------|-----------|------|------------|---------|
| EP1 OUT | Host→Device | Bulk | 512 bytes | Commands |
| EP1 IN | Device→Host | Bulk | 512 bytes | Responses |
| EP2 IN | Device→Host | Bulk | 512 bytes | Flux Data (Read) |
| EP3 OUT | Host→Device | Bulk | 512 bytes | Flux Data (Write) |

```c
// EP1 OUT - Command endpoint
Endpoint Descriptor:
  bLength                  7
  bDescriptorType          5 (Endpoint)
  bEndpointAddress      0x01 (OUT)
  bmAttributes          0x02 (Bulk)
  wMaxPacketSize         512
  bInterval                0

// EP1 IN - Response endpoint  
Endpoint Descriptor:
  bLength                  7
  bDescriptorType          5 (Endpoint)
  bEndpointAddress      0x81 (IN)
  bmAttributes          0x02 (Bulk)
  wMaxPacketSize         512
  bInterval                0

// EP2 IN - Flux read endpoint
Endpoint Descriptor:
  bLength                  7
  bDescriptorType          5 (Endpoint)
  bEndpointAddress      0x82 (IN)
  bmAttributes          0x02 (Bulk)
  wMaxPacketSize         512
  bInterval                0

// EP3 OUT - Flux write endpoint
Endpoint Descriptor:
  bLength                  7
  bDescriptorType          5 (Endpoint)
  bEndpointAddress      0x03 (OUT)
  bmAttributes          0x02 (Bulk)
  wMaxPacketSize         512
  bInterval                0
```

---

## 3. Command Protocol

### 3.1 Packet Structure

All commands and responses use a common packet header:

```
┌────────────────────────────────────────────────────────────────┐
│                      UFI Packet Header (16 bytes)              │
├───────┬───────┬───────┬───────┬───────────────┬───────────────┤
│ Magic │  Cmd  │ Flags │ SeqNo │    Length     │   Reserved    │
│ (4)   │  (1)  │  (1)  │  (2)  │     (4)       │     (4)       │
├───────┴───────┴───────┴───────┴───────────────┴───────────────┤
│                         Payload (0-496 bytes)                  │
├───────────────────────────────────────────────────────────────┤
│                      CRC32 Checksum (4 bytes)                  │
└───────────────────────────────────────────────────────────────┘
```

### 3.2 Header Fields

| Field | Offset | Size | Description |
|-------|--------|------|-------------|
| Magic | 0 | 4 | `0x55 0x46 0x49 0x21` ("UFI!") |
| Command | 4 | 1 | Command code (see section 4) |
| Flags | 5 | 1 | Bit flags (see below) |
| SeqNo | 6 | 2 | Sequence number (little-endian) |
| Length | 8 | 4 | Payload length (little-endian) |
| Reserved | 12 | 4 | Reserved (must be 0) |
| Payload | 16 | N | Command-specific data |
| CRC32 | 16+N | 4 | CRC32 of header + payload |

### 3.3 Flags Byte

```
Bit 7: ACK_REQUIRED  - Response expected
Bit 6: CONTINUED     - More packets follow
Bit 5: FINAL         - Last packet in sequence
Bit 4: ERROR         - Error response
Bit 3: Reserved
Bit 2: Reserved
Bit 1: Reserved
Bit 0: Reserved
```

### 3.4 C Structure Definition

```c
#pragma pack(push, 1)

#define UFI_MAGIC       0x21494655  // "UFI!" little-endian

typedef struct {
    uint32_t magic;         // Must be UFI_MAGIC
    uint8_t  command;       // Command code
    uint8_t  flags;         // Packet flags
    uint16_t seq_no;        // Sequence number
    uint32_t length;        // Payload length
    uint32_t reserved;      // Reserved (0)
} ufi_header_t;

typedef struct {
    ufi_header_t header;
    uint8_t      payload[496];  // Max payload
    uint32_t     crc32;
} ufi_packet_t;

// Flag definitions
#define UFI_FLAG_ACK_REQUIRED   (1 << 7)
#define UFI_FLAG_CONTINUED      (1 << 6)
#define UFI_FLAG_FINAL          (1 << 5)
#define UFI_FLAG_ERROR          (1 << 4)

#pragma pack(pop)
```

---

## 4. Command Codes

### 4.1 System Commands (0x00 - 0x0F)

| Code | Name | Description | Payload |
|------|------|-------------|---------|
| 0x00 | CMD_NOP | No operation | None |
| 0x01 | CMD_INFO | Get device info | None |
| 0x02 | CMD_VERSION | Get firmware version | None |
| 0x03 | CMD_RESET | Soft reset device | None |
| 0x04 | CMD_ENTER_DFU | Enter bootloader | None |
| 0x05 | CMD_SET_LED | Control status LEDs | LED state byte |
| 0x06 | CMD_GET_STATUS | Get device status | None |
| 0x07 | CMD_ECHO | Echo test | Any data |

### 4.2 Drive Commands (0x10 - 0x2F)

| Code | Name | Description | Payload |
|------|------|-------------|---------|
| 0x10 | CMD_DRIVE_SELECT | Select drive | drive_select_t |
| 0x11 | CMD_DRIVE_DESELECT | Deselect all drives | None |
| 0x12 | CMD_MOTOR_ON | Turn motor on | None |
| 0x13 | CMD_MOTOR_OFF | Turn motor off | None |
| 0x14 | CMD_SEEK | Seek to track | seek_params_t |
| 0x15 | CMD_RECALIBRATE | Seek to track 0 | None |
| 0x16 | CMD_GET_TRACK | Get current track | None |
| 0x17 | CMD_SET_SIDE | Select head/side | side (0 or 1) |
| 0x18 | CMD_GET_DRIVE_STATUS | Get drive status | None |
| 0x19 | CMD_SET_DENSITY | Set density mode | density_t |

### 4.3 Flux Commands (0x30 - 0x4F)

| Code | Name | Description | Payload |
|------|------|-------------|---------|
| 0x30 | CMD_FLUX_READ | Read flux data | read_params_t |
| 0x31 | CMD_FLUX_WRITE | Write flux data | write_params_t |
| 0x32 | CMD_FLUX_ERASE | Erase track | erase_params_t |
| 0x33 | CMD_FLUX_READ_INDEX | Read with index sync | read_params_t |
| 0x34 | CMD_FLUX_ABORT | Abort current operation | None |
| 0x35 | CMD_FLUX_GET_STATS | Get capture statistics | None |
| 0x36 | CMD_SET_SAMPLE_RATE | Set sample rate | sample_rate_t |
| 0x37 | CMD_GET_SAMPLE_RATE | Get current rate | None |
| 0x38 | CMD_SET_PRECOMP | Set write precompensation | precomp_t |

### 4.4 IEC Commands (0x50 - 0x6F)

| Code | Name | Description | Payload |
|------|------|-------------|---------|
| 0x50 | CMD_IEC_RESET | Reset IEC bus | None |
| 0x51 | CMD_IEC_LISTEN | Send LISTEN command | device_addr |
| 0x52 | CMD_IEC_TALK | Send TALK command | device_addr |
| 0x53 | CMD_IEC_UNLISTEN | Send UNLISTEN | None |
| 0x54 | CMD_IEC_UNTALK | Send UNTALK | None |
| 0x55 | CMD_IEC_OPEN | Open channel | open_params_t |
| 0x56 | CMD_IEC_CLOSE | Close channel | channel |
| 0x57 | CMD_IEC_READ | Read data | read_iec_t |
| 0x58 | CMD_IEC_WRITE | Write data | data |
| 0x59 | CMD_IEC_GET_EOI | Check EOI status | None |
| 0x5A | CMD_IEC_DIRECTORY | Read directory | device_addr |

### 4.5 Sync Sensor Commands (0x70 - 0x7F)

| Code | Name | Description | Payload |
|------|------|-------------|---------|
| 0x70 | CMD_SYNC_ENABLE | Enable sync sensor | sensor_id |
| 0x71 | CMD_SYNC_DISABLE | Disable sync sensor | sensor_id |
| 0x72 | CMD_SYNC_GET_STATUS | Get sync status | None |
| 0x73 | CMD_SYNC_CALIBRATE | Calibrate sensor | cal_params_t |

---

## 5. Response Codes

### 5.1 Success Codes (0x00 - 0x0F)

| Code | Name | Description |
|------|------|-------------|
| 0x00 | RSP_OK | Command successful |
| 0x01 | RSP_OK_DATA | Success with data payload |
| 0x02 | RSP_OK_PENDING | Operation in progress |

### 5.2 Error Codes (0x80 - 0xFF)

| Code | Name | Description |
|------|------|-------------|
| 0x80 | ERR_UNKNOWN_CMD | Unknown command code |
| 0x81 | ERR_INVALID_PARAM | Invalid parameter |
| 0x82 | ERR_INVALID_STATE | Invalid device state |
| 0x83 | ERR_NO_DRIVE | No drive selected |
| 0x84 | ERR_NO_DISK | No disk in drive |
| 0x85 | ERR_WRITE_PROTECT | Disk is write protected |
| 0x86 | ERR_SEEK_FAILED | Seek operation failed |
| 0x87 | ERR_TIMEOUT | Operation timed out |
| 0x88 | ERR_CRC | CRC error in data |
| 0x89 | ERR_BUFFER_OVERFLOW | Buffer overflow |
| 0x8A | ERR_IEC_TIMEOUT | IEC bus timeout |
| 0x8B | ERR_IEC_DEVICE | IEC device error |
| 0x8C | ERR_NOT_READY | Drive not ready |
| 0x8D | ERR_ABORTED | Operation aborted |
| 0xFE | ERR_INTERNAL | Internal firmware error |
| 0xFF | ERR_FATAL | Fatal error, reset required |

---

## 6. Data Structures

### 6.1 Device Info Response

```c
typedef struct {
    char     device_name[32];    // "UFI Flux Engine"
    char     fw_version[16];     // "1.0.0"
    char     hw_version[16];     // "1.0"
    char     serial[32];         // Unique serial number
    uint32_t capabilities;       // Capability flags
    uint32_t buffer_size;        // Internal buffer size
    uint32_t max_sample_rate;    // Max sample rate in Hz
    uint16_t num_drives;         // Number of drive ports
    uint16_t reserved;
} device_info_t;

// Capability flags
#define CAP_FLUX_READ       (1 << 0)
#define CAP_FLUX_WRITE      (1 << 1)
#define CAP_IEC_BUS         (1 << 2)
#define CAP_SYNC_SENSOR     (1 << 3)
#define CAP_APPLE_DISK_II   (1 << 4)
#define CAP_AMIGA_EXT       (1 << 5)
#define CAP_HIGH_DENSITY    (1 << 6)
#define CAP_PRECOMPENSATION (1 << 7)
```

### 6.2 Drive Select Parameters

```c
typedef struct {
    uint8_t  port;          // Drive port (0-5)
    uint8_t  type;          // Drive type (see below)
    uint16_t flags;         // Drive flags
} drive_select_t;

// Drive types
#define DRIVE_TYPE_SHUGART_35   0x01  // 3.5" PC/Amiga/ST
#define DRIVE_TYPE_SHUGART_525  0x02  // 5.25" PC
#define DRIVE_TYPE_APPLE_525    0x03  // Apple Disk II
#define DRIVE_TYPE_AMIGA_EXT    0x04  // Amiga external
#define DRIVE_TYPE_IEC          0x05  // IEC/C64

// Drive flags
#define DRIVE_FLAG_DOUBLE_STEP  (1 << 0)  // 40 track in 80 track drive
#define DRIVE_FLAG_IGNORE_READY (1 << 1)  // Ignore drive ready signal
```

### 6.3 Seek Parameters

```c
typedef struct {
    uint8_t  track;         // Target track (0-83 typical)
    uint8_t  flags;         // Seek flags
    uint16_t step_rate_us;  // Step rate in microseconds
} seek_params_t;

// Seek flags
#define SEEK_FLAG_VERIFY    (1 << 0)  // Verify track after seek
```

### 6.4 Flux Read Parameters

```c
typedef struct {
    uint8_t  track;         // Track number
    uint8_t  side;          // Head/side (0 or 1)
    uint8_t  revolutions;   // Number of revolutions to capture
    uint8_t  flags;         // Read flags
    uint32_t timeout_ms;    // Timeout in milliseconds
} read_params_t;

// Read flags
#define READ_FLAG_INDEX_SYNC    (1 << 0)  // Wait for index pulse
#define READ_FLAG_RAW           (1 << 1)  // Raw flux (no processing)
#define READ_FLAG_SYNC_SENSOR   (1 << 2)  // Use external sync sensor
```

### 6.5 Flux Data Format

Flux data is transferred as a stream of timing values representing the time between magnetic transitions.

```c
// Flux timing entry (variable length encoding)
// 
// 1-byte encoding (1-127):
//   0xxxxxxx = value * base_tick
//
// 2-byte encoding (128-16383):
//   10xxxxxx xxxxxxxx = value * base_tick
//
// 3-byte encoding (16384-2097151):
//   110xxxxx xxxxxxxx xxxxxxxx = value * base_tick
//
// Special codes:
//   0xFF 0x00 = Index pulse marker
//   0xFF 0x01 = End of data
//   0xFF 0x02 = Buffer underrun occurred
//   0xFF 0x03 = Sync sensor trigger

// Flux data packet
typedef struct {
    uint32_t index_offset;  // Sample offset of index pulse
    uint32_t num_samples;   // Number of flux samples
    uint32_t sample_rate;   // Sample rate in Hz
    uint8_t  data[];        // Variable-length flux data
} flux_data_t;
```

### 6.6 Flux Write Parameters

```c
typedef struct {
    uint8_t  track;         // Track number
    uint8_t  side;          // Head/side (0 or 1)
    uint8_t  flags;         // Write flags
    uint8_t  precomp_ns;    // Write precompensation in ns
    uint32_t data_length;   // Length of flux data to write
} write_params_t;

// Write flags
#define WRITE_FLAG_ERASE_FIRST  (1 << 0)  // Erase track before write
#define WRITE_FLAG_VERIFY       (1 << 1)  // Verify after write
```

### 6.7 Drive Status Response

```c
typedef struct {
    uint8_t  port;          // Drive port
    uint8_t  flags;         // Status flags
    uint8_t  track;         // Current track
    uint8_t  side;          // Current side
    uint32_t motor_time;    // Motor on time in ms
} drive_status_t;

// Status flags
#define STATUS_DRIVE_READY      (1 << 0)
#define STATUS_DISK_PRESENT     (1 << 1)
#define STATUS_WRITE_PROTECTED  (1 << 2)
#define STATUS_TRACK_00         (1 << 3)
#define STATUS_INDEX_PULSE      (1 << 4)
#define STATUS_MOTOR_ON         (1 << 5)
```

---

## 7. Protocol Flow Examples

### 7.1 Read Track Example

```
Host                                    Device
  │                                       │
  │  CMD_DRIVE_SELECT (port=0, type=1)    │
  ├──────────────────────────────────────►│
  │                                       │
  │  RSP_OK                               │
  │◄──────────────────────────────────────┤
  │                                       │
  │  CMD_MOTOR_ON                         │
  ├──────────────────────────────────────►│
  │                                       │
  │  RSP_OK                               │
  │◄──────────────────────────────────────┤
  │                                       │
  │  (wait 500ms for motor spinup)        │
  │                                       │
  │  CMD_SEEK (track=40)                  │
  ├──────────────────────────────────────►│
  │                                       │
  │  RSP_OK                               │
  │◄──────────────────────────────────────┤
  │                                       │
  │  CMD_FLUX_READ (track=40, side=0,     │
  │                 revolutions=3)         │
  ├──────────────────────────────────────►│
  │                                       │
  │  RSP_OK_PENDING                       │
  │◄──────────────────────────────────────┤
  │                                       │
  │  (EP2 IN) Flux data packet 1          │
  │◄══════════════════════════════════════╡
  │                                       │
  │  (EP2 IN) Flux data packet 2          │
  │◄══════════════════════════════════════╡
  │                                       │
  │  (EP2 IN) Flux data packet N (FINAL)  │
  │◄══════════════════════════════════════╡
  │                                       │
  │  RSP_OK (on EP1)                      │
  │◄──────────────────────────────────────┤
  │                                       │
  │  CMD_MOTOR_OFF                        │
  ├──────────────────────────────────────►│
  │                                       │
  │  RSP_OK                               │
  │◄──────────────────────────────────────┤
```

### 7.2 IEC Directory Listing Example

```
Host                                    Device
  │                                       │
  │  CMD_IEC_DIRECTORY (device=8)         │
  ├──────────────────────────────────────►│
  │                                       │
  │  RSP_OK_DATA                          │
  │  + directory listing data             │
  │◄──────────────────────────────────────┤
```

---

## 8. Error Handling

### 8.1 Timeout Handling

- Command timeout: 5000ms (configurable)
- Flux read timeout: Per-revolution (200ms typical for 300 RPM)
- IEC timeout: 1000ms (C64 standard)

### 8.2 Retry Policy

```c
// Recommended retry policy
#define MAX_RETRIES     3
#define RETRY_DELAY_MS  100

// Retryable errors
#define IS_RETRYABLE(err) ( \
    (err) == ERR_TIMEOUT || \
    (err) == ERR_CRC || \
    (err) == ERR_IEC_TIMEOUT \
)
```

### 8.3 Recovery Procedures

1. **Command Timeout**: Send CMD_NOP, wait for response
2. **Device Not Responding**: USB reset, re-enumerate
3. **Fatal Error**: Hardware reset via GPIO, re-enumerate

---

## 9. Streaming Protocol

For large data transfers (flux read/write), data flows through separate endpoints while control messages continue on EP1.

### 9.1 Read Streaming

```
EP1 OUT: CMD_FLUX_READ
EP1 IN:  RSP_OK_PENDING

EP2 IN:  [Flux Data Packet 1] (CONTINUED flag)
EP2 IN:  [Flux Data Packet 2] (CONTINUED flag)
EP2 IN:  [Flux Data Packet 3] (CONTINUED flag)
...
EP2 IN:  [Flux Data Packet N] (FINAL flag)

EP1 IN:  RSP_OK (completion status)
```

### 9.2 Write Streaming

```
EP1 OUT: CMD_FLUX_WRITE (with parameters)
EP1 IN:  RSP_OK_PENDING

EP3 OUT: [Flux Data Packet 1] (CONTINUED flag)
EP3 OUT: [Flux Data Packet 2] (CONTINUED flag)
...
EP3 OUT: [Flux Data Packet N] (FINAL flag)

EP1 IN:  RSP_OK (verification result)
```

### 9.3 Flow Control

The device uses internal buffering (1MB) to handle data rate mismatches. The host should:

1. Check device status before starting large transfers
2. Monitor for ERR_BUFFER_OVERFLOW responses
3. Implement backpressure via USB NAK handling

---

## 10. Reference Implementation

### 10.1 Python Example (Host Side)

```python
#!/usr/bin/env python3
"""UFI USB Protocol - Python Reference Implementation"""

import struct
import usb.core
import usb.util
import zlib
from enum import IntEnum
from dataclasses import dataclass
from typing import Optional, Tuple

# USB IDs
UFI_VID = 0x1209
UFI_PID = 0x4F54

# Endpoints
EP_CMD_OUT = 0x01
EP_CMD_IN = 0x81
EP_FLUX_IN = 0x82
EP_FLUX_OUT = 0x03

# Protocol constants
UFI_MAGIC = 0x21494655  # "UFI!"
HEADER_SIZE = 16
MAX_PAYLOAD = 496
TIMEOUT_MS = 5000


class Command(IntEnum):
    NOP = 0x00
    INFO = 0x01
    VERSION = 0x02
    RESET = 0x03
    ENTER_DFU = 0x04
    SET_LED = 0x05
    GET_STATUS = 0x06
    
    DRIVE_SELECT = 0x10
    DRIVE_DESELECT = 0x11
    MOTOR_ON = 0x12
    MOTOR_OFF = 0x13
    SEEK = 0x14
    RECALIBRATE = 0x15
    
    FLUX_READ = 0x30
    FLUX_WRITE = 0x31
    FLUX_ABORT = 0x34


class Response(IntEnum):
    OK = 0x00
    OK_DATA = 0x01
    OK_PENDING = 0x02
    
    ERR_UNKNOWN_CMD = 0x80
    ERR_INVALID_PARAM = 0x81
    ERR_NO_DRIVE = 0x83
    ERR_NO_DISK = 0x84
    ERR_TIMEOUT = 0x87


class Flags:
    ACK_REQUIRED = 0x80
    CONTINUED = 0x40
    FINAL = 0x20
    ERROR = 0x10


@dataclass
class UFIPacket:
    command: int
    flags: int
    seq_no: int
    payload: bytes
    
    def to_bytes(self) -> bytes:
        """Serialize packet to bytes"""
        header = struct.pack('<IBBHI',
            UFI_MAGIC,
            self.command,
            self.flags,
            self.seq_no,
            len(self.payload),
        ) + b'\x00\x00\x00\x00'  # Reserved
        
        data = header + self.payload
        crc = zlib.crc32(data) & 0xFFFFFFFF
        return data + struct.pack('<I', crc)
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'UFIPacket':
        """Deserialize packet from bytes"""
        if len(data) < HEADER_SIZE + 4:
            raise ValueError("Packet too short")
        
        magic, cmd, flags, seq, length, _ = struct.unpack('<IBBHII', data[:16])
        
        if magic != UFI_MAGIC:
            raise ValueError(f"Invalid magic: {magic:#x}")
        
        payload = data[16:16+length]
        
        # Verify CRC
        expected_crc = struct.unpack('<I', data[16+length:16+length+4])[0]
        actual_crc = zlib.crc32(data[:16+length]) & 0xFFFFFFFF
        
        if expected_crc != actual_crc:
            raise ValueError(f"CRC mismatch: {expected_crc:#x} != {actual_crc:#x}")
        
        return cls(cmd, flags, seq, payload)


class UFIDevice:
    """UFI USB Device Interface"""
    
    def __init__(self):
        self.dev = None
        self.seq_no = 0
    
    def open(self) -> bool:
        """Find and open UFI device"""
        self.dev = usb.core.find(idVendor=UFI_VID, idProduct=UFI_PID)
        
        if self.dev is None:
            return False
        
        # Detach kernel driver if active
        if self.dev.is_kernel_driver_active(0):
            self.dev.detach_kernel_driver(0)
        
        self.dev.set_configuration()
        return True
    
    def close(self):
        """Close device"""
        if self.dev:
            usb.util.dispose_resources(self.dev)
            self.dev = None
    
    def _send_command(self, cmd: int, payload: bytes = b'', 
                      flags: int = Flags.ACK_REQUIRED) -> UFIPacket:
        """Send command and receive response"""
        self.seq_no = (self.seq_no + 1) & 0xFFFF
        
        packet = UFIPacket(cmd, flags, self.seq_no, payload)
        data = packet.to_bytes()
        
        # Send command
        written = self.dev.write(EP_CMD_OUT, data, TIMEOUT_MS)
        if written != len(data):
            raise IOError(f"Write failed: {written}/{len(data)}")
        
        # Read response
        response = self.dev.read(EP_CMD_IN, 512, TIMEOUT_MS)
        return UFIPacket.from_bytes(bytes(response))
    
    def get_info(self) -> dict:
        """Get device information"""
        resp = self._send_command(Command.INFO)
        
        if resp.command != Response.OK_DATA:
            raise RuntimeError(f"Info failed: {resp.command:#x}")
        
        # Parse info structure
        name = resp.payload[:32].rstrip(b'\x00').decode()
        fw_ver = resp.payload[32:48].rstrip(b'\x00').decode()
        hw_ver = resp.payload[48:64].rstrip(b'\x00').decode()
        serial = resp.payload[64:96].rstrip(b'\x00').decode()
        caps, buf_size, max_rate, num_drives = struct.unpack(
            '<IIIH', resp.payload[96:114])
        
        return {
            'name': name,
            'firmware_version': fw_ver,
            'hardware_version': hw_ver,
            'serial': serial,
            'capabilities': caps,
            'buffer_size': buf_size,
            'max_sample_rate': max_rate,
            'num_drives': num_drives,
        }
    
    def select_drive(self, port: int, drive_type: int = 1) -> bool:
        """Select a drive port"""
        payload = struct.pack('<BBH', port, drive_type, 0)
        resp = self._send_command(Command.DRIVE_SELECT, payload)
        return resp.command == Response.OK
    
    def motor_on(self) -> bool:
        """Turn drive motor on"""
        resp = self._send_command(Command.MOTOR_ON)
        return resp.command == Response.OK
    
    def motor_off(self) -> bool:
        """Turn drive motor off"""
        resp = self._send_command(Command.MOTOR_OFF)
        return resp.command == Response.OK
    
    def seek(self, track: int, step_rate_us: int = 3000) -> bool:
        """Seek to track"""
        payload = struct.pack('<BBH', track, 0, step_rate_us)
        resp = self._send_command(Command.SEEK, payload)
        return resp.command == Response.OK
    
    def read_flux(self, track: int, side: int = 0, 
                  revolutions: int = 3) -> bytes:
        """Read flux data from track"""
        payload = struct.pack('<BBBI', track, side, revolutions, 0, 10000)
        resp = self._send_command(Command.FLUX_READ, payload)
        
        if resp.command != Response.OK_PENDING:
            raise RuntimeError(f"Read failed: {resp.command:#x}")
        
        # Collect flux data from EP2
        flux_data = b''
        while True:
            data = bytes(self.dev.read(EP_FLUX_IN, 512, TIMEOUT_MS))
            packet = UFIPacket.from_bytes(data)
            flux_data += packet.payload
            
            if packet.flags & Flags.FINAL:
                break
        
        # Read completion status
        resp = self.dev.read(EP_CMD_IN, 512, TIMEOUT_MS)
        final = UFIPacket.from_bytes(bytes(resp))
        
        if final.command != Response.OK:
            raise RuntimeError(f"Read completed with error: {final.command:#x}")
        
        return flux_data
    
    def enter_dfu(self) -> bool:
        """Enter DFU bootloader mode"""
        try:
            resp = self._send_command(Command.ENTER_DFU)
            return resp.command == Response.OK
        except usb.core.USBError:
            # Device disconnects, this is expected
            return True


# Example usage
if __name__ == '__main__':
    ufi = UFIDevice()
    
    if not ufi.open():
        print("UFI device not found")
        exit(1)
    
    try:
        # Get device info
        info = ufi.get_info()
        print(f"Device: {info['name']}")
        print(f"Firmware: {info['firmware_version']}")
        print(f"Serial: {info['serial']}")
        
        # Read a track
        if ufi.select_drive(0):
            ufi.motor_on()
            import time
            time.sleep(0.5)  # Spinup
            
            if ufi.seek(0):
                flux = ufi.read_flux(0, 0, 3)
                print(f"Read {len(flux)} bytes of flux data")
            
            ufi.motor_off()
    
    finally:
        ufi.close()
```

### 10.2 C Example (Device Side - STM32)

```c
/* UFI Protocol Handler - STM32 Firmware */

#include "ufi_protocol.h"
#include "usbd_core.h"
#include "flux_capture.h"

static uint16_t seq_no = 0;

void ufi_handle_command(uint8_t *data, uint16_t len) {
    ufi_header_t *hdr = (ufi_header_t *)data;
    
    // Verify magic
    if (hdr->magic != UFI_MAGIC) {
        ufi_send_error(ERR_INVALID_PARAM, hdr->seq_no);
        return;
    }
    
    // Verify CRC
    uint32_t crc = crc32(data, HEADER_SIZE + hdr->length);
    uint32_t recv_crc = *(uint32_t *)(data + HEADER_SIZE + hdr->length);
    if (crc != recv_crc) {
        ufi_send_error(ERR_CRC, hdr->seq_no);
        return;
    }
    
    uint8_t *payload = data + HEADER_SIZE;
    
    switch (hdr->command) {
        case CMD_NOP:
            ufi_send_response(RSP_OK, hdr->seq_no, NULL, 0);
            break;
            
        case CMD_INFO:
            ufi_cmd_info(hdr->seq_no);
            break;
            
        case CMD_DRIVE_SELECT:
            ufi_cmd_drive_select(hdr->seq_no, payload);
            break;
            
        case CMD_MOTOR_ON:
            fdd_motor_on();
            ufi_send_response(RSP_OK, hdr->seq_no, NULL, 0);
            break;
            
        case CMD_MOTOR_OFF:
            fdd_motor_off();
            ufi_send_response(RSP_OK, hdr->seq_no, NULL, 0);
            break;
            
        case CMD_SEEK:
            ufi_cmd_seek(hdr->seq_no, payload);
            break;
            
        case CMD_FLUX_READ:
            ufi_cmd_flux_read(hdr->seq_no, payload);
            break;
            
        case CMD_ENTER_DFU:
            ufi_send_response(RSP_OK, hdr->seq_no, NULL, 0);
            HAL_Delay(10);
            enter_dfu_bootloader();
            break;
            
        default:
            ufi_send_error(ERR_UNKNOWN_CMD, hdr->seq_no);
            break;
    }
}

void ufi_send_response(uint8_t status, uint16_t seq, 
                       uint8_t *payload, uint16_t len) {
    static uint8_t buf[512];
    
    ufi_header_t *hdr = (ufi_header_t *)buf;
    hdr->magic = UFI_MAGIC;
    hdr->command = status;
    hdr->flags = UFI_FLAG_FINAL;
    hdr->seq_no = seq;
    hdr->length = len;
    hdr->reserved = 0;
    
    if (payload && len > 0) {
        memcpy(buf + HEADER_SIZE, payload, len);
    }
    
    uint32_t crc = crc32(buf, HEADER_SIZE + len);
    memcpy(buf + HEADER_SIZE + len, &crc, 4);
    
    USBD_Transmit(EP_CMD_IN, buf, HEADER_SIZE + len + 4);
}

void ufi_send_flux_data(uint8_t *data, uint32_t len, bool final) {
    static uint8_t buf[512];
    uint32_t offset = 0;
    
    while (offset < len) {
        uint32_t chunk = MIN(len - offset, MAX_PAYLOAD);
        bool is_final = final && (offset + chunk >= len);
        
        ufi_header_t *hdr = (ufi_header_t *)buf;
        hdr->magic = UFI_MAGIC;
        hdr->command = RSP_OK_DATA;
        hdr->flags = is_final ? UFI_FLAG_FINAL : UFI_FLAG_CONTINUED;
        hdr->seq_no = ++seq_no;
        hdr->length = chunk;
        hdr->reserved = 0;
        
        memcpy(buf + HEADER_SIZE, data + offset, chunk);
        
        uint32_t crc = crc32(buf, HEADER_SIZE + chunk);
        memcpy(buf + HEADER_SIZE + chunk, &crc, 4);
        
        USBD_Transmit(EP_FLUX_IN, buf, HEADER_SIZE + chunk + 4);
        
        offset += chunk;
    }
}
```

---

## 11. Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-01-16 | Initial specification |

---

## 12. Appendix: CRC32 Implementation

```c
// CRC32 (ISO 3309 / ITU-T V.42)
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    // ... (full table omitted for brevity)
};

uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    
    while (len--) {
        crc = crc32_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}
```
