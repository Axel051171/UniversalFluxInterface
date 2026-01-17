#!/usr/bin/env python3
"""
UFI CM5 Processing Layer
========================

Aufgaben:
- Flux-Daten vom STM32 empfangen und puffern
- Routinen & Algorithmen für Datenverbesserung
- Aufbereitete Daten an PC-Hauptprogramm senden
- Web-Interface für Status/Debug
"""

import asyncio
import struct
import logging
import json
import hashlib
from typing import List, Dict, Optional, Tuple
from dataclasses import dataclass, field
from enum import IntEnum
from pathlib import Path
import numpy as np

# USB Kommunikation mit STM32
import usb.core
import usb.util

# Web Framework
from aiohttp import web
import aiohttp_cors

# ============================================================================
# KONFIGURATION
# ============================================================================

UFI_VID = 0x1209  # pid.codes VID
UFI_PID = 0x4F54  # "OT" für Open Tool

USB_EP_IN = 0x81   # Flux-Daten vom STM32
USB_EP_OUT = 0x02  # Befehle zum STM32

FLUX_CLOCK_HZ = 275_000_000  # STM32 Timer Clock
FLUX_NS_PER_TICK = 1e9 / FLUX_CLOCK_HZ  # ~3.6ns

# Standard-Timings (ns)
MFM_CELL_NS = 2000      # MFM Bit-Cell (500 kbit/s)
GCR_CELL_NS = 4000      # GCR Bit-Cell (C64/Amiga)

logging.basicConfig(level=logging.INFO)
log = logging.getLogger("ufi-cm5")

# ============================================================================
# DATENSTRUKTUREN
# ============================================================================

class DiskFormat(IntEnum):
    """Unterstützte Disk-Formate"""
    UNKNOWN = 0
    PC_MFM_DD = 1       # PC 720K
    PC_MFM_HD = 2       # PC 1.44M
    AMIGA_DD = 3        # Amiga 880K
    AMIGA_HD = 4        # Amiga 1.76M
    C64_GCR = 5         # C64 170K
    C64_GCR_40 = 6      # C64 40 Track
    APPLE_II_GCR = 7    # Apple II 140K
    ATARI_FM = 8        # Atari 8-bit


@dataclass
class FluxSample:
    """Ein Flux-Übergang"""
    timestamp: int      # Timer-Wert (Ticks)
    delta_ns: float = 0 # Zeit zum vorherigen (ns)


@dataclass
class FluxRevolution:
    """Eine Disk-Umdrehung"""
    samples: List[FluxSample]
    index_time: int
    revolution: int
    duration_ns: float = 0
    rpm: float = 0


@dataclass
class FluxTrack:
    """Ein kompletter Track (mehrere Revolutions)"""
    track: int
    side: int
    revolutions: List[FluxRevolution]
    format: DiskFormat = DiskFormat.UNKNOWN
    quality_score: float = 0
    sectors: List['Sector'] = field(default_factory=list)


@dataclass
class Sector:
    """Ein dekodierter Sektor"""
    number: int
    data: bytes
    crc_ok: bool
    quality: float
    weak_bits: List[int] = field(default_factory=list)


@dataclass
class ProcessingResult:
    """Ergebnis der CM5-Verarbeitung"""
    track: int
    side: int
    format: DiskFormat
    quality_score: float
    sectors: List[Sector]
    raw_flux: Optional[FluxTrack] = None
    warnings: List[str] = field(default_factory=list)
    protection_info: Dict = field(default_factory=dict)


# ============================================================================
# USB KOMMUNIKATION MIT STM32
# ============================================================================

class STM32Connection:
    """USB Verbindung zum STM32 Flux Engine"""
    
    def __init__(self):
        self.dev = None
        self.ep_in = None
        self.ep_out = None
    
    def connect(self) -> bool:
        """Verbindung zum STM32 herstellen"""
        self.dev = usb.core.find(idVendor=UFI_VID, idProduct=UFI_PID)
        if self.dev is None:
            log.error("STM32 nicht gefunden!")
            return False
        
        self.dev.set_configuration()
        cfg = self.dev.get_active_configuration()
        intf = cfg[(0, 0)]
        
        self.ep_in = usb.util.find_descriptor(
            intf, custom_match=lambda e: e.bEndpointAddress == USB_EP_IN
        )
        self.ep_out = usb.util.find_descriptor(
            intf, custom_match=lambda e: e.bEndpointAddress == USB_EP_OUT
        )
        
        log.info("STM32 verbunden")
        return True
    
    def send_command(self, cmd: int, data: bytes = b'') -> bytes:
        """Befehl an STM32 senden und Antwort empfangen"""
        packet = struct.pack('<BH', cmd, len(data)) + data
        self.ep_out.write(packet)
        
        # Antwort lesen
        response = self.ep_in.read(4, timeout=5000)
        cmd_echo, status, length = struct.unpack('<BBH', bytes(response))
        
        if status != 0:
            raise Exception(f"STM32 Fehler: {status}")
        
        if length > 0:
            return bytes(self.ep_in.read(length, timeout=5000))
        return b''
    
    def read_track(self, track: int, side: int, revolutions: int = 3) -> FluxTrack:
        """Track vom Laufwerk lesen"""
        # Befehl senden
        data = struct.pack('<BBB', track, side, revolutions)
        self.send_command(0x21, data)  # UFI_CMD_READ_TRACK_RAW
        
        # Flux-Daten empfangen
        flux_track = FluxTrack(track=track, side=side, revolutions=[])
        
        for _ in range(revolutions):
            # Header lesen
            header_data = self.ep_in.read(16, timeout=10000)
            trk, sid, rev, flags, idx_time, sample_count = struct.unpack(
                '<BBBBII', bytes(header_data)
            )
            
            # Samples lesen
            samples_data = self.ep_in.read(sample_count * 4, timeout=10000)
            timestamps = struct.unpack(f'<{sample_count}I', bytes(samples_data))
            
            # Revolution erstellen
            samples = [FluxSample(timestamp=t) for t in timestamps]
            flux_rev = FluxRevolution(
                samples=samples,
                index_time=idx_time,
                revolution=rev
            )
            flux_track.revolutions.append(flux_rev)
        
        return flux_track


# ============================================================================
# ALGORITHMEN & ROUTINEN
# ============================================================================

class FluxProcessor:
    """Kern-Verarbeitungslogik für Flux-Daten"""
    
    def __init__(self):
        self.format_detectors = {
            DiskFormat.PC_MFM_DD: self._detect_pc_mfm,
            DiskFormat.AMIGA_DD: self._detect_amiga,
            DiskFormat.C64_GCR: self._detect_c64_gcr,
        }
    
    # ========================================================================
    # TIMING-NORMALISIERUNG
    # ========================================================================
    
    def normalize_timing(self, track: FluxTrack) -> FluxTrack:
        """Timing-Normalisierung: Drehzahl-Schwankungen ausgleichen"""
        log.info(f"Timing-Normalisierung Track {track.track}/{track.side}")
        
        for rev in track.revolutions:
            if len(rev.samples) < 2:
                continue
            
            # Delta-Zeiten berechnen
            prev_ts = 0
            for sample in rev.samples:
                delta_ticks = sample.timestamp - prev_ts
                sample.delta_ns = delta_ticks * FLUX_NS_PER_TICK
                prev_ts = sample.timestamp
            
            # Umdrehungs-Dauer und RPM
            rev.duration_ns = rev.index_time * FLUX_NS_PER_TICK
            if rev.duration_ns > 0:
                rev.rpm = 60e9 / rev.duration_ns
            
            # Auf Referenz-RPM normalisieren (300 RPM für DD)
            if rev.rpm > 0:
                scale = 300.0 / rev.rpm
                for sample in rev.samples:
                    sample.delta_ns *= scale
        
        return track
    
    # ========================================================================
    # FEHLER-ERKENNUNG
    # ========================================================================
    
    def detect_errors(self, track: FluxTrack) -> List[str]:
        """Fehler im Track erkennen"""
        errors = []
        
        for rev in track.revolutions:
            # RPM prüfen
            if rev.rpm < 280 or rev.rpm > 320:
                errors.append(f"Rev {rev.revolution}: Abnormale Drehzahl {rev.rpm:.1f} RPM")
            
            # Flux-Dichte prüfen
            expected_flux = 50000 if track.format in [DiskFormat.PC_MFM_DD, DiskFormat.AMIGA_DD] else 40000
            if len(rev.samples) < expected_flux * 0.8:
                errors.append(f"Rev {rev.revolution}: Zu wenig Flux-Übergänge ({len(rev.samples)})")
            
            # Timing-Ausreißer finden
            deltas = [s.delta_ns for s in rev.samples if s.delta_ns > 0]
            if deltas:
                median = np.median(deltas)
                for i, d in enumerate(deltas):
                    if d > median * 3 or d < median * 0.3:
                        errors.append(f"Rev {rev.revolution}: Timing-Anomalie bei Sample {i}")
        
        return errors
    
    # ========================================================================
    # MULTI-READ KOMBINATION
    # ========================================================================
    
    def combine_revolutions(self, track: FluxTrack) -> FluxRevolution:
        """Mehrere Umdrehungen zu einer optimalen kombinieren"""
        log.info(f"Kombiniere {len(track.revolutions)} Umdrehungen")
        
        if len(track.revolutions) == 1:
            return track.revolutions[0]
        
        # Referenz: Erste Revolution
        ref_rev = track.revolutions[0]
        best_samples = list(ref_rev.samples)
        
        # Für jede Position: Bestes Sample aus allen Revolutions wählen
        for i in range(len(best_samples)):
            candidates = []
            
            for rev in track.revolutions:
                if i < len(rev.samples):
                    candidates.append(rev.samples[i])
            
            if len(candidates) > 1:
                # Median-Timing wählen (robuster gegen Ausreißer)
                deltas = [c.delta_ns for c in candidates]
                median_delta = np.median(deltas)
                
                # Sample mit geringstem Abstand zum Median
                best_idx = min(range(len(candidates)), 
                              key=lambda x: abs(candidates[x].delta_ns - median_delta))
                best_samples[i] = candidates[best_idx]
        
        combined = FluxRevolution(
            samples=best_samples,
            index_time=ref_rev.index_time,
            revolution=99,  # Markiert als kombiniert
            duration_ns=ref_rev.duration_ns,
            rpm=ref_rev.rpm
        )
        
        return combined
    
    # ========================================================================
    # WEAK-BITS ERKENNUNG
    # ========================================================================
    
    def detect_weak_bits(self, track: FluxTrack) -> List[int]:
        """Weak-Bits durch Vergleich mehrerer Lesungen finden"""
        log.info("Suche Weak-Bits...")
        
        weak_positions = []
        
        if len(track.revolutions) < 2:
            return weak_positions
        
        ref_rev = track.revolutions[0]
        
        for i in range(min(len(r.samples) for r in track.revolutions)):
            deltas = [r.samples[i].delta_ns for r in track.revolutions]
            
            # Hohe Varianz = Weak-Bit
            variance = np.var(deltas)
            mean = np.mean(deltas)
            
            if mean > 0 and variance / mean > 0.1:  # >10% Varianz
                weak_positions.append(i)
        
        if weak_positions:
            log.info(f"  {len(weak_positions)} Weak-Bits gefunden")
        
        return weak_positions
    
    # ========================================================================
    # KOPIERSCHUTZ-ANALYSE
    # ========================================================================
    
    def analyze_protection(self, track: FluxTrack) -> Dict:
        """Kopierschutz-Mechanismen erkennen"""
        log.info("Analysiere Kopierschutz...")
        
        protection = {
            'type': 'none',
            'details': {}
        }
        
        # Weak-Bits (häufig bei Kopierschutz)
        weak_bits = self.detect_weak_bits(track)
        if len(weak_bits) > 10:
            protection['type'] = 'weak_bits'
            protection['details']['weak_count'] = len(weak_bits)
            protection['details']['positions'] = weak_bits[:20]  # Erste 20
        
        # Timing-Anomalien (z.B. lange Gaps)
        for rev in track.revolutions:
            deltas = [s.delta_ns for s in rev.samples]
            if deltas:
                max_delta = max(deltas)
                median = np.median(deltas)
                
                if max_delta > median * 5:
                    protection['type'] = 'timing_protection'
                    protection['details']['max_gap_ns'] = max_delta
        
        # Track-Längen-Variation (z.B. bei Track 6-7)
        flux_counts = [len(r.samples) for r in track.revolutions]
        if max(flux_counts) - min(flux_counts) > 1000:
            protection['type'] = 'variable_density'
            protection['details']['flux_variation'] = max(flux_counts) - min(flux_counts)
        
        return protection
    
    # ========================================================================
    # FORMAT-ERKENNUNG
    # ========================================================================
    
    def detect_format(self, track: FluxTrack) -> DiskFormat:
        """Disk-Format automatisch erkennen"""
        log.info("Erkenne Disk-Format...")
        
        if not track.revolutions or not track.revolutions[0].samples:
            return DiskFormat.UNKNOWN
        
        rev = track.revolutions[0]
        
        # Durchschnittliche Bitcell-Zeit
        deltas = [s.delta_ns for s in rev.samples if s.delta_ns > 0]
        if not deltas:
            return DiskFormat.UNKNOWN
        
        avg_delta = np.mean(deltas)
        
        # MFM: ~2000ns (kurz), ~3000ns (mittel), ~4000ns (lang)
        # GCR: ~3200ns, ~3500ns, ~4000ns, ~4500ns
        
        if 1500 < avg_delta < 2500:
            # Wahrscheinlich MFM
            if len(rev.samples) > 80000:
                return DiskFormat.PC_MFM_HD
            else:
                return DiskFormat.PC_MFM_DD
        
        elif 2500 < avg_delta < 4000:
            # Amiga oder C64
            if rev.rpm > 290 and rev.rpm < 310:
                return DiskFormat.AMIGA_DD
            else:
                return DiskFormat.C64_GCR
        
        elif 3500 < avg_delta < 5000:
            return DiskFormat.APPLE_II_GCR
        
        return DiskFormat.UNKNOWN
    
    def _detect_pc_mfm(self, track: FluxTrack) -> bool:
        """PC MFM Format erkennen"""
        # TODO: Sync-Pattern suchen (0x4489)
        return True
    
    def _detect_amiga(self, track: FluxTrack) -> bool:
        """Amiga Format erkennen"""
        # TODO: Amiga Sync suchen (0x4489 4489)
        return True
    
    def _detect_c64_gcr(self, track: FluxTrack) -> bool:
        """C64 GCR Format erkennen"""
        # TODO: GCR Sync suchen
        return True
    
    # ========================================================================
    # QUALITÄTS-BEWERTUNG
    # ========================================================================
    
    def calculate_quality(self, track: FluxTrack) -> float:
        """Qualitäts-Score berechnen (0-100)"""
        score = 100.0
        
        # RPM-Abweichung
        for rev in track.revolutions:
            rpm_deviation = abs(300 - rev.rpm)
            score -= rpm_deviation * 0.5
        
        # Fehler-Anzahl
        errors = self.detect_errors(track)
        score -= len(errors) * 5
        
        # Weak-Bits
        weak_bits = self.detect_weak_bits(track)
        score -= len(weak_bits) * 0.1
        
        # Konsistenz zwischen Revolutions
        if len(track.revolutions) > 1:
            counts = [len(r.samples) for r in track.revolutions]
            variance = np.var(counts) / np.mean(counts) if np.mean(counts) > 0 else 0
            score -= variance * 10
        
        return max(0, min(100, score))
    
    # ========================================================================
    # HAUPTVERARBEITUNG
    # ========================================================================
    
    def process_track(self, track: FluxTrack) -> ProcessingResult:
        """Komplette Track-Verarbeitung"""
        log.info(f"Verarbeite Track {track.track}/{track.side}")
        
        # 1. Timing normalisieren
        track = self.normalize_timing(track)
        
        # 2. Format erkennen
        track.format = self.detect_format(track)
        log.info(f"  Format: {track.format.name}")
        
        # 3. Fehler erkennen
        warnings = self.detect_errors(track)
        
        # 4. Multi-Read kombinieren
        combined = self.combine_revolutions(track)
        
        # 5. Kopierschutz analysieren
        protection = self.analyze_protection(track)
        
        # 6. Qualität bewerten
        quality = self.calculate_quality(track)
        log.info(f"  Qualität: {quality:.1f}%")
        
        # Ergebnis
        return ProcessingResult(
            track=track.track,
            side=track.side,
            format=track.format,
            quality_score=quality,
            sectors=[],  # TODO: Sektor-Dekodierung
            raw_flux=track,
            warnings=warnings,
            protection_info=protection
        )


# ============================================================================
# DISK BUFFER (Cache für komplette Disk)
# ============================================================================

class DiskBuffer:
    """Puffert eine komplette Disk im Speicher"""
    
    def __init__(self):
        self.tracks: Dict[Tuple[int, int], ProcessingResult] = {}
        self.disk_info = {
            'format': DiskFormat.UNKNOWN,
            'total_tracks': 0,
            'sides': 2,
            'quality_avg': 0
        }
    
    def add_track(self, result: ProcessingResult):
        """Track zum Buffer hinzufügen"""
        key = (result.track, result.side)
        self.tracks[key] = result
        self._update_disk_info()
    
    def get_track(self, track: int, side: int) -> Optional[ProcessingResult]:
        """Track aus Buffer holen"""
        return self.tracks.get((track, side))
    
    def _update_disk_info(self):
        """Disk-Info aktualisieren"""
        if not self.tracks:
            return
        
        # Format von Track 0/0
        if (0, 0) in self.tracks:
            self.disk_info['format'] = self.tracks[(0, 0)].format
        
        # Track-Anzahl
        self.disk_info['total_tracks'] = max(t[0] for t in self.tracks.keys()) + 1
        
        # Durchschnittliche Qualität
        qualities = [t.quality_score for t in self.tracks.values()]
        self.disk_info['quality_avg'] = np.mean(qualities) if qualities else 0
    
    def to_json(self) -> Dict:
        """Buffer als JSON für PC-Übertragung"""
        return {
            'disk_info': self.disk_info,
            'tracks': [
                {
                    'track': r.track,
                    'side': r.side,
                    'format': r.format.name,
                    'quality': r.quality_score,
                    'sectors': len(r.sectors),
                    'warnings': r.warnings,
                    'protection': r.protection_info
                }
                for r in self.tracks.values()
            ]
        }


# ============================================================================
# WEB API (für PC-Kommunikation)
# ============================================================================

class WebAPI:
    """REST API für PC-Hauptprogramm"""
    
    def __init__(self, processor: FluxProcessor, buffer: DiskBuffer, stm32: STM32Connection):
        self.processor = processor
        self.buffer = buffer
        self.stm32 = stm32
        self.app = web.Application()
        self._setup_routes()
    
    def _setup_routes(self):
        """API Routen konfigurieren"""
        self.app.router.add_get('/api/status', self.get_status)
        self.app.router.add_get('/api/disk', self.get_disk_info)
        self.app.router.add_get('/api/track/{track}/{side}', self.get_track)
        self.app.router.add_post('/api/read/track', self.read_track)
        self.app.router.add_post('/api/read/disk', self.read_disk)
        self.app.router.add_post('/api/drive/select', self.drive_select)
        self.app.router.add_post('/api/drive/motor', self.drive_motor)
        
        # CORS für PC-Zugriff
        cors = aiohttp_cors.setup(self.app, defaults={
            "*": aiohttp_cors.ResourceOptions(
                allow_credentials=True,
                expose_headers="*",
                allow_headers="*"
            )
        })
        for route in list(self.app.router.routes()):
            cors.add(route)
    
    async def get_status(self, request):
        """System-Status abrufen"""
        return web.json_response({
            'connected': self.stm32.dev is not None,
            'buffer_tracks': len(self.buffer.tracks),
            'disk_info': self.buffer.disk_info
        })
    
    async def get_disk_info(self, request):
        """Disk-Informationen abrufen"""
        return web.json_response(self.buffer.to_json())
    
    async def get_track(self, request):
        """Einzelnen Track abrufen"""
        track = int(request.match_info['track'])
        side = int(request.match_info['side'])
        
        result = self.buffer.get_track(track, side)
        if result is None:
            return web.json_response({'error': 'Track nicht im Buffer'}, status=404)
        
        return web.json_response({
            'track': result.track,
            'side': result.side,
            'format': result.format.name,
            'quality': result.quality_score,
            'warnings': result.warnings,
            'protection': result.protection_info,
            # Optional: Raw-Flux als Base64
        })
    
    async def read_track(self, request):
        """Track lesen und verarbeiten"""
        data = await request.json()
        track = data.get('track', 0)
        side = data.get('side', 0)
        revolutions = data.get('revolutions', 3)
        
        # Von STM32 lesen
        flux_track = self.stm32.read_track(track, side, revolutions)
        
        # Verarbeiten
        result = self.processor.process_track(flux_track)
        
        # Puffern
        self.buffer.add_track(result)
        
        return web.json_response({
            'track': result.track,
            'side': result.side,
            'quality': result.quality_score,
            'format': result.format.name
        })
    
    async def read_disk(self, request):
        """Komplette Disk lesen"""
        data = await request.json()
        max_tracks = data.get('tracks', 80)
        sides = data.get('sides', 2)
        
        # Disk lesen (async für Progress-Updates)
        for track in range(max_tracks):
            for side in range(sides):
                flux_track = self.stm32.read_track(track, side, 3)
                result = self.processor.process_track(flux_track)
                self.buffer.add_track(result)
                
                # Bei schlechter Qualität: Retry
                if result.quality_score < 80:
                    log.warning(f"Track {track}/{side}: Retry wegen Qualität {result.quality_score:.0f}%")
                    flux_track = self.stm32.read_track(track, side, 5)
                    result = self.processor.process_track(flux_track)
                    self.buffer.add_track(result)
        
        return web.json_response(self.buffer.to_json())
    
    async def drive_select(self, request):
        """Laufwerk auswählen"""
        data = await request.json()
        drive_type = data.get('drive', 1)
        self.stm32.send_command(0x10, struct.pack('<B', drive_type))
        return web.json_response({'ok': True})
    
    async def drive_motor(self, request):
        """Motor ein/aus"""
        data = await request.json()
        on = data.get('on', True)
        cmd = 0x11 if on else 0x12
        self.stm32.send_command(cmd)
        return web.json_response({'ok': True})
    
    def run(self, host='0.0.0.0', port=5000):
        """Web-Server starten"""
        web.run_app(self.app, host=host, port=port)


# ============================================================================
# MAIN
# ============================================================================

def main():
    log.info("UFI CM5 Processing Layer startet...")
    
    # STM32 verbinden
    stm32 = STM32Connection()
    if not stm32.connect():
        log.error("STM32 nicht gefunden - Demo-Modus")
    
    # Komponenten erstellen
    processor = FluxProcessor()
    buffer = DiskBuffer()
    
    # Web-API starten
    api = WebAPI(processor, buffer, stm32)
    log.info("Web-API auf http://0.0.0.0:5000")
    api.run()


if __name__ == '__main__':
    main()
