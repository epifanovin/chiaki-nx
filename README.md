
![chiaki-ng Logo](gui/res/chiaking-logo.svg)

# [chiaki-ng](https://streetpea.github.io/chiaki-ng/)

An open source PlayStation remote play project serving as the next-generation of Chiaki with improvements and ongoing support now that the original Chiaki project is in maintenance mode only. [Click here to see the accompanying site for documentation, updates and more](https://streetpea.github.io/chiaki-ng/).

## Discord
[chiaki-ng community Discord](https://discord.gg/tAMbRuwXDH)

## Disclaimer
This project is not endorsed or certified by Sony Interactive Entertainment LLC.

Chiaki is a Free and Open Source Software Client for PlayStation 4 and PlayStation 5 Remote Play
for Linux, FreeBSD, OpenBSD, Android, macOS, Windows, Nintendo Switch and potentially even more platforms.

## Nintendo Switch (chiaki-nx)

The Switch build uses the Deko3D native GPU API and is optimized for the Tegra X1 hardware.

### Features

- **Deko3D rendering** -- native Tegra GPU path, no OpenGL/Vulkan translation overhead
- **HEVC (H.265) streaming** -- up to 1080p from PS4/PS5
- **Jitter buffer** -- 1-frame FIFO absorbs PS5 encode timing variation for stutter-free 60fps playback (~8ms avg latency cost)
- **Debanding filter** -- GPU shader that smooths compression banding in gradients at no measurable performance cost
- **Early IDR recovery** -- requests keyframes on frame sequence gaps, not just FEC failure, for faster recovery from packet loss
- **Bitrate control** -- adjustable 0-50 Mbps with auto mode
- **DualSense haptics** -- rumble passthrough with granular intensity control (1% steps)
- **Stats HUD** -- real-time overlay showing FPS, decode time, and network statistics
- **On-screen keyboard** -- for PSN login and text input

### Building for Switch

Requires devkitPro with devkitA64, libnx, and switch portlibs.

```bash
./scripts/switch/build.sh
```

Output: `build_switch/switch/chiaki-nx.nro`

### Deploying

```bash
/opt/devkitpro/tools/bin/nxlink -a <SWITCH_IP> -s build_switch/switch/chiaki-nx.nro
```
