"""Reference implementation of the ORC v1 wire protocol (docs/PROTOCOL.md)."""
from __future__ import annotations

import socket
import struct
from dataclasses import dataclass
from enum import IntEnum

DEFAULT_PORT = 47800
VERSION = 1
HEADER = struct.Struct(">BBHI")  # type, flags, reserved, length
MAX_PAYLOAD = 16 * 1024 * 1024
FLAG_KEYFRAME = 0x01


class Msg(IntEnum):
    HELLO = 1
    HELLO_ACK = 2
    VIDEO = 3
    KEYFRAME_REQ = 4
    PING = 5
    PONG = 6
    MOUSE_MOVE = 16
    MOUSE_BUTTON = 17
    MOUSE_WHEEL = 18
    KEY = 19
    RELEASE_ALL = 20


@dataclass
class Message:
    type: int
    flags: int
    payload: bytes


def pack(msg_type: int, payload: bytes = b"", flags: int = 0) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    return HEADER.pack(int(msg_type), flags, 0, len(payload)) + payload


class Reader:
    """Incremental parser: feed() bytes, iterate complete messages."""

    def __init__(self) -> None:
        self.buf = bytearray()

    def feed(self, data: bytes) -> list[Message]:
        self.buf += data
        out: list[Message] = []
        while len(self.buf) >= HEADER.size:
            t, flags, _, length = HEADER.unpack_from(self.buf)
            if length > MAX_PAYLOAD:
                raise ValueError("oversized message")
            if len(self.buf) < HEADER.size + length:
                break
            payload = bytes(self.buf[HEADER.size:HEADER.size + length])
            del self.buf[:HEADER.size + length]
            out.append(Message(t, flags, payload))
        return out


def recv_message(sock: socket.socket, reader: Reader, pending: list[Message]) -> Message | None:
    while not pending:
        data = sock.recv(65536)
        if not data:
            return None
        pending.extend(reader.feed(data))
    return pending.pop(0)


# --- payload helpers -------------------------------------------------------

def hello(token: str) -> bytes:
    t = token.encode()
    return b"ORC1" + struct.pack(">HH", VERSION, len(t)) + t


def parse_hello(p: bytes) -> tuple[int, str] | None:
    if len(p) < 8 or p[:4] != b"ORC1":
        return None
    version, tlen = struct.unpack_from(">HH", p, 4)
    if len(p) < 8 + tlen:
        return None
    return version, p[8:8 + tlen].decode(errors="replace")


def hello_ack(status: int, width: int, height: int, codec: int = 1) -> bytes:
    return struct.pack(">BHHB", status, width, height, codec)


def describe_input(m: Message) -> str:
    p = m.payload
    if m.type == Msg.MOUSE_MOVE:
        x, y = struct.unpack(">HH", p[:4])
        return f"mouse move  x={x / 65535:.3f} y={y / 65535:.3f}"
    if m.type == Msg.MOUSE_BUTTON:
        names = ["left", "right", "middle", "back", "forward"]
        b = names[p[0]] if p[0] < len(names) else str(p[0])
        return f"mouse {b} {'down' if p[1] else 'up'}"
    if m.type == Msg.MOUSE_WHEEL:
        dx, dy = struct.unpack(">hh", p[:4])
        return f"wheel dx={dx} dy={dy}"
    if m.type == Msg.KEY:
        vk, = struct.unpack(">H", p[:2])
        return f"key vk=0x{vk:02X} {'down' if p[2] else 'up'}"
    if m.type == Msg.RELEASE_ALL:
        return "release all"
    return f"type {m.type} ({len(p)} bytes)"


# --- HEVC Annex-B helpers ----------------------------------------------------

AUD_NAL = 35


def split_nals(data: bytes) -> list[bytes]:
    """Split an Annex-B stream into NAL units (without start codes)."""
    nals: list[bytes] = []
    i, n, start = 0, len(data), -1
    while i + 2 < n:
        if data[i] == 0 and data[i + 1] == 0 and data[i + 2] == 1:
            if start >= 0:
                end = i - 1 if i > start and data[i - 1] == 0 else i
                if end > start:
                    nals.append(data[start:end])
            i += 3
            start = i
        else:
            i += 1
    if 0 <= start < n:
        nals.append(data[start:])
    return nals


def nal_type(nal: bytes) -> int:
    return (nal[0] >> 1) & 0x3F


class AccessUnitSplitter:
    """Groups an Annex-B HEVC byte stream into access units, using AUD NALs
    (x265 aud=1 / NVENC aud=1) as boundaries. Each AU is returned in Annex-B form."""

    def __init__(self) -> None:
        self.buf = bytearray()

    def feed(self, data: bytes) -> list[bytes]:
        self.buf += data
        aus: list[bytes] = []
        # AUD = start code + NAL header 0x46 0x01 (type 35, layer 0, tid 1)
        marker = b"\x00\x00\x01\x46\x01"
        while True:
            first = self.buf.find(marker)
            if first < 0:
                break
            second = self.buf.find(marker, first + len(marker))
            if second < 0:
                break
            cut = second - 1 if second > 0 and self.buf[second - 1] == 0 else second
            begin = first - 1 if first > 0 and self.buf[first - 1] == 0 else first
            aus.append(bytes(self.buf[begin:cut]))
            del self.buf[:cut]
        return aus

    def flush(self) -> bytes | None:
        rest = bytes(self.buf)
        self.buf.clear()
        return rest or None
