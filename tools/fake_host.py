#!/usr/bin/env python3
"""Stand-in for orc-host: streams an FFmpeg test pattern over the real ORC
protocol and prints the input it receives. Lets you build and test the Mac
client without the Windows PC.

    brew install ffmpeg
    python3 tools/fake_host.py --token secret
    swift run orc-client 127.0.0.1 secret
"""
from __future__ import annotations

import argparse
import socket
import subprocess
import threading
import time

from protocol import (DEFAULT_PORT, FLAG_KEYFRAME, VERSION, AccessUnitSplitter, Msg, Reader,
                      describe_input, hello_ack, nal_type, pack, parse_hello, recv_message,
                      split_nals)

KEYFRAME_TYPES = {16, 17, 18, 19, 20, 21}  # BLA / IDR / CRA


def ffmpeg_cmd(w: int, h: int, fps: int) -> list[str]:
    gop = fps * 2  # x265 can't take keyframe requests, so refresh every 2 s
    return [
        "ffmpeg", "-loglevel", "error", "-re",
        "-f", "lavfi", "-i", f"testsrc2=size={w}x{h}:rate={fps}",
        "-c:v", "libx265", "-preset", "ultrafast", "-tune", "zerolatency",
        "-x265-params", f"aud=1:repeat-headers=1:keyint={gop}:bframes=0:log-level=error",
        "-b:v", "8M", "-f", "hevc", "-",
    ]


def stream_video(sock: socket.socket, send_lock: threading.Lock, stop: threading.Event,
                 w: int, h: int, fps: int) -> None:
    proc = subprocess.Popen(ffmpeg_cmd(w, h, fps), stdout=subprocess.PIPE)
    splitter = AccessUnitSplitter()
    t0 = time.monotonic()
    started = False
    try:
        while not stop.is_set():
            chunk = proc.stdout.read(65536)
            if not chunk:
                break
            for au in splitter.feed(chunk):
                key = any(nal_type(n) in KEYFRAME_TYPES for n in split_nals(au) if n)
                if not started and not key:
                    continue  # the client needs a keyframe (with VPS/SPS/PPS) first
                started = True
                pts = int((time.monotonic() - t0) * 1e6)
                msg = pack(Msg.VIDEO, pts.to_bytes(8, "big") + au, FLAG_KEYFRAME if key else 0)
                with send_lock:
                    sock.sendall(msg)
    except OSError:
        pass
    finally:
        proc.kill()
        stop.set()


def serve(args: argparse.Namespace) -> None:
    ls = socket.create_server((args.bind, args.port), reuse_port=False)
    print(f"fake host listening on {args.bind}:{args.port} ({args.width}x{args.height} @ {args.fps})")
    while True:
        sock, addr = ls.accept()
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        print(f"connection from {addr[0]}")
        reader, pending = Reader(), []
        m = recv_message(sock, reader, pending)
        parsed = parse_hello(m.payload) if m and m.type == Msg.HELLO else None
        if not parsed:
            print("bad hello"); sock.close(); continue
        version, token = parsed
        status = 2 if version != VERSION else (0 if token == args.token else 1)
        sock.sendall(pack(Msg.HELLO_ACK, hello_ack(status, args.width, args.height)))
        if status:
            print("rejected:", "bad token" if status == 1 else "bad version"); sock.close(); continue

        stop, lock = threading.Event(), threading.Lock()
        threading.Thread(target=stream_video, args=(sock, lock, stop, args.width, args.height, args.fps),
                         daemon=True).start()
        try:
            while not stop.is_set():
                m = recv_message(sock, reader, pending)
                if m is None:
                    break
                if m.type == Msg.PING:
                    with lock:
                        sock.sendall(pack(Msg.PONG, m.payload))
                elif m.type == Msg.KEYFRAME_REQ:
                    print("keyframe requested (next one within 2 s)")
                elif not (m.type == Msg.MOUSE_MOVE and args.quiet_moves):
                    print(describe_input(m))
        except OSError:
            pass
        stop.set()
        sock.close()
        print("session ended")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--token", required=True)
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1200)
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--quiet-moves", action="store_true", help="don't print mouse moves")
    serve(ap.parse_args())


if __name__ == "__main__":
    main()
