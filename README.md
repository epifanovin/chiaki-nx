![chiaki-nx Logo](switch/nro_icon.jpg) 

# chiaki-nx

A Nintendo Switch PlayStation Remote Play client, based on [chiaki-ng](https://github.com/streetpea/chiaki-ng).

This fork focuses exclusively on the Switch platform — optimizing streaming quality, input latency, and audio/video stability for the Tegra X1 hardware.

## Disclaimer

This project is not endorsed or certified by Sony Interactive Entertainment LLC.


## Installation and usage

- Put .nro to /switch folder, connect to PS like in og Chiaki.
- To exit to menu touch left screen edge to stop streaming

## What chiaki-nx adds over chiaki-ng

The upstream chiaki-ng Switch build uses SDL/Mesa for rendering and provides basic streaming functionality. chiaki-nx replaces and extends this with:

### Rendering

- **Deko3D native GPU renderer** — bypasses OpenGL/Mesa entirely, talks directly to the Tegra GPU through Nintendo's native API. Based on the Deko3D rendering approach pioneered by XITRIX and Cooler3D in [Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch).
- **Debanding shader** — GPU fragment shader that smooths compression banding in gradients (Off/Low/Medium/High presets, no measurable performance cost)
- **Triple-buffered swapchain** with display-paced vsync for tear-free presentation

### Frame Smoothing

- **FIFO jitter buffer** — decoded frames queue through a configurable-depth FIFO instead of latest-frame-wins, absorbing PS5 encode timing variation
- **Adaptive condvar wait** — when the buffer runs dry, a vsync-budget-aware timed wait catches late frames instead of immediately underflowing
- **Configurable buffer depth** — Smooth (1-frame, ~17ms latency) and Smoothest (4/6/8-frame) presets for trading latency against stutter resistance in heavy scenes
- **FIFO pre-fill gating** — rendering waits until the buffer reaches target depth at stream start, eliminating startup stutter

### Audio

- **Audren backend** — native Switch audio driver replacing SDL audio, with lower latency and direct hardware submission
- **Jitter-resistant audio buffering** — increased wavebuf count and configurable extra latency to absorb Wi-Fi/Bluetooth coexistence jitter on the Switch's shared radio

### Streaming & Network

- **HEVC (H.265)** — hardware-accelerated decoding up to 1080p from PS4/PS5
- **Early IDR recovery** — requests keyframes on frame sequence gaps (not just FEC failure), cutting recovery time from packet loss
- **Enlarged UDP receive buffer** (512KB) — reduces packet loss from Wi-Fi burst delays
- **Adjustable bitrate** — 0–50 Mbps with auto mode

### Input & Haptics

- **Dedicated 120Hz input polling thread** — reads HID directly on core 1, bypassing SDL for lower input latency
- **CPU core pinning** — input on core 1, decoder/network threads on cores 2–3, preventing scheduling contention
- **DualSense haptics** — rumble passthrough with 1% granularity (0–100%)
- **Stick deadzone** — configurable 0–20%

### UI & Quality of Life

- **Stats HUD** — real-time overlay with FPS, decode time, network statistics
- **On-screen keyboard** — system swkbd for PSN login and text input
- **Auto-connect** — remembers last host, reconnects on launch
- **Host discovery** — real-time online/offline tracking with Connect button state
- **In-stream overlay** — stop streaming and settings access without leaving the stream

## Building

Requires [devkitPro](https://devkitpro.org/) with devkitA64, libnx, and switch portlibs.

```bash
./scripts/switch/build.sh
```

Output: `build_switch/switch/chiaki-nx.nro`

## Deploying

```bash
/opt/devkitpro/tools/bin/nxlink -a <SWITCH_IP> -s build_switch/switch/chiaki-nx.nro
```

## Credits

- [Chiaki](https://git.sr.ht/~thestr4ng3r/chiaki) by **Florian Märkl (thestr4ng3r)** — the original open-source PlayStation Remote Play client that started it all
- [chiaki-ng](https://github.com/streetpea/chiaki-ng) by **Street Pea** — next-generation continuation with active development and multi-platform support
- **h0neybadger** — original Nintendo Switch port with Borealis GUI, audio fixes, discovery, and PS5 support
- **kkWong** — Switch FFmpeg hardware acceleration integration
- **xlanor** — Switch environment updates and holepunch fixes
- [Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch) by **XITRIX** and **Cooler3D** — the Deko3D native GPU rendering implementation that served as the foundation and reference for chiaki-nx's renderer

## License

This project is licensed under the GNU Affero General Public License v3.0 — see [LICENSE](LICENSE) for details.
