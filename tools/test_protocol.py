"""Run: python3 -m unittest tools/test_protocol.py  (or python3 tools/test_protocol.py)"""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from protocol import (AccessUnitSplitter, Message, Msg, Reader, describe_input, hello, hello_ack,  # noqa: E402
                      nal_type, pack, parse_hello, split_nals)


class Framing(unittest.TestCase):
    def test_header_layout(self):
        b = pack(Msg.VIDEO, b"abc", flags=1)
        self.assertEqual(b[:8], bytes([3, 1, 0, 0, 0, 0, 0, 3]))
        self.assertEqual(b[8:], b"abc")

    def test_reader_handles_split_and_merged_chunks(self):
        stream = pack(Msg.PING, b"12345678") + pack(Msg.KEY, b"\x00\x41\x01") + pack(Msg.RELEASE_ALL)
        r = Reader()
        got = []
        for i in range(len(stream)):  # one byte at a time
            got += r.feed(stream[i:i + 1])
        self.assertEqual([m.type for m in got], [Msg.PING, Msg.KEY, Msg.RELEASE_ALL])
        self.assertEqual(got[0].payload, b"12345678")
        self.assertEqual(Reader().feed(stream)[1].payload, b"\x00\x41\x01")

    def test_hello_roundtrip(self):
        self.assertEqual(parse_hello(hello("s3cr3t")), (1, "s3cr3t"))
        self.assertIsNone(parse_hello(b"NOPE\x00\x01\x00\x00"))
        self.assertIsNone(parse_hello(b"ORC1\x00\x01\x00\x09abc"))  # truncated token

    def test_hello_ack_layout(self):
        self.assertEqual(hello_ack(0, 2560, 1600), bytes([0, 0x0A, 0x00, 0x06, 0x40, 1]))

    def test_describe_input(self):
        self.assertIn("left down", describe_input(Message(Msg.MOUSE_BUTTON, 0, b"\x00\x01")))
        self.assertIn("dy=-120", describe_input(Message(Msg.MOUSE_WHEEL, 0, struct.pack(">hh", 0, -120))))


class HEVC(unittest.TestCase):
    # Fake NAL units: header byte = type << 1
    AUD = b"\x46\x01\x50"
    VPS = bytes([32 << 1, 1, 0xAA])
    SPS = bytes([33 << 1, 1, 0xBB])
    PPS = bytes([34 << 1, 1, 0xCC])
    IDR = bytes([19 << 1, 1, 0x00, 0x00, 0x02, 0x11])  # contains 00 00 02, not a start code
    TRAIL = bytes([1 << 1, 1, 0x22])

    def annexb(self, *nals, four=True):
        sc = b"\x00\x00\x00\x01" if four else b"\x00\x00\x01"
        return b"".join(sc + n for n in nals)

    def test_split_nals_both_start_code_lengths(self):
        data = self.annexb(self.VPS, self.SPS) + self.annexb(self.PPS, self.IDR, four=False)
        self.assertEqual(split_nals(data), [self.VPS, self.SPS, self.PPS, self.IDR])
        self.assertEqual([nal_type(n) for n in split_nals(data)], [32, 33, 34, 19])

    def test_access_unit_splitter(self):
        au1 = self.annexb(self.AUD, self.VPS, self.SPS, self.PPS, self.IDR)
        au2 = self.annexb(self.AUD, self.TRAIL)
        au3 = self.annexb(self.AUD, self.TRAIL)
        stream = au1 + au2 + au3
        s = AccessUnitSplitter()
        out = []
        for i in range(0, len(stream), 5):
            out += s.feed(stream[i:i + 5])
        self.assertEqual(out, [au1, au2])  # au3 stays buffered until the next AUD
        self.assertEqual(s.flush(), au3)


if __name__ == "__main__":
    unittest.main()
