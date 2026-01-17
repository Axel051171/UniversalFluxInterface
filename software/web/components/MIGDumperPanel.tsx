/**
 * UFI MIG Dumper Web Interface
 * 
 * React component for managing MIG Switch Dumper integration.
 * Provides UI for detecting, mounting, and managing Switch game dumps.
 * 
 * Copyright (c) 2026 UFI Project
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import React, { useState, useEffect, useCallback } from 'react';

// ============================================================================
// Types
// ============================================================================

interface MIGStatus {
  connected: boolean;
  device_path: string | null;
  mount_point: string;
  firmware_version: string;
  total_space: number;
  free_space: number;
  operation_active: boolean;
  operation_progress: number;
  operation_message: string;
}

interface USBDevice {
  name: string;
  path: string;
  size: string;
  mountpoint: string | null;
}

interface DumpInfo {
  path: string;
  title: string;
  title_id: string;
  file_size: number;
  file_size_gb: number;
  rom_size_gb: number;
  trimmed_size: number;
  trimmed_size_gb: number;
  date: number;
  has_certificate: boolean;
  has_initial_data: boolean;
  has_card_id: boolean;
  has_card_uid: boolean;
}

// ============================================================================
// API Functions
// ============================================================================

const API_BASE = '/api/mig';

async function fetchAPI(endpoint: string, options?: RequestInit) {
  const response = await fetch(`${API_BASE}${endpoint}`, {
    headers: {
      'Content-Type': 'application/json',
    },
    ...options,
  });
  
  if (!response.ok) {
    const error = await response.json().catch(() => ({ error: 'Request failed' }));
    throw new Error(error.error || 'Request failed');
  }
  
  return response.json();
}

// ============================================================================
// Components
// ============================================================================

const StatusBadge: React.FC<{ connected: boolean }> = ({ connected }) => (
  <span className={`inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium ${
    connected ? 'bg-green-100 text-green-800' : 'bg-gray-100 text-gray-800'
  }`}>
    <span className={`w-2 h-2 mr-1.5 rounded-full ${
      connected ? 'bg-green-400' : 'bg-gray-400'
    }`}></span>
    {connected ? 'Connected' : 'Disconnected'}
  </span>
);

const ProgressBar: React.FC<{ progress: number; message: string }> = ({ progress, message }) => (
  <div className="w-full">
    <div className="flex justify-between text-sm mb-1">
      <span className="text-gray-600">{message}</span>
      <span className="text-gray-600">{progress}%</span>
    </div>
    <div className="w-full bg-gray-200 rounded-full h-2.5">
      <div
        className="bg-blue-600 h-2.5 rounded-full transition-all duration-300"
        style={{ width: `${progress}%` }}
      ></div>
    </div>
  </div>
);

const StorageBar: React.FC<{ used: number; total: number }> = ({ used, total }) => {
  const usedPercent = total > 0 ? (used / total) * 100 : 0;
  const usedGB = (used / (1024 ** 3)).toFixed(1);
  const totalGB = (total / (1024 ** 3)).toFixed(1);
  
  return (
    <div className="w-full">
      <div className="flex justify-between text-sm mb-1">
        <span className="text-gray-600">Storage</span>
        <span className="text-gray-600">{usedGB} GB / {totalGB} GB</span>
      </div>
      <div className="w-full bg-gray-200 rounded-full h-2">
        <div
          className={`h-2 rounded-full ${usedPercent > 90 ? 'bg-red-500' : 'bg-blue-500'}`}
          style={{ width: `${usedPercent}%` }}
        ></div>
      </div>
    </div>
  );
};

const DumpCard: React.FC<{
  dump: DumpInfo;
  onCopy: () => void;
  onVerify: () => void;
  onDelete: () => void;
  disabled: boolean;
}> = ({ dump, onCopy, onVerify, onDelete, disabled }) => {
  const extras = [
    dump.has_certificate && 'Cert',
    dump.has_initial_data && 'InitData',
    dump.has_card_id && 'CardID',
    dump.has_card_uid && 'UID',
  ].filter(Boolean);
  
  return (
    <div className="bg-white rounded-lg shadow p-4 border border-gray-200">
      <div className="flex justify-between items-start">
        <div className="flex-1">
          <h3 className="font-semibold text-lg text-gray-900">{dump.title}</h3>
          {dump.title_id && (
            <p className="text-sm text-gray-500 font-mono">{dump.title_id}</p>
          )}
        </div>
        <span className="bg-blue-100 text-blue-800 text-xs font-medium px-2.5 py-0.5 rounded">
          {dump.rom_size_gb} GB
        </span>
      </div>
      
      <div className="mt-3 grid grid-cols-2 gap-2 text-sm">
        <div>
          <span className="text-gray-500">File Size:</span>
          <span className="ml-1 text-gray-900">{dump.file_size_gb} GB</span>
        </div>
        <div>
          <span className="text-gray-500">Trimmed:</span>
          <span className="ml-1 text-gray-900">{dump.trimmed_size_gb} GB</span>
        </div>
      </div>
      
      {extras.length > 0 && (
        <div className="mt-2 flex flex-wrap gap-1">
          {extras.map((extra) => (
            <span
              key={extra}
              className="bg-gray-100 text-gray-600 text-xs px-2 py-0.5 rounded"
            >
              {extra}
            </span>
          ))}
        </div>
      )}
      
      <div className="mt-4 flex gap-2">
        <button
          onClick={onCopy}
          disabled={disabled}
          className="flex-1 bg-blue-600 text-white px-3 py-1.5 rounded text-sm font-medium
                     hover:bg-blue-700 disabled:bg-gray-300 disabled:cursor-not-allowed"
        >
          Copy to UFI
        </button>
        <button
          onClick={onVerify}
          disabled={disabled}
          className="px-3 py-1.5 border border-gray-300 rounded text-sm font-medium
                     hover:bg-gray-50 disabled:bg-gray-100 disabled:cursor-not-allowed"
        >
          Verify
        </button>
        <button
          onClick={onDelete}
          disabled={disabled}
          className="px-3 py-1.5 border border-red-300 text-red-600 rounded text-sm font-medium
                     hover:bg-red-50 disabled:bg-gray-100 disabled:cursor-not-allowed"
        >
          Delete
        </button>
      </div>
    </div>
  );
};

// ============================================================================
// Main Component
// ============================================================================

export const MIGDumperPanel: React.FC = () => {
  const [status, setStatus] = useState<MIGStatus | null>(null);
  const [devices, setDevices] = useState<USBDevice[]>([]);
  const [dumps, setDumps] = useState<DumpInfo[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [selectedDevice, setSelectedDevice] = useState<string>('');
  
  // Fetch status
  const fetchStatus = useCallback(async () => {
    try {
      const data = await fetchAPI('/status');
      setStatus(data);
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to fetch status');
    }
  }, []);
  
  // Fetch dumps list
  const fetchDumps = useCallback(async () => {
    if (!status?.connected) return;
    
    try {
      const data = await fetchAPI('/dumps');
      setDumps(data.dumps);
    } catch (err) {
      console.error('Failed to fetch dumps:', err);
    }
  }, [status?.connected]);
  
  // Initial load
  useEffect(() => {
    setLoading(true);
    fetchStatus().finally(() => setLoading(false));
    
    // Poll for updates
    const interval = setInterval(fetchStatus, 2000);
    return () => clearInterval(interval);
  }, [fetchStatus]);
  
  // Fetch dumps when connected
  useEffect(() => {
    if (status?.connected) {
      fetchDumps();
    } else {
      setDumps([]);
    }
  }, [status?.connected, fetchDumps]);
  
  // Detect devices
  const handleDetect = async () => {
    try {
      const data = await fetchAPI('/detect', { method: 'POST' });
      setDevices(data.devices);
      
      if (data.devices.length === 1) {
        setSelectedDevice(data.devices[0].path);
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Detection failed');
    }
  };
  
  // Mount device
  const handleMount = async () => {
    if (!selectedDevice) return;
    
    try {
      await fetchAPI('/mount', {
        method: 'POST',
        body: JSON.stringify({ device_path: selectedDevice }),
      });
      await fetchStatus();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Mount failed');
    }
  };
  
  // Unmount device
  const handleUnmount = async () => {
    try {
      await fetchAPI('/unmount', { method: 'POST' });
      await fetchStatus();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Unmount failed');
    }
  };
  
  // Copy dump
  const handleCopy = async (dump: DumpInfo) => {
    const relativePath = dump.path.replace(status?.mount_point || '', '').replace(/^\//, '');
    
    try {
      await fetchAPI(`/dumps/${encodeURIComponent(relativePath)}/copy`, {
        method: 'POST',
        body: JSON.stringify({ include_extras: true }),
      });
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Copy failed');
    }
  };
  
  // Verify dump
  const handleVerify = async (dump: DumpInfo) => {
    const relativePath = dump.path.replace(status?.mount_point || '', '').replace(/^\//, '');
    
    try {
      const result = await fetchAPI(`/dumps/${encodeURIComponent(relativePath)}/verify`, {
        method: 'POST',
      });
      
      if (result.valid) {
        alert('✅ Dump verified successfully!');
      } else {
        alert(`❌ Verification failed:\n${result.errors.join('\n')}`);
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Verify failed');
    }
  };
  
  // Delete dump
  const handleDelete = async (dump: DumpInfo) => {
    if (!confirm(`Delete "${dump.title}"?\n\nThis cannot be undone.`)) {
      return;
    }
    
    const relativePath = dump.path.replace(status?.mount_point || '', '').replace(/^\//, '');
    
    try {
      await fetchAPI(`/dumps/${encodeURIComponent(relativePath)}`, {
        method: 'DELETE',
      });
      await fetchDumps();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Delete failed');
    }
  };
  
  if (loading) {
    return (
      <div className="flex items-center justify-center h-64">
        <div className="animate-spin rounded-full h-12 w-12 border-b-2 border-blue-600"></div>
      </div>
    );
  }
  
  return (
    <div className="max-w-4xl mx-auto p-4">
      {/* Header */}
      <div className="flex justify-between items-center mb-6">
        <div>
          <h1 className="text-2xl font-bold text-gray-900">MIG Dumper</h1>
          <p className="text-gray-600">Nintendo Switch Cartridge Management</p>
        </div>
        <StatusBadge connected={status?.connected || false} />
      </div>
      
      {/* Error Alert */}
      {error && (
        <div className="mb-4 bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded relative">
          <span className="block sm:inline">{error}</span>
          <button
            className="absolute top-0 bottom-0 right-0 px-4"
            onClick={() => setError(null)}
          >
            ×
          </button>
        </div>
      )}
      
      {/* Connection Section */}
      {!status?.connected && (
        <div className="bg-white rounded-lg shadow p-6 mb-6">
          <h2 className="text-lg font-semibold mb-4">Connect MIG Dumper</h2>
          
          <div className="space-y-4">
            <button
              onClick={handleDetect}
              className="w-full bg-gray-100 hover:bg-gray-200 px-4 py-2 rounded font-medium"
            >
              🔍 Scan for Devices
            </button>
            
            {devices.length > 0 && (
              <>
                <div>
                  <label className="block text-sm font-medium text-gray-700 mb-1">
                    Select Device
                  </label>
                  <select
                    value={selectedDevice}
                    onChange={(e) => setSelectedDevice(e.target.value)}
                    className="w-full border border-gray-300 rounded px-3 py-2"
                  >
                    <option value="">-- Select --</option>
                    {devices.map((dev) => (
                      <option key={dev.path} value={dev.path}>
                        {dev.path} ({dev.size})
                      </option>
                    ))}
                  </select>
                </div>
                
                <button
                  onClick={handleMount}
                  disabled={!selectedDevice}
                  className="w-full bg-blue-600 text-white px-4 py-2 rounded font-medium
                             hover:bg-blue-700 disabled:bg-gray-300"
                >
                  Mount Device
                </button>
              </>
            )}
          </div>
        </div>
      )}
      
      {/* Connected Device Info */}
      {status?.connected && (
        <div className="bg-white rounded-lg shadow p-6 mb-6">
          <div className="flex justify-between items-start mb-4">
            <div>
              <h2 className="text-lg font-semibold">Device Connected</h2>
              <p className="text-sm text-gray-500">{status.device_path}</p>
              <p className="text-sm text-gray-500">
                Firmware: {status.firmware_version}
              </p>
            </div>
            <button
              onClick={handleUnmount}
              disabled={status.operation_active}
              className="px-4 py-2 border border-gray-300 rounded font-medium
                         hover:bg-gray-50 disabled:bg-gray-100"
            >
              Eject
            </button>
          </div>
          
          <StorageBar
            used={status.total_space - status.free_space}
            total={status.total_space}
          />
          
          {/* Operation Progress */}
          {status.operation_active && (
            <div className="mt-4 p-4 bg-blue-50 rounded">
              <ProgressBar
                progress={status.operation_progress}
                message={status.operation_message}
              />
            </div>
          )}
        </div>
      )}
      
      {/* Dumps List */}
      {status?.connected && (
        <div>
          <div className="flex justify-between items-center mb-4">
            <h2 className="text-lg font-semibold">
              Game Dumps ({dumps.length})
            </h2>
            <button
              onClick={fetchDumps}
              className="px-3 py-1 text-sm border border-gray-300 rounded hover:bg-gray-50"
            >
              ↻ Refresh
            </button>
          </div>
          
          {dumps.length === 0 ? (
            <div className="bg-gray-50 rounded-lg p-8 text-center text-gray-500">
              No dumps found on device
            </div>
          ) : (
            <div className="grid gap-4 md:grid-cols-2">
              {dumps.map((dump) => (
                <DumpCard
                  key={dump.path}
                  dump={dump}
                  onCopy={() => handleCopy(dump)}
                  onVerify={() => handleVerify(dump)}
                  onDelete={() => handleDelete(dump)}
                  disabled={status.operation_active}
                />
              ))}
            </div>
          )}
        </div>
      )}
    </div>
  );
};

export default MIGDumperPanel;
