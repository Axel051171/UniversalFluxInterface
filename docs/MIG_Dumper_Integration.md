# UFI MIG Dumper Integration

## Overview

This module provides integration between UFI and the MIG Switch Dumper device, enabling management of Nintendo Switch game cartridge dumps through the UFI interface.

```
┌─────────────────────────────────────────────────────────────────────┐
│                          UFI System                                  │
│                                                                      │
│   ┌─────────────────┐       ┌─────────────────────────────────────┐ │
│   │   MIG Dumper    │ USB   │                                     │ │
│   │   (Hardware)    │◄─────►│           UFI Software              │ │
│   │                 │       │                                     │ │
│   │ ┌─────────────┐ │       │  ┌───────────┐  ┌───────────────┐  │ │
│   │ │ Switch Cart │ │       │  │  CLI Tool │  │   Web API     │  │ │
│   │ │   Slot      │ │       │  │  ufi-mig  │  │  /api/mig/*   │  │ │
│   │ └─────────────┘ │       │  └───────────┘  └───────────────┘  │ │
│   │                 │       │                                     │ │
│   │ ┌─────────────┐ │       │  ┌───────────────────────────────┐  │ │
│   │ │ SD Card /   │ │       │  │       Web Interface           │  │ │
│   │ │ Internal    │ │       │  │   MIGDumperPanel.tsx          │  │ │
│   │ └─────────────┘ │       │  └───────────────────────────────┘  │ │
│   └─────────────────┘       └─────────────────────────────────────┘ │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Components

### 1. CLI Tool (`ufi-mig`)

Command-line interface for MIG Dumper management.

**Installation:**
```bash
cp software/tools/ufi-mig /usr/local/bin/
chmod +x /usr/local/bin/ufi-mig
```

**Commands:**

```bash
# Detect MIG Dumper
ufi-mig detect

# Mount device
ufi-mig mount
ufi-mig mount -d /dev/sda1

# List dumps
ufi-mig list

# Copy dump to UFI storage
ufi-mig copy -i 1                    # By index
ufi-mig copy -t "Zelda"              # By title search
ufi-mig copy -i 1 -o /path/to/dest   # Custom destination
ufi-mig copy -i 1 --xci-only         # XCI only (skip extras)

# Verify dump integrity
ufi-mig verify -i 1

# Delete dump from MIG
ufi-mig delete -i 1
ufi-mig delete -i 1 -f               # Force (skip confirmation)

# Unmount device
ufi-mig unmount
```

### 2. Web API (`mig_api.py`)

Flask Blueprint providing REST API endpoints.

**Endpoints:**

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/mig/status` | Get device status |
| POST | `/api/mig/detect` | Scan for USB devices |
| POST | `/api/mig/mount` | Mount MIG Dumper |
| POST | `/api/mig/unmount` | Unmount device |
| GET | `/api/mig/dumps` | List all dumps |
| POST | `/api/mig/dumps/{path}/copy` | Copy dump to UFI |
| POST | `/api/mig/dumps/{path}/verify` | Verify dump integrity |
| DELETE | `/api/mig/dumps/{path}` | Delete dump |
| POST | `/api/mig/operation/cancel` | Cancel current operation |

### 3. Web Component (`MIGDumperPanel.tsx`)

React component for the UFI web interface.

**Features:**
- Device detection and mounting
- Storage usage display
- Dump listing with details
- Copy/Verify/Delete operations
- Real-time progress updates

---

## File Format Support

### XCI Files

The Nintendo Switch XCI (NX Card Image) format:

```
┌─────────────────────────────────────┐
│          XCI File Layout            │
├─────────────────────────────────────┤
│  0x0000 - 0x00FF: RSA Signature     │
│  0x0100 - 0x0103: Magic "HEAD"      │
│  0x0104 - 0x01FF: Header Data       │
│  0x0200 - 0x6FFF: Reserved          │
│  0x7000 - 0x71FF: Certificate       │  ← Usually blanked (0xFF)
│  0x7200 - ...   : Game Data         │
└─────────────────────────────────────┘
```

### Dump File Structure

MIG Dumper creates the following files:

```
{GameTitle} [TitleID]/
├── {GameTitle} [TitleID].xci           # Main game image
├── {GameTitle} [TitleID] (Certificate).bin     # Optional
├── {GameTitle} [TitleID] (Initial Data).bin    # Optional
├── {GameTitle} [TitleID] (Card ID Set).bin     # Optional
└── {GameTitle} [TitleID] (Card UID).bin        # Optional
```

### ROM Size Codes

| Code | Capacity |
|------|----------|
| 0xFA | 1 GB |
| 0xF8 | 2 GB |
| 0xF0 | 4 GB |
| 0xE0 | 8 GB |
| 0xE1 | 16 GB |
| 0xE2 | 32 GB |

---

## API Examples

### Python

```python
import requests

BASE_URL = "http://ufi.local/api/mig"

# Check status
status = requests.get(f"{BASE_URL}/status").json()
print(f"Connected: {status['connected']}")

# Mount device
requests.post(f"{BASE_URL}/mount", json={
    "device_path": "/dev/sda1"
})

# List dumps
dumps = requests.get(f"{BASE_URL}/dumps").json()
for dump in dumps['dumps']:
    print(f"{dump['title']} - {dump['file_size_gb']} GB")

# Copy dump
requests.post(f"{BASE_URL}/dumps/MyGame/copy", json={
    "destination": "/var/lib/ufi/images",
    "include_extras": True
})

# Monitor progress
while True:
    status = requests.get(f"{BASE_URL}/status").json()
    if not status['operation_active']:
        break
    print(f"Progress: {status['operation_progress']}%")
    time.sleep(1)
```

### JavaScript

```javascript
const API_BASE = '/api/mig';

// Check status
const status = await fetch(`${API_BASE}/status`).then(r => r.json());

// List dumps
const { dumps } = await fetch(`${API_BASE}/dumps`).then(r => r.json());

// Copy dump
await fetch(`${API_BASE}/dumps/${encodeURIComponent(dumps[0].path)}/copy`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ include_extras: true })
});
```

### cURL

```bash
# Status
curl http://ufi.local/api/mig/status

# Detect devices
curl -X POST http://ufi.local/api/mig/detect

# Mount
curl -X POST http://ufi.local/api/mig/mount \
    -H "Content-Type: application/json" \
    -d '{"device_path": "/dev/sda1"}'

# List dumps
curl http://ufi.local/api/mig/dumps

# Copy dump
curl -X POST "http://ufi.local/api/mig/dumps/MyGame%20%5B0100XXX%5D/copy" \
    -H "Content-Type: application/json" \
    -d '{"include_extras": true}'
```

---

## Integration with UFI Workflow

### Automated Dump Workflow

```bash
#!/bin/bash
# Auto-dump workflow script

# Wait for MIG Dumper
echo "Waiting for MIG Dumper..."
while ! ufi-mig detect | grep -q "detected"; do
    sleep 2
done

# Mount
ufi-mig mount

# List and copy all dumps
for i in $(ufi-mig list --json | jq -r '.[].index'); do
    echo "Copying dump $i..."
    ufi-mig copy -i $i -o /var/lib/ufi/images/switch/
done

# Verify all
for i in $(ufi-mig list --json | jq -r '.[].index'); do
    ufi-mig verify -i $i
done

# Eject
ufi-mig unmount
echo "Done!"
```

### systemd Service for Auto-Mount

```ini
# /etc/systemd/system/ufi-mig-automount.service
[Unit]
Description=UFI MIG Dumper Auto-Mount
After=ufi.service

[Service]
Type=simple
ExecStart=/usr/local/bin/ufi-mig-monitor
Restart=always

[Install]
WantedBy=multi-user.target
```

---

## Troubleshooting

### Device Not Detected

1. Check USB connection
2. Verify MIG Dumper is powered on
3. Check dmesg for USB events:
   ```bash
   dmesg | tail -20
   ```
4. List USB devices:
   ```bash
   lsusb
   lsblk
   ```

### Mount Fails

1. Check filesystem type (FAT32/exFAT supported)
2. Ensure mount point exists:
   ```bash
   sudo mkdir -p /mnt/mig
   ```
3. Try manual mount:
   ```bash
   sudo mount -t vfat /dev/sda1 /mnt/mig
   ```

### Copy Errors

1. Check available space on UFI
2. Verify dump integrity first
3. Check file permissions

### Verification Fails

- **Invalid header**: XCI file may be corrupted
- **Size mismatch**: Incomplete dump or trimmed file
- **No certificate**: Normal if blanked for privacy

---

## Security Considerations

### Certificate Handling

The MIG Dumper blanks certificates in XCI files (replaces with 0xFF) to prevent tracking. The original certificate is saved separately if enabled.

### Title Keys

Initial Data files may contain encrypted title keys. These are console-specific and required for decryption on matching consoles.

### Legal Notice

This integration tool is for legitimate backup purposes only. Ensure you own the original game cartridges for any dumps you create or manage.

---

## File Locations

| Component | Path |
|-----------|------|
| CLI Tool | `/usr/local/bin/ufi-mig` |
| Web API | `/opt/ufi/web/api/mig_api.py` |
| Web Component | `/opt/ufi/web/components/MIGDumperPanel.tsx` |
| C Library | `/opt/ufi/lib/libmig.so` |
| Config | `/etc/ufi/mig.yaml` |
| Logs | `/var/log/ufi/mig.log` |
| Default Storage | `/var/lib/ufi/images/switch/` |
| Mount Point | `/mnt/mig` |

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2026-01-17 | Initial release |

---

## References

- [MIG Switch Official](https://migswitch.com)
- [SwitchBrew Gamecard Documentation](https://switchbrew.org/wiki/Gamecard)
- [XCI File Format](https://switchbrew.org/wiki/XCI)
