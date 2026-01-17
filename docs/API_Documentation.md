# UFI API Dokumentation

## Übersicht

REST API und WebSocket für Kommunikation zwischen PC-Hauptprogramm und CM5.

```
PC-Hauptprogramm  ◄──── HTTP/WebSocket ────►  CM5 (ufi.local:5000)
```

**Basis-URL:** `http://ufi.local:5000/api/`
**WebSocket:** `ws://ufi.local:5000/ws`

---

## Response-Format

```json
{
    "success": true,
    "data": { ... },
    "error": null
}
```

---

## System-Endpunkte

### GET /api/status
```json
{
    "version": "1.0.0",
    "stm32_connected": true,
    "active_drive": 1,
    "motor_on": false,
    "buffer_tracks": 0
}
```

### GET /api/info
```json
{
    "device": "UFI",
    "hardware_version": "2.1",
    "supported_drives": [
        {"id": 1, "name": "Shugart A"},
        {"id": 2, "name": "Shugart B"},
        {"id": 3, "name": "Apple Disk II"},
        {"id": 4, "name": "Amiga External"},
        {"id": 5, "name": "IEC Bus"}
    ]
}
```

---

## Laufwerk-Steuerung

### POST /api/drive/select
```json
// Request
{"drive": 1}

// Response
{"drive": 1, "name": "Shugart A", "ready": true}
```

| drive | Beschreibung |
|-------|--------------|
| 1 | FDD1 (Shugart A) |
| 2 | FDD2 (Shugart B) |
| 3 | Apple Disk II |
| 4 | Amiga External |
| 5 | IEC Bus (1541) |

### POST /api/drive/motor
```json
// Request
{"on": true}

// Response
{"motor": true, "spin_up_time_ms": 500}
```

### GET /api/drive/status
```json
{
    "drive": 1,
    "motor_on": true,
    "current_track": 35,
    "current_side": 0,
    "track0": false,
    "write_protected": true,
    "disk_changed": false,
    "rpm": 300.5
}
```

### POST /api/drive/seek
```json
// Request
{"track": 35}

// Response
{"track": 35, "seek_time_ms": 105}
```

### POST /api/drive/recalibrate
```json
// Response
{"track": 0, "steps": 42}
```

### POST /api/drive/side
```json
// Request
{"side": 0}
```

---

## Flux-Capture

### POST /api/read/track

Einzelnen Track lesen.

```json
// Request
{
    "track": 35,
    "side": 0,
    "revolutions": 3
}

// Response
{
    "track": 35,
    "side": 0,
    "format": "C64_GCR",
    "quality": 94.5,
    "rpm": 300.2,
    "flux_count": 49823,
    "index_time_ns": 199850000,
    "sectors": [
        {"number": 0, "size": 256, "crc_ok": true, "quality": 98.0},
        {"number": 1, "size": 256, "crc_ok": true, "quality": 95.5}
    ],
    "warnings": [],
    "protection": {"type": "none"}
}
```

### POST /api/read/disk

Komplette Disk lesen (asynchron).

```json
// Request
{
    "tracks": 80,
    "sides": 2,
    "revolutions": 3,
    "retry_threshold": 80,
    "max_retries": 3
}

// Response
{
    "job_id": "disk_20240117_143052",
    "status": "started"
}
```

### GET /api/read/status

Job-Status abrufen.

```json
{
    "job_id": "disk_20240117_143052",
    "status": "running",
    "progress": 45.5,
    "current_track": 36,
    "current_side": 0,
    "quality_avg": 93.2,
    "elapsed_seconds": 142,
    "eta_seconds": 174
}
```

### POST /api/read/abort

Job abbrechen.

---

## Disk-Daten

### GET /api/disk

Komplette Disk-Daten.

```json
{
    "disk_info": {
        "format": "C64_GCR",
        "total_tracks": 35,
        "sides": 1,
        "quality_avg": 94.2,
        "total_sectors": 683,
        "good_sectors": 680,
        "bad_sectors": 3
    },
    "tracks": {
        "1/0": {"quality": 98.5, "sectors": 21},
        "2/0": {"quality": 95.2, "sectors": 21}
    }
}
```

### GET /api/disk/track/{track}/{side}

Track-Details.

```json
{
    "track": 18,
    "side": 0,
    "format": "C64_GCR",
    "quality": 96.5,
    "sectors": [
        {
            "number": 0,
            "data": "base64...",
            "checksum_ok": true,
            "weak_bits": []
        }
    ]
}
```

### GET /api/disk/track/{track}/{side}/flux

Raw Flux-Daten.

Query-Parameter:
- `revolution`: 0-4 (optional)
- `format`: "json" oder "binary"

```json
{
    "track": 18,
    "side": 0,
    "sample_count": 49823,
    "samples": [
        {"delta_ns": 3245.5},
        {"delta_ns": 4012.3}
    ]
}
```

**Binary Format:**
```
Header (16 bytes):
  uint8:  track
  uint8:  side
  uint8:  revolution
  uint8:  flags
  uint32: index_time_ns
  uint32: sample_count
  uint32: reserved

Data:
  uint16[n]: delta_ticks (× 3.636 = ns)
```

---

## IEC Bus (C64)

### POST /api/iec/reset
```json
{"devices_found": [8, 9]}
```

### POST /api/iec/command
```json
// Request
{
    "device": 8,
    "command": "M-R",
    "data": [0x00, 0x03, 0x02]
}

// Response
{"response": [0x41, 0x42, 0x43], "status": 0}
```

### POST /api/iec/read_sector
```json
// Request
{"device": 8, "track": 18, "sector": 0}

// Response
{
    "track": 18,
    "sector": 0,
    "data": "base64_256_bytes...",
    "error": 0
}
```

---

## WebSocket API

### Verbindung
```
ws://ufi.local:5000/ws
```

### Client → Server

```json
// Subscribe
{"type": "subscribe", "data": {"events": ["progress", "status"]}}

// Disk lesen
{"type": "read_disk", "data": {"tracks": 80, "sides": 2}}

// Abbrechen
{"type": "abort", "data": {}}
```

### Server → Client

```json
// Progress
{"type": "progress", "data": {"track": 35, "side": 0, "progress": 43.75, "quality": 94.5}}

// Track fertig
{"type": "track_complete", "data": {"track": 35, "side": 0, "quality": 94.5}}

// Disk fertig
{"type": "disk_complete", "data": {"tracks_total": 160, "quality_avg": 93.8}}

// Fehler
{"type": "error", "data": {"code": 201, "message": "Track read failed"}}
```

---

## Fehler-Codes

| Code | Beschreibung |
|------|--------------|
| 100 | Allgemeiner Fehler |
| 101 | STM32 nicht verbunden |
| 102 | Ungültige Parameter |
| 200 | Laufwerk-Fehler |
| 201 | Track lesen fehlgeschlagen |
| 202 | Kein Laufwerk ausgewählt |
| 203 | Motor nicht an |
| 300 | IEC Bus Fehler |
| 301 | IEC Gerät nicht gefunden |

---

## Datentypen

### DiskFormat
```
UNKNOWN=0, PC_MFM_DD=1, PC_MFM_HD=2, AMIGA_DD=3, AMIGA_HD=4,
C64_GCR=5, C64_GCR_40=6, APPLE_II_GCR=7, ATARI_FM=8
```

### ProtectionType
```
none, weak_bits, timing, variable_density, custom
```

---

## Beispiel: C#

```csharp
var client = new HttpClient();
client.BaseAddress = new Uri("http://ufi.local:5000/api/");

// Laufwerk wählen
await client.PostAsJsonAsync("drive/select", new { drive = 5 });

// Motor an
await client.PostAsJsonAsync("drive/motor", new { on = true });

// Track lesen
var resp = await client.PostAsJsonAsync("read/track", new {
    track = 18, side = 0, revolutions = 3
});
var result = await resp.Content.ReadFromJsonAsync<TrackResult>();

// Motor aus
await client.PostAsJsonAsync("drive/motor", new { on = false });
```

---

## Beispiel: Delphi

```pascal
var
  Client: THTTPClient;
  Response: IHTTPResponse;
begin
  Client := THTTPClient.Create;
  
  // Track lesen
  Response := Client.Post(
    'http://ufi.local:5000/api/read/track',
    TStringStream.Create('{"track":18,"side":0,"revolutions":3}')
  );
  
  ShowMessage(Response.ContentAsString);
end;
```
