"""Verify published wire examples and the probe against the existing host double."""
import importlib.util
from pathlib import Path
import re
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'software/ufi_host'), str(ROOT/'software/ufi_host/tests')]
from ufi_host import protocol2 as P
from test_protocol2 import FakeV2

spec = importlib.util.spec_from_file_location('ufi_v2_probe', Path(__file__).parent/'examples/ufi_v2_probe.py')
example = importlib.util.module_from_spec(spec)
spec.loader.exec_module(example)


class ProtocolExamples(unittest.TestCase):
    def test_published_vectors_match_reference_codec_in_both_languages(self):
        expected = [
            P.encode_frame(P.T_REQUEST, P.PING, 1, payload=b'UFI2'),
            P.encode_frame(P.T_RESPONSE, P.PING, 1, payload=b'UFI2'),
            P.encode_frame(P.T_REQUEST, P.SELECT, 2, payload=b'\x01'),
            P.encode_frame(P.T_REQUEST, P.READ, 3, payload=struct.pack('<BBBBH', 0, 0, 3, 0, 0)),
        ]
        for name in ('USB-Protokoll', 'USB-Protocol'):
            text = (ROOT/f'docs/wiki/{name}.md').read_text(encoding='utf-8')
            vectors = [bytes.fromhex(s) for s in re.findall(r'(?m)^(?:[0-9A-F]{2} ){9,}[0-9A-F]{2}$', text)]
            self.assertEqual(vectors, expected, name)
            for vector in vectors:
                for split in range(len(vector)+1):
                    parser = P.FrameParser()
                    frames = parser.feed(vector[:split])+parser.feed(vector[split:])
                    self.assertEqual(len(frames), 1, (name, split))
                    self.assertEqual(frames[0].encode(), vector)
        self.assertEqual(P.crc16(b'123456789'), 0x29B1)

    def test_probe_uses_only_ping_and_info_with_fragmented_replies(self):
        stream = FakeV2(board_rev=10)
        stream.garbage = b'\x00\x55\x00'
        result = example.probe(stream)
        self.assertEqual(result['proto'], 2)
        self.assertEqual(result['board_rev'], 10)
        self.assertEqual(result['sample_hz'], 275000000)
        self.assertEqual([frame.cmd for frame in stream.requests], [P.PING, P.INFO])


if __name__ == '__main__':
    unittest.main()
