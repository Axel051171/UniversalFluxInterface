"""Extended stream integrity. CRC32 protects framing completeness, not authenticity."""
import struct
import zlib
from dataclasses import dataclass

HEADER = struct.Struct("<QII")
END = struct.Struct("<QIII")

@dataclass
class JobStream:
    id: int
    packets: int = 0
    offset: int = 0
    crc: int = 0

    def _advance(self, data):
        if self.offset + len(data) > 0xffffffff or self.packets == 0xffffffff:
            raise ValueError("job stream exceeds protocol limits")
        self.crc = zlib.crc32(data, self.crc)
        self.offset += len(data)
        self.packets += 1

    def encode(self, data):
        header = HEADER.pack(self.id, self.packets, self.offset)
        self._advance(data)
        return header + data

    def decode(self, payload):
        if len(payload) <= HEADER.size:
            raise ValueError("truncated or empty job data")
        if HEADER.unpack_from(payload) != (self.id, self.packets, self.offset):
            raise ValueError("job ID, packet sequence or offset mismatch")
        data = payload[HEADER.size:]
        self._advance(data)
        return data

    def finish(self, payload):
        if len(payload) < END.size or END.unpack_from(payload) != (
                self.id, self.packets, self.offset, self.crc):
            raise ValueError("job completion totals or checksum mismatch")
        return payload[END.size:]
