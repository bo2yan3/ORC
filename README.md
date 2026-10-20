# ORC — Open Remote Control

A small, open-source, low-latency remote desktop for driving a Windows PC
(with an NVIDIA GPU) from a Mac. Built as a learning project and tuned for
CAD / PCB work (SolidWorks, Altium), where a mostly-still screen with fine
detail matters more than raw frame rate.

> **Status: Milestone 1 (LAN prototype).** The protocol, fake host and headless
> client are tested. The Windows host passes a syntax check but hasn't run on
> real hardware yet, and the Swift client hasn't been compiled yet.
> Use Parsec or Chrome Remote Desktop for anything you depend on.

## How it works

```
 Windows PC (host)                                    Mac (client)
 ┌───────────────────────────────┐                    ┌──────────────────────────────┐
 │ DXGI Desktop Duplication      │                    │ Network.framework (TCP)      │
 │   → BGRA frame                │   HEVC video  ───▶ │   → split Annex-B NAL units  │
 │ FFmpeg hevc_nvenc (RTX GPU)   │                    │   → CMSampleBuffer           │
 │   → HEVC Annex-B packets      │                    │   → AVSampleBufferDisplay-   │
 │ Winsock TCP server            │ ◀───  input        │     Layer (VideoToolbox HW)  │
 │ SendInput (mouse / keyboard)  │                    │ NSView mouse/keyboard events │
 └───────────────────────────────┘                    └──────────────────────────────┘
```

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and
[`docs/PROTOCOL.md`](docs/PROTOCOL.md).

## Repository layout

| Path | What |
|---|---|
| `host-windows/` | C++17 host: screen capture, NVENC encoding, input injection |
| `client-macos/` | Swift client: decode, display, capture input |
| `tools/` | Python protocol library, fake host (test the Mac client without Windows), tests |
| `docs/` | Architecture, wire protocol, roadmap |

## Quick start

### 1. Test the Mac client first (no Windows needed)

```sh
brew install ffmpeg
python3 tools/fake_host.py --token secret            # streams a test pattern on :47800
cd client-macos && swift run orc-client 127.0.0.1 secret
```

You should see a moving test pattern; clicks and keys are printed by the fake host.

Protocol tests and a headless end-to-end check (no GUI needed):

```sh
python3 tools/test_protocol.py
python3 tools/smoke_client.py 127.0.0.1 secret --frames 120 --out /tmp/out.hevc
```

### 2. Build the Windows host

Requirements: Windows 10/11, NVIDIA GPU with a current driver, Visual Studio 2022
(Desktop C++), CMake, and [vcpkg](https://vcpkg.io).

```bat
vcpkg install ffmpeg[nvcodec]:x64-windows
cd host-windows
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
build\Release\orc-host.exe --token <your-secret> --bitrate 40
```

### 3. Connect from the Mac

```sh
cd client-macos
swift run -c release orc-client <pc-ip> <your-secret>
```

Allow the "local network" prompt on macOS and the firewall prompt on Windows.

## Security

Milestone 1 has a shared-token check but **no encryption**. Only use it on your
home network or over [Tailscale](https://tailscale.com) (which encrypts the
link). Never port-forward it to the internet. Bind the host to your Tailscale
IP with `--bind 100.x.y.z`.

## Roadmap

See [`docs/ROADMAP.md`](docs/ROADMAP.md). The headline goal is a **lossless
refinement mode**: stream normally while the screen moves, then send a
pixel-perfect update once it settles, so CAD drawings and PCB traces are sharp
whenever you're actually reading them.

## License

MIT — see [`LICENSE`](LICENSE).
