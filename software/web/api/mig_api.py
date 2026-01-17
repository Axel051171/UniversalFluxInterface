"""
UFI MIG Dumper Web API

Flask Blueprint providing REST API for MIG Dumper integration.
Enables the UFI web interface to manage Switch game dumps.

Copyright (c) 2026 UFI Project
SPDX-License-Identifier: GPL-3.0-or-later
"""

import os
import sys
import json
import threading
from pathlib import Path
from flask import Blueprint, request, jsonify, send_file
from functools import wraps

# Add tools directory to path
sys.path.insert(0, str(Path(__file__).parent.parent / 'tools'))

# Import MIG manager (would need to be refactored as module)
# For now, we'll define a simplified version here

# ============================================================================
# Blueprint Setup
# ============================================================================

mig_bp = Blueprint('mig', __name__, url_prefix='/api/mig')

# Global state
_mig_state = {
    'mounted': False,
    'device_path': None,
    'mount_point': '/mnt/mig',
    'firmware_version': 'Unknown',
    'total_space': 0,
    'free_space': 0,
    'operation_active': False,
    'operation_progress': 0,
    'operation_message': '',
}

_mig_lock = threading.Lock()

# ============================================================================
# Helper Functions
# ============================================================================

def get_mig_state():
    """Get current MIG Dumper state"""
    with _mig_lock:
        return dict(_mig_state)

def update_mig_state(**kwargs):
    """Update MIG state"""
    with _mig_lock:
        _mig_state.update(kwargs)

def require_mounted(f):
    """Decorator requiring MIG to be mounted"""
    @wraps(f)
    def decorated(*args, **kwargs):
        if not _mig_state['mounted']:
            return jsonify({'error': 'MIG Dumper not mounted'}), 400
        return f(*args, **kwargs)
    return decorated

def parse_xci_header(path):
    """Parse XCI header from file"""
    try:
        with open(path, 'rb') as f:
            data = f.read(0x200)
        
        if data[0x100:0x104] != b'HEAD':
            return None
        
        import struct
        
        rom_size_code = data[0x10D]
        valid_data_end_page = struct.unpack('<I', data[0x118:0x11C])[0]
        
        rom_sizes = {
            0xFA: 1, 0xF8: 2, 0xF0: 4,
            0xE0: 8, 0xE1: 16, 0xE2: 32
        }
        
        return {
            'rom_size_gb': rom_sizes.get(rom_size_code, 0),
            'data_size': (valid_data_end_page + 1) * 0x200
        }
    except:
        return None

def scan_dumps(mount_point):
    """Scan for dumps on mounted device"""
    dumps = []
    
    try:
        for entry in os.listdir(mount_point):
            if entry.startswith('.') or entry == 'System':
                continue
            
            full_path = os.path.join(mount_point, entry)
            
            # Check for XCI files
            if os.path.isdir(full_path):
                xci_path = os.path.join(full_path, f"{entry}.xci")
                if not os.path.exists(xci_path):
                    # Look for any XCI
                    for sub in os.listdir(full_path):
                        if sub.lower().endswith('.xci'):
                            xci_path = os.path.join(full_path, sub)
                            break
                
                if os.path.exists(xci_path):
                    dump = analyze_dump(full_path, xci_path)
                    if dump:
                        dumps.append(dump)
            
            elif entry.lower().endswith('.xci'):
                dump = analyze_dump(full_path, full_path)
                if dump:
                    dumps.append(dump)
    except OSError:
        pass
    
    return dumps

def analyze_dump(path, xci_path):
    """Analyze a dump"""
    try:
        stat = os.stat(xci_path)
        
        # Parse filename for title
        base_name = os.path.splitext(os.path.basename(xci_path))[0]
        title = base_name
        title_id = ""
        
        if '[' in base_name and ']' in base_name:
            bracket_start = base_name.rfind('[')
            bracket_end = base_name.rfind(']')
            if bracket_end > bracket_start:
                title_id = base_name[bracket_start+1:bracket_end]
                title = base_name[:bracket_start].strip()
        
        # Parse header
        header = parse_xci_header(xci_path)
        
        dump = {
            'path': path,
            'xci_path': xci_path,
            'title': title,
            'title_id': title_id,
            'file_size': stat.st_size,
            'rom_size_gb': header['rom_size_gb'] if header else 0,
            'trimmed_size': header['data_size'] if header else stat.st_size,
            'date': stat.st_mtime,
            'extras': {}
        }
        
        # Check for extra files
        folder = os.path.dirname(xci_path)
        for suffix, key in [
            (' (Certificate).bin', 'certificate'),
            (' (Initial Data).bin', 'initial_data'),
            (' (Card ID Set).bin', 'card_id'),
            (' (Card UID).bin', 'card_uid'),
        ]:
            extra_path = os.path.join(folder, base_name + suffix)
            if os.path.exists(extra_path):
                dump['extras'][key] = extra_path
        
        return dump
    except:
        return None

# ============================================================================
# API Routes
# ============================================================================

@mig_bp.route('/status', methods=['GET'])
def get_status():
    """Get MIG Dumper status"""
    state = get_mig_state()
    
    # Check if still mounted
    if state['mounted']:
        if not os.path.ismount(state['mount_point']):
            update_mig_state(mounted=False, device_path=None)
            state = get_mig_state()
    
    return jsonify({
        'connected': state['mounted'],
        'device_path': state['device_path'],
        'mount_point': state['mount_point'],
        'firmware_version': state['firmware_version'],
        'total_space': state['total_space'],
        'free_space': state['free_space'],
        'operation_active': state['operation_active'],
        'operation_progress': state['operation_progress'],
        'operation_message': state['operation_message'],
    })


@mig_bp.route('/detect', methods=['POST'])
def detect_device():
    """Detect MIG Dumper device"""
    import subprocess
    
    try:
        # Use lsblk to find USB devices
        result = subprocess.run(
            ['lsblk', '-J', '-o', 'NAME,TRAN,SIZE,MOUNTPOINT'],
            capture_output=True, text=True
        )
        
        if result.returncode != 0:
            return jsonify({'error': 'Failed to scan devices'}), 500
        
        data = json.loads(result.stdout)
        usb_devices = []
        
        for device in data.get('blockdevices', []):
            if device.get('tran') == 'usb':
                children = device.get('children', [])
                if children:
                    for part in children:
                        usb_devices.append({
                            'name': part.get('name'),
                            'size': part.get('size'),
                            'mountpoint': part.get('mountpoint'),
                            'path': f"/dev/{part.get('name')}"
                        })
                else:
                    usb_devices.append({
                        'name': device.get('name'),
                        'size': device.get('size'),
                        'mountpoint': device.get('mountpoint'),
                        'path': f"/dev/{device.get('name')}"
                    })
        
        return jsonify({'devices': usb_devices})
    
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@mig_bp.route('/mount', methods=['POST'])
def mount_device():
    """Mount MIG Dumper"""
    import subprocess
    
    data = request.get_json() or {}
    device_path = data.get('device_path')
    mount_point = data.get('mount_point', '/mnt/mig')
    
    if not device_path:
        return jsonify({'error': 'device_path required'}), 400
    
    # Check if already mounted
    if os.path.ismount(mount_point):
        update_mig_state(mounted=True, device_path=device_path, mount_point=mount_point)
        return jsonify({'success': True, 'message': 'Already mounted'})
    
    # Create mount point
    os.makedirs(mount_point, exist_ok=True)
    
    # Try mounting
    for fstype in ['vfat', 'exfat']:
        result = subprocess.run(
            ['mount', '-t', fstype, '-o', 'utf8,umask=0000',
             device_path, mount_point],
            capture_output=True, text=True
        )
        
        if result.returncode == 0:
            # Get filesystem info
            try:
                stat = os.statvfs(mount_point)
                total = stat.f_blocks * stat.f_bsize
                free = stat.f_bavail * stat.f_bsize
            except:
                total = free = 0
            
            # Read firmware version
            fw_version = 'Unknown'
            system_path = os.path.join(mount_point, 'System')
            if os.path.isdir(system_path):
                for entry in os.listdir(system_path):
                    if entry[0].isdigit() and '.' in entry:
                        fw_version = entry
                        break
            
            update_mig_state(
                mounted=True,
                device_path=device_path,
                mount_point=mount_point,
                firmware_version=fw_version,
                total_space=total,
                free_space=free
            )
            
            return jsonify({
                'success': True,
                'filesystem': fstype,
                'firmware_version': fw_version
            })
    
    return jsonify({'error': 'Failed to mount device'}), 500


@mig_bp.route('/unmount', methods=['POST'])
def unmount_device():
    """Unmount MIG Dumper"""
    import subprocess
    
    state = get_mig_state()
    mount_point = state['mount_point']
    
    if not os.path.ismount(mount_point):
        update_mig_state(mounted=False, device_path=None)
        return jsonify({'success': True, 'message': 'Not mounted'})
    
    # Sync and unmount
    subprocess.run(['sync'], capture_output=True)
    
    result = subprocess.run(
        ['umount', mount_point],
        capture_output=True, text=True
    )
    
    if result.returncode == 0:
        update_mig_state(mounted=False, device_path=None)
        return jsonify({'success': True})
    
    # Try lazy unmount
    result = subprocess.run(
        ['umount', '-l', mount_point],
        capture_output=True, text=True
    )
    
    if result.returncode == 0:
        update_mig_state(mounted=False, device_path=None)
        return jsonify({'success': True, 'lazy': True})
    
    return jsonify({'error': 'Failed to unmount'}), 500


@mig_bp.route('/dumps', methods=['GET'])
@require_mounted
def list_dumps():
    """List dumps on MIG Dumper"""
    state = get_mig_state()
    dumps = scan_dumps(state['mount_point'])
    
    # Convert to JSON-serializable format
    result = []
    for dump in dumps:
        result.append({
            'path': dump['path'],
            'title': dump['title'],
            'title_id': dump['title_id'],
            'file_size': dump['file_size'],
            'file_size_gb': round(dump['file_size'] / (1024**3), 2),
            'rom_size_gb': dump['rom_size_gb'],
            'trimmed_size': dump['trimmed_size'],
            'trimmed_size_gb': round(dump['trimmed_size'] / (1024**3), 2),
            'date': dump['date'],
            'has_certificate': 'certificate' in dump['extras'],
            'has_initial_data': 'initial_data' in dump['extras'],
            'has_card_id': 'card_id' in dump['extras'],
            'has_card_uid': 'card_uid' in dump['extras'],
        })
    
    return jsonify({'dumps': result, 'count': len(result)})


@mig_bp.route('/dumps/<path:dump_path>/copy', methods=['POST'])
@require_mounted
def copy_dump(dump_path):
    """Copy dump to UFI storage"""
    import shutil
    
    state = get_mig_state()
    
    if state['operation_active']:
        return jsonify({'error': 'Operation already in progress'}), 409
    
    data = request.get_json() or {}
    dest_dir = data.get('destination', '/var/lib/ufi/images')
    include_extras = data.get('include_extras', True)
    
    # Find the dump
    full_path = os.path.join(state['mount_point'], dump_path)
    
    if not os.path.exists(full_path):
        return jsonify({'error': 'Dump not found'}), 404
    
    # Start copy in background
    def copy_worker():
        try:
            update_mig_state(operation_active=True, operation_progress=0,
                           operation_message='Starting copy...')
            
            # Determine files to copy
            if os.path.isdir(full_path):
                files = []
                for entry in os.listdir(full_path):
                    entry_path = os.path.join(full_path, entry)
                    if os.path.isfile(entry_path):
                        if include_extras or entry.endswith('.xci'):
                            files.append(entry_path)
            else:
                files = [full_path]
            
            total_size = sum(os.path.getsize(f) for f in files)
            copied = 0
            
            os.makedirs(dest_dir, exist_ok=True)
            
            for src_path in files:
                filename = os.path.basename(src_path)
                dest_path = os.path.join(dest_dir, filename)
                
                update_mig_state(operation_message=f'Copying {filename}...')
                
                # Copy with progress
                src_size = os.path.getsize(src_path)
                
                with open(src_path, 'rb') as fsrc, open(dest_path, 'wb') as fdst:
                    while True:
                        chunk = fsrc.read(4 * 1024 * 1024)  # 4MB
                        if not chunk:
                            break
                        fdst.write(chunk)
                        copied += len(chunk)
                        
                        progress = int((copied * 100) / total_size)
                        update_mig_state(operation_progress=progress)
            
            update_mig_state(operation_active=False, operation_progress=100,
                           operation_message='Copy complete')
        
        except Exception as e:
            update_mig_state(operation_active=False, operation_progress=0,
                           operation_message=f'Error: {str(e)}')
    
    thread = threading.Thread(target=copy_worker)
    thread.start()
    
    return jsonify({'success': True, 'message': 'Copy started'})


@mig_bp.route('/dumps/<path:dump_path>/verify', methods=['POST'])
@require_mounted
def verify_dump(dump_path):
    """Verify dump integrity"""
    state = get_mig_state()
    
    full_path = os.path.join(state['mount_point'], dump_path)
    
    if not os.path.exists(full_path):
        return jsonify({'error': 'Dump not found'}), 404
    
    # Find XCI file
    if os.path.isdir(full_path):
        xci_path = None
        for entry in os.listdir(full_path):
            if entry.lower().endswith('.xci'):
                xci_path = os.path.join(full_path, entry)
                break
        if not xci_path:
            return jsonify({'error': 'No XCI file found'}), 404
    else:
        xci_path = full_path
    
    result = {
        'valid': True,
        'header_valid': False,
        'size_valid': False,
        'errors': []
    }
    
    try:
        # Check header
        header = parse_xci_header(xci_path)
        if header:
            result['header_valid'] = True
            
            # Check size
            file_size = os.path.getsize(xci_path)
            if file_size >= header['data_size']:
                result['size_valid'] = True
            else:
                result['errors'].append(
                    f"File size {file_size} < expected {header['data_size']}")
                result['valid'] = False
        else:
            result['errors'].append('Invalid XCI header')
            result['valid'] = False
    
    except Exception as e:
        result['errors'].append(str(e))
        result['valid'] = False
    
    return jsonify(result)


@mig_bp.route('/dumps/<path:dump_path>', methods=['DELETE'])
@require_mounted
def delete_dump(dump_path):
    """Delete dump from MIG Dumper"""
    import shutil
    
    state = get_mig_state()
    full_path = os.path.join(state['mount_point'], dump_path)
    
    if not os.path.exists(full_path):
        return jsonify({'error': 'Dump not found'}), 404
    
    try:
        if os.path.isdir(full_path):
            shutil.rmtree(full_path)
        else:
            os.remove(full_path)
        
        return jsonify({'success': True})
    
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@mig_bp.route('/operation/cancel', methods=['POST'])
def cancel_operation():
    """Cancel current operation"""
    # Note: This is a simplified version - proper cancellation
    # would need thread interruption support
    
    update_mig_state(
        operation_active=False,
        operation_message='Cancelled'
    )
    
    return jsonify({'success': True})


# ============================================================================
# WebSocket Events (for real-time updates)
# ============================================================================

def register_socketio(socketio):
    """Register Socket.IO events for real-time updates"""
    
    @socketio.on('connect', namespace='/mig')
    def on_connect():
        # Send current state
        socketio.emit('status', get_mig_state(), namespace='/mig')
    
    @socketio.on('subscribe', namespace='/mig')
    def on_subscribe():
        # Client wants real-time updates
        pass
    
    # Background task to emit progress updates
    def progress_emitter():
        import time
        last_state = None
        
        while True:
            state = get_mig_state()
            
            if state != last_state:
                socketio.emit('status', state, namespace='/mig')
                last_state = state.copy()
            
            time.sleep(0.5)
    
    # Start emitter thread
    thread = threading.Thread(target=progress_emitter, daemon=True)
    thread.start()


# ============================================================================
# Integration with main Flask app
# ============================================================================

def init_app(app):
    """Initialize MIG Dumper integration with Flask app"""
    app.register_blueprint(mig_bp)
    
    # Check if SocketIO is available
    if hasattr(app, 'socketio'):
        register_socketio(app.socketio)
    
    return app
