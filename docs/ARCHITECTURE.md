# Architecture

## Host (Windows, C++17)

| File | Responsibility |
|---|---|
| `capture.cpp` | DXGI Desktop Duplication on one monitor. Returns a CPU copy of the BGRA frame when the screen changed. Recreates itself on `DXGI_ERROR_ACCESS_LOST` (UAC prompt, resolution change, fullscreen app). |
| `encoder.cpp` | FFmpeg `hevc_nvenc`: preset `p1`, tune `ull`, zero-latency, no B-frames, CBR. Input is `bgr0`, so the RGB→YUV conversion happens on the GPU. Keyframes on request. |
| `input.cpp` | Turns protocol messages into `SendInput` calls. Tracks what is held so it can release everything on disconnect. |
| `net.cpp` | Winsock helpers: listen, framed send (mutex-protected) and receive. |
| `main.cpp` | Arg parsing, accept loop, one session at a time: a video thread (capture → encode → send) and the main thread receiving input. |

Threads per session:

```
video thread:   capture.next() ─▶ encoder.encode() ─▶ send VIDEO   (loops until session ends)
main thread:    recv message ─▶ input.apply() / keyframe flag / PONG
```

### Known milestone-1 shortcuts

- **CPU copy of every frame.** Capture copies the GPU texture to a staging
  texture, then into an `AVFrame`. At 1440p60 that's about 0.9 GB/s of memcpy.
  Fine on a desktop CPU; Milestone 3 passes the D3D11 texture straight to NVENC.
- **Cursor not in the image.** Desktop Duplication doesn't draw the pointer.
  The Mac shows its own cursor over the stream instead, which feels instant.
  Cursor *shape* (I-beam, resize arrows) is a later milestone.
- **TCP.** Simple and reliable, but one lost packet stalls everything behind
  it. Good on a LAN; Milestone 4 moves video to UDP.

## Client (macOS, Swift)

| File | Responsibility |
|---|---|
| `Connection.swift` | `NWConnection` TCP, message framing, send helpers. |
| `HEVCDepacketizer.swift` | Splits Annex-B into NAL units, collects VPS/SPS/PPS, builds the `CMVideoFormatDescription`, converts to length-prefixed samples, makes `CMSampleBuffer`s. |
| `StreamView.swift` | `NSView` hosting an `AVSampleBufferDisplayLayer` (hardware decode via VideoToolbox). Captures mouse, scroll and keyboard, maps to Windows input. |
| `KeyMap.swift` | macOS key code → Windows virtual-key table. Cmd is sent as Ctrl. |
| `main.swift` | App setup, window, wiring. |

### Why `AVSampleBufferDisplayLayer` instead of `VTDecompressionSession`

The layer decodes with VideoToolbox internally and presents with the right
timing. `DisplayImmediately` on each sample means no buffering. It's less code,
and a decompression session can be swapped in later if we need the decoded
pixels (e.g. for the lossless-refinement overlay).

## Tools (Python)

- `protocol.py` — reference implementation of the wire format.
- `fake_host.py` — streams an FFmpeg test pattern using the real protocol and
  prints incoming input. Lets the Mac client be developed on the Mac alone.
- `smoke_client.py` — headless client: handshake, receive frames, send input,
  measure RTT, save the stream for `ffprobe`. Works against the fake host or
  the real Windows host.
- `test_protocol.py` — unit tests for framing and the HEVC access-unit splitter.
