# Roadmap

Each milestone ends with something you can actually use or measure.

## M0 — Mac client against fake host  ⟵ start here
- [ ] `python3 tools/fake_host.py` streams a test pattern on the Mac
- [ ] `swift run orc-client 127.0.0.1 <token>` shows it
- [ ] Clicks, scrolls and keys show up in the fake host's log

## M1 — Windows host on the home network
- [ ] Host builds with vcpkg FFmpeg + NVENC
- [ ] Desktop visible on the Mac, mouse and keyboard work
- [ ] Measure: latency (PING/PONG round trip + frame age), bitrate, host CPU
- [ ] Stress: UAC prompt, resolution change, unplugging the client mid-drag (no stuck keys)

## M2 — Away from home
- [ ] Works over Tailscale from campus Wi-Fi and a phone hotspot
- [ ] Client auto-reconnects; keyframe on decode failure
- [ ] Bitrate setting in the client; on-screen stats overlay (fps, Mbps, RTT)
- [ ] Check colours against the real monitor (BT.709 matrix, full vs limited range)

## M3 — Quality of life
- [ ] Zero-copy: D3D11 texture → NVENC (no CPU memcpy)
- [ ] Cursor shape forwarding
- [ ] Clipboard text sync both ways
- [ ] Virtual display at the Mac's 16:10 resolution (IddCx virtual display driver)

## M4 — Better transport
- [ ] Video over UDP with sequence numbers, NACK/retransmit or FEC
- [ ] Adaptive bitrate from loss and delay
- [ ] Stays smooth with 1–2 % packet loss

## M5 — Lossless refinement mode (the differentiator)
- [ ] Detect "screen settled" (no changes for ~200 ms)
- [ ] Send changed regions losslessly (e.g. zstd/QOI tiles of the BGRA frame)
- [ ] Client composites refinement tiles over the decoded video until the next change
- [ ] Compare against Parsec Free on SolidWorks drawings and Altium traces (screenshots, zoomed)

## M6 — Experiments
- [ ] HEVC 4:4:4 from NVENC; check whether the M3 decodes it in hardware
- [ ] AV1 (RTX 5070 NVENC supports it; check M3 decode support)
- [ ] Write-up: design, measurements, what worked — for the portfolio
