# UFI PC-Hauptprogramm Konzept

## Übersicht

Das PC-Hauptprogramm ist die zentrale Benutzeroberfläche und macht die finale Image-Erstellung. Es kommuniziert mit dem CM5 über das Netzwerk und empfängt bereits aufbereitete Daten.

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                              PC HAUPTPROGRAMM                                            │
├─────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                          │
│   ┌─────────────────────────────────────────────────────────────────────────────────┐   │
│   │                              GUI                                                 │   │
│   │                                                                                  │   │
│   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐           │   │
│   │   │   Dump      │  │   Archiv    │  │   Analyse   │  │   Export    │           │   │
│   │   │   Wizard    │  │   Browser   │  │   Tools     │  │   Optionen  │           │   │
│   │   └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘           │   │
│   │                                                                                  │   │
│   └─────────────────────────────────────────────────────────────────────────────────┘   │
│                                           │                                              │
│                                           ▼                                              │
│   ┌─────────────────────────────────────────────────────────────────────────────────┐   │
│   │                         CORE ENGINE                                              │   │
│   │                                                                                  │   │
│   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐           │   │
│   │   │   UFI       │  │   Image     │  │   Format    │  │   Database  │           │   │
│   │   │   Client    │  │   Builder   │  │   Plugins   │  │   Manager   │           │   │
│   │   └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘           │   │
│   │                                                                                  │   │
│   └─────────────────────────────────────────────────────────────────────────────────┘   │
│                                           │                                              │
│                                           ▼                                              │
│   ┌─────────────────────────────────────────────────────────────────────────────────┐   │
│   │                         DATEN-LAYER                                              │   │
│   │                                                                                  │   │
│   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                             │   │
│   │   │   SQLite    │  │   File      │  │   Cloud     │                             │   │
│   │   │   Archive   │  │   Storage   │  │   Sync      │                             │   │
│   │   └─────────────┘  └─────────────┘  └─────────────┘                             │   │
│   │                                                                                  │   │
│   └─────────────────────────────────────────────────────────────────────────────────┘   │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Komponenten

### 1. GUI Layer

#### Dump Wizard
```
┌─────────────────────────────────────────────────────────────┐
│                    DUMP WIZARD                               │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   Schritt 1: UFI auswählen                                  │
│   ┌─────────────────────────────────────────────────────┐  │
│   │ ○ UFI "Werkstatt" (192.168.1.50) - Online          │  │
│   │ ○ UFI "Labor" (192.168.1.51) - Offline             │  │
│   │ ○ UFI suchen...                                     │  │
│   └─────────────────────────────────────────────────────┘  │
│                                                             │
│   Schritt 2: Laufwerk wählen                               │
│   ┌─────────────────────────────────────────────────────┐  │
│   │ ○ FDD1 (5.25" DD)                                   │  │
│   │ ○ FDD2 (3.5" HD)                                    │  │
│   │ ○ Amiga External                                    │  │
│   │ ○ IEC (1541)                                        │  │
│   └─────────────────────────────────────────────────────┘  │
│                                                             │
│   Schritt 3: Format / Optionen                             │
│   ┌─────────────────────────────────────────────────────┐  │
│   │ Format: [Auto-Detect ▼]                             │  │
│   │ Tracks: [  80  ] Seiten: [2 ▼]                     │  │
│   │ ☑ Kopierschutz erhalten                            │  │
│   │ ☑ Mehrfach-Lesung (3x)                             │  │
│   │ ☐ Raw-Flux speichern                               │  │
│   └─────────────────────────────────────────────────────┘  │
│                                                             │
│   [    Dump starten    ]                                   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

#### Fortschritts-Anzeige
```
┌─────────────────────────────────────────────────────────────┐
│                    DISK DUMP                                 │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   Track: 35/80  Seite: 0                                   │
│   ████████████████████░░░░░░░░░░░░░░░░░░░░  44%            │
│                                                             │
│   ┌─────────────────────────────────────────────────────┐  │
│   │                 TRACK MAP                            │  │
│   │                                                      │  │
│   │   Seite 0: ████████████████████████████░░░░░░░░░░░  │  │
│   │   Seite 1: ██████████████████░░░░░░░░░░░░░░░░░░░░░  │  │
│   │                                                      │  │
│   │   ■ OK (>90%)  ▓ Warnung (70-90%)  □ Fehler (<70%)  │  │
│   └─────────────────────────────────────────────────────┘  │
│                                                             │
│   Qualität: 94.2%  |  Zeit: 2:35 / ~5:00  |  Fehler: 0    │
│                                                             │
│   Status: Lese Track 35, Seite 0 - Revolution 2/3          │
│                                                             │
│   [  Pause  ]  [  Abbrechen  ]                             │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

#### Archiv Browser
```
┌─────────────────────────────────────────────────────────────┐
│                    ARCHIV                                    │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   Suche: [                    ] [🔍]  Filter: [Alle ▼]     │
│                                                             │
│   ┌─────────────────────────────────────────────────────┐  │
│   │ ┌──────────────────────────────────────────────────┐│  │
│   │ │📀 Marble Madness (Amiga)           98.5%  ADF  ││  │
│   │ │   1990, Electronic Arts                         ││  │
│   │ │   Kopierschutz: Rob Northen                     ││  │
│   │ ├──────────────────────────────────────────────────┤│  │
│   │ │📀 Katakis (C64)                    95.2%  G64  ││  │
│   │ │   1988, Rainbow Arts                            ││  │
│   │ │   Kopierschutz: Keine                           ││  │
│   │ ├──────────────────────────────────────────────────┤│  │
│   │ │📀 Prince of Persia (PC)            99.1%  IMG  ││  │
│   │ │   1989, Broderbund                              ││  │
│   │ │   Kopierschutz: Keine                           ││  │
│   │ └──────────────────────────────────────────────────┘│  │
│   └─────────────────────────────────────────────────────┘  │
│                                                             │
│   [  Export  ]  [  Verifizieren  ]  [  Löschen  ]         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

### 2. Core Engine

#### UFI Client
Kommunikation mit CM5 über REST API.

```python
class UFIClient:
    """Verbindung zu einem UFI-Gerät"""
    
    def __init__(self, host: str, port: int = 5000):
        self.base_url = f"http://{host}:{port}/api"
        self.session = aiohttp.ClientSession()
    
    async def get_status(self) -> dict:
        """System-Status abrufen"""
        async with self.session.get(f"{self.base_url}/status") as resp:
            return await resp.json()
    
    async def read_track(self, track: int, side: int) -> dict:
        """Einzelnen Track lesen"""
        async with self.session.post(
            f"{self.base_url}/read/track",
            json={'track': track, 'side': side, 'revolutions': 3}
        ) as resp:
            return await resp.json()
    
    async def read_disk(self, tracks: int = 80, sides: int = 2, 
                        progress_callback=None) -> dict:
        """Komplette Disk lesen mit Progress-Updates"""
        # WebSocket für Echtzeit-Updates
        async with self.session.ws_connect(f"{self.base_url}/ws") as ws:
            await ws.send_json({
                'command': 'read_disk',
                'tracks': tracks,
                'sides': sides
            })
            
            while True:
                msg = await ws.receive_json()
                if msg['type'] == 'progress':
                    if progress_callback:
                        progress_callback(msg)
                elif msg['type'] == 'complete':
                    return msg['data']
                elif msg['type'] == 'error':
                    raise Exception(msg['error'])
    
    async def get_disk_data(self) -> dict:
        """Aufbereitete Disk-Daten vom CM5 holen"""
        async with self.session.get(f"{self.base_url}/disk") as resp:
            return await resp.json()
```

#### Image Builder
Finale Image-Erstellung aus aufbereiteten Daten.

```python
class ImageBuilder:
    """Erstellt Disk-Images aus CM5-Daten"""
    
    def __init__(self):
        self.format_handlers = {
            'D64': D64Builder(),
            'G64': G64Builder(),
            'ADF': ADFBuilder(),
            'IPF': IPFBuilder(),
            'SCP': SCPBuilder(),
            'IMG': IMGBuilder(),
        }
    
    def build(self, disk_data: dict, format: str) -> bytes:
        """Image erstellen"""
        handler = self.format_handlers.get(format)
        if not handler:
            raise ValueError(f"Unbekanntes Format: {format}")
        
        return handler.build(disk_data)
    
    def auto_select_format(self, disk_data: dict) -> str:
        """Bestes Format automatisch wählen"""
        disk_format = disk_data['disk_info']['format']
        
        format_map = {
            'C64_GCR': 'G64',      # Mit Timing-Info
            'AMIGA_DD': 'ADF',     # Standard Amiga
            'AMIGA_HD': 'ADF',
            'PC_MFM_DD': 'IMG',    # Sektor-Image
            'PC_MFM_HD': 'IMG',
            'APPLE_II_GCR': 'NIB', # Apple Nibble
        }
        
        # Wenn Kopierschutz: IPF oder SCP
        if disk_data.get('has_protection'):
            return 'IPF' if disk_format.startswith('AMIGA') else 'SCP'
        
        return format_map.get(disk_format, 'SCP')


class D64Builder:
    """C64 D64 Image Builder"""
    
    TRACK_SECTORS = [21]*17 + [19]*7 + [18]*6 + [17]*5  # Sektoren pro Track
    
    def build(self, disk_data: dict) -> bytes:
        image = bytearray()
        
        for track in range(1, 36):  # Tracks 1-35
            sectors = self.TRACK_SECTORS[track - 1]
            for sector in range(sectors):
                # Sektor-Daten aus disk_data
                sector_data = self._get_sector(disk_data, track, sector)
                image.extend(sector_data)
        
        return bytes(image)
    
    def _get_sector(self, disk_data: dict, track: int, sector: int) -> bytes:
        # Track-Daten holen (von CM5 bereits dekodiert)
        track_data = disk_data['tracks'].get(f"{track}/0")
        if track_data and sector < len(track_data.get('sectors', [])):
            return track_data['sectors'][sector]['data']
        return bytes(256)  # Leerer Sektor


class G64Builder:
    """C64 G64 Image Builder (mit Timing)"""
    
    def build(self, disk_data: dict) -> bytes:
        # G64 Header
        header = b'GCR-1541'
        header += struct.pack('<B', 0)      # Version
        header += struct.pack('<B', 84)     # Tracks (42 * 2 half-tracks)
        header += struct.pack('<H', 7928)   # Max track size
        
        # Track offsets table
        offsets = []
        current_offset = 12 + 84 * 4 + 84 * 4  # Nach Header und Tabellen
        
        tracks_data = []
        for half_track in range(84):
            track = half_track // 2 + 1
            if half_track % 2 == 0:  # Nur ganze Tracks
                track_key = f"{track}/0"
                if track_key in disk_data['tracks']:
                    flux = disk_data['tracks'][track_key].get('raw_flux', b'')
                    offsets.append(current_offset)
                    tracks_data.append(flux)
                    current_offset += len(flux) + 2
                else:
                    offsets.append(0)
            else:
                offsets.append(0)  # Half-tracks leer
        
        # Speed zone table
        speed_zones = [3] * 17 + [2] * 7 + [1] * 6 + [0] * 5
        
        # Image zusammenbauen
        image = bytearray(header)
        
        # Offset table
        for offset in offsets:
            image.extend(struct.pack('<I', offset))
        
        # Speed table
        for i, zone in enumerate(speed_zones):
            if i * 2 < 84:
                image.extend(struct.pack('<I', zone))
        
        # Track data
        for track_data in tracks_data:
            image.extend(struct.pack('<H', len(track_data)))
            image.extend(track_data)
        
        return bytes(image)


class ADFBuilder:
    """Amiga ADF Image Builder"""
    
    def build(self, disk_data: dict) -> bytes:
        # ADF: 80 Tracks * 2 Seiten * 11 Sektoren * 512 Bytes
        image = bytearray(80 * 2 * 11 * 512)
        
        for track in range(80):
            for side in range(2):
                track_key = f"{track}/{side}"
                track_data = disk_data['tracks'].get(track_key, {})
                
                for sector in range(11):
                    offset = ((track * 2 + side) * 11 + sector) * 512
                    
                    if 'sectors' in track_data and sector < len(track_data['sectors']):
                        sector_data = track_data['sectors'][sector].get('data', b'')
                        image[offset:offset + 512] = sector_data[:512].ljust(512, b'\x00')
        
        return bytes(image)
```

#### Format Plugins
Erweiterbare Format-Unterstützung.

```python
# plugins/format_ipf.py
class IPFPlugin:
    """CAPS/IPF Format für Amiga mit Kopierschutz"""
    
    NAME = "IPF (CAPS)"
    EXTENSIONS = ['.ipf']
    DESCRIPTION = "Interchangeable Preservation Format - Amiga mit Kopierschutz"
    
    def can_handle(self, disk_data: dict) -> bool:
        return disk_data['disk_info']['format'].startswith('AMIGA')
    
    def build(self, disk_data: dict) -> bytes:
        # IPF erfordert CAPS Library
        # TODO: Implementierung mit libcapsimage
        pass


# plugins/format_scp.py  
class SCPPlugin:
    """SuperCard Pro Format (Raw Flux)"""
    
    NAME = "SCP (SuperCard Pro)"
    EXTENSIONS = ['.scp']
    DESCRIPTION = "Raw Flux Format - alle Systeme"
    
    def build(self, disk_data: dict) -> bytes:
        # SCP Header
        header = b'SCP'
        header += struct.pack('<B', 0x18)  # Version
        header += struct.pack('<B', 0)     # Disk type
        header += struct.pack('<B', 2)     # Revolutions
        header += struct.pack('<B', 0)     # Start track
        header += struct.pack('<B', 83)    # End track
        header += struct.pack('<B', 0)     # Flags
        header += struct.pack('<B', 0)     # Bit cell encoding
        header += struct.pack('<B', 2)     # Heads
        header += struct.pack('<B', 0)     # Resolution
        header += struct.pack('<I', 0)     # Checksum (später)
        
        # Track data offset table (168 entries für 84 tracks * 2 heads)
        # TODO: Vollständige SCP Implementierung
        
        return header
```

#### Database Manager
SQLite-basierte Archiv-Verwaltung.

```python
class DatabaseManager:
    """Disk-Image Archiv mit SQLite"""
    
    def __init__(self, db_path: str):
        self.conn = sqlite3.connect(db_path)
        self._init_schema()
    
    def _init_schema(self):
        self.conn.executescript('''
            CREATE TABLE IF NOT EXISTS disks (
                id INTEGER PRIMARY KEY,
                title TEXT,
                system TEXT,
                year INTEGER,
                publisher TEXT,
                format TEXT,
                protection TEXT,
                quality REAL,
                hash_md5 TEXT,
                hash_sha1 TEXT,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                notes TEXT
            );
            
            CREATE TABLE IF NOT EXISTS disk_files (
                id INTEGER PRIMARY KEY,
                disk_id INTEGER REFERENCES disks(id),
                filename TEXT,
                format TEXT,
                size INTEGER,
                path TEXT
            );
            
            CREATE TABLE IF NOT EXISTS dumps (
                id INTEGER PRIMARY KEY,
                disk_id INTEGER REFERENCES disks(id),
                ufi_device TEXT,
                drive_type TEXT,
                dump_date DATETIME,
                quality REAL,
                raw_flux_path TEXT
            );
            
            CREATE INDEX IF NOT EXISTS idx_disks_title ON disks(title);
            CREATE INDEX IF NOT EXISTS idx_disks_system ON disks(system);
            CREATE INDEX IF NOT EXISTS idx_disks_hash ON disks(hash_md5);
        ''')
    
    def add_disk(self, title: str, system: str, image_data: bytes, 
                 format: str, **metadata) -> int:
        """Neue Disk zum Archiv hinzufügen"""
        hash_md5 = hashlib.md5(image_data).hexdigest()
        hash_sha1 = hashlib.sha1(image_data).hexdigest()
        
        # Duplikat-Prüfung
        existing = self.conn.execute(
            'SELECT id FROM disks WHERE hash_md5 = ?', (hash_md5,)
        ).fetchone()
        
        if existing:
            return existing[0]  # Bereits vorhanden
        
        cursor = self.conn.execute('''
            INSERT INTO disks (title, system, format, hash_md5, hash_sha1,
                              year, publisher, protection, quality, notes)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ''', (
            title, system, format, hash_md5, hash_sha1,
            metadata.get('year'), metadata.get('publisher'),
            metadata.get('protection'), metadata.get('quality'),
            metadata.get('notes')
        ))
        
        self.conn.commit()
        return cursor.lastrowid
    
    def search(self, query: str = '', system: str = None) -> List[dict]:
        """Disks suchen"""
        sql = 'SELECT * FROM disks WHERE 1=1'
        params = []
        
        if query:
            sql += ' AND (title LIKE ? OR publisher LIKE ?)'
            params.extend([f'%{query}%', f'%{query}%'])
        
        if system:
            sql += ' AND system = ?'
            params.append(system)
        
        sql += ' ORDER BY title'
        
        cursor = self.conn.execute(sql, params)
        columns = [d[0] for d in cursor.description]
        return [dict(zip(columns, row)) for row in cursor.fetchall()]
    
    def verify_integrity(self, disk_id: int, image_data: bytes) -> bool:
        """Image gegen Datenbank verifizieren"""
        disk = self.conn.execute(
            'SELECT hash_md5 FROM disks WHERE id = ?', (disk_id,)
        ).fetchone()
        
        if not disk:
            return False
        
        return hashlib.md5(image_data).hexdigest() == disk[0]
```

---

### 3. Haupt-Workflow

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                              DUMP WORKFLOW                                               │
├─────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                          │
│   ┌─────────┐     ┌─────────┐     ┌─────────┐     ┌─────────┐     ┌─────────┐          │
│   │         │     │         │     │         │     │         │     │         │          │
│   │   UFI   │────►│   CM5   │────►│   PC    │────►│  Image  │────►│ Archiv  │          │
│   │  lesen  │     │ verarbeiten   │empfangen│     │ erstellen     │speichern│          │
│   │         │     │         │     │         │     │         │     │         │          │
│   └─────────┘     └─────────┘     └─────────┘     └─────────┘     └─────────┘          │
│                                                                                          │
│   1. User klickt "Dump"                                                                 │
│   2. PC sendet Befehl an CM5                                                            │
│   3. CM5 steuert STM32 → liest Flux                                                     │
│   4. CM5 verarbeitet: Puffern, Algorithmen, Verbessern                                 │
│   5. CM5 sendet aufbereitete Daten an PC                                               │
│   6. PC erstellt finales Image (D64, ADF, etc.)                                        │
│   7. PC speichert in Archiv mit Metadaten                                              │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Technologie-Stack

| Komponente | Technologie |
|------------|-------------|
| GUI Framework | Python + Qt6 / PyQt6 |
| HTTP Client | aiohttp |
| Datenbank | SQLite |
| Image Parsing | Custom + NumPy |
| Packaging | PyInstaller (Windows/macOS/Linux) |

---

## Deployment

```bash
# Installation
pip install ufi-dumper

# Start
ufi-dumper

# oder als Portable App
./UFI-Dumper.exe        # Windows
./UFI-Dumper.app        # macOS
./ufi-dumper            # Linux
```

---

## Konfiguration

```yaml
# ~/.ufi/config.yaml

# Bekannte UFI-Geräte
devices:
  - name: "Werkstatt"
    host: "192.168.1.50"
    port: 5000
  - name: "Labor"
    host: "ufi-labor.local"
    port: 5000

# Archiv-Pfad
archive:
  database: "~/.ufi/archive.db"
  images: "~/UFI-Images"
  raw_flux: "~/UFI-Raw"  # Optional

# Standard-Optionen
defaults:
  revolutions: 3
  preserve_protection: true
  auto_retry: true
  retry_threshold: 80  # Qualität %

# Format-Präferenzen
formats:
  c64: "g64"      # Mit Timing
  amiga: "adf"    # Standard
  pc: "img"       # Sektor-Image
  protected: "scp" # Raw für Kopierschutz
```
