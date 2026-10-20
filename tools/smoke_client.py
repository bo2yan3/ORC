#!/usr/bin/env python3
"""Headless test client: handshakes, receives N video frames, sends some input,
measures RTT, and writes the received stream to a .hevc file so it can be
checked with ffmpeg/ffprobe.

    python3 tools/smoke_client.py 127.0.0.1 secret --frames 60 --out /tmp/out.hevc
"""
from __future__ import annotations

import argparse
import socket
import struct
import time

from protocol import (DEFAULT_PORT, FLAG_KEYFRAME, Msg, Reader, hello, pack, recv_message)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("token")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--out")
    a = ap.parse_args()

    sock = socket.create_connection((a.host, a.port), timeout=10)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    reader, pending = Reader(), []
    sock.sendall(pack(Msg.HELLO, hello(a.token)))
    ack = recv_message(sock, reader, pending)
    if not ack or ack.type != Msg.HELLO_ACK:
        print("FAIL: no HELLO_ACK"); return 1
    status, w, h, codec = struct.unpack(">BHHB", ack.payload[:6])
    print(f"HELLO_ACK status={status} {w}x{h} codec={codec}")
    if status != 0:
        return 1

    # Some input, as the Mac client would send it
    sock.sendall(pack(Msg.MOUSE_MOVE, struct.pack(">HH", 32768, 16384)))
    sock.sendall(pack(Msg.MOUSE_BUTTON, bytes([0, 1])) + pack(Msg.MOUSE_BUTTON, bytes([0, 0])))
    sock.sendall(pack(Msg.KEY, struct.pack(">HB", 0xA2, 1)) + pack(Msg.KEY, struct.pack(">HB", 0x43, 1)))
    sock.sendall(pack(Msg.RELEASE_ALL))
    sock.sendall(pack(Msg.PING, struct.pack(">Q", time.monotonic_ns() // 1000)))

    frames, keyframes, total, rtt = 0, 0, 0, None
    first_is_key = None
    out = open(a.out, "wb") if a.out else None
    t0 = time.monotonic()
    while frames < a.frames:
        m = recv_message(sock, reader, pending)
        if m is None:
            print("FAIL: host closed"); return 1
        if m.type == Msg.VIDEO:
            key = bool(m.flags & FLAG_KEYFRAME)
            if first_is_key is None:
                first_is_key = key
            frames += 1
            keyframes += key
            total += len(m.payload)
            if out:
                out.write(m.payload[8:])
        elif m.type == Msg.PONG:
            sent, = struct.unpack(">Q", m.payload)
            rtt = (time.monotonic_ns() // 1000 - sent) / 1000
    dt = time.monotonic() - t0
    if out:
        out.close()
    print(f"frames={frames} keyframes={keyframes} first_is_keyframe={first_is_key} "
          f"fps={frames / dt:.1f} Mbps={total * 8 / dt / 1e6:.2f} rtt_ms={rtt}")
    sock.close()
    return 0 if first_is_key else 1


if __name__ == "__main__":
    raise SystemExit(main())
