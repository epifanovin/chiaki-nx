# AGENTS.md

## Mission
- Primary target: Nintendo Switch performance and stability for `chiaki-ng`.
- Keep PS4/PS5 Remote Play compatibility.
- Use Moonlight-Switch as a reference for optimization ideas, not as a drop-in replacement.
- Prioritize Deko3D path and keep fallback paths buildable when possible.

## Repositories In Scope
- `/Users/ilaepifanov/Development/Switch/chiaki-ng` (main implementation repo)
- `/Users/ilaepifanov/Development/Switch/Moonlight-Switch` (reference repo for optimizations)

## How We Work
- Every change must be done in its own branch.
- Branch names should be task-specific (recommended prefix: `codex/`).
- Do not work directly on `main`.
- Every valuable change must be committed.
- Keep commits focused and atomic (one logical change per commit).
- Write clear commit messages describing what changed and why.
- Before risky resets/rebases, create a safety branch first.

## Build Process (Switch)
- Preferred build script:
  - `cd /Users/ilaepifanov/Development/Switch/chiaki-ng`
  - `./scripts/switch/build.sh`
- Manual configure/build (common workflow):
  - `cmake -B build_switch -GNinja ...`
  - `cmake --build build_switch -j6`
- Expected artifact:
  - `build_switch/switch/chiaki-nx.nro`

## Test Process
- Smoke test on real hardware after each meaningful commit:
  - Launch app
  - Wake console
  - Connect/start stream
  - Validate video, audio, input, haptics
  - Open/close overlay and stop streaming flow
- Stability/perf checks:
  - Long session test (target 30+ minutes)
  - Check for stalls, frame drops, reconnects, crashes
  - Validate both handheld and docked behavior when relevant

## Netload / Deploy
- Use `nxlink` to push test builds:
  - `/opt/devkitpro/tools/bin/nxlink -a <SWITCH_IP> -s /path/to/chiaki-nx.nro`

## Device Endpoints (Current Lab Setup)
- Switch IP: `192.168.6.224`
- FTP server: `192.168.6.224:1488`
- Note: IPs/ports can change by network/session. Verify on-device before long runs.

## Logs and Debug Data
- Primary runtime logs: `nxlink` stdout/stderr stream from the running app.
- Keep full `nxlink` logs for each test session when debugging regressions.
- FTP workflow used for post-crash analysis:
  - Start FTP on Switch.
  - Connect from host to `192.168.6.224:1488`.
  - Pull logs/crash files to local machine for inspection.
  - First target path: `/atmosphere/crash_reports`.
  - Also inspect app/config folders on SD when needed (for repro artifacts).
- Example FTP pull (lftp):
  - `lftp -e "open 192.168.6.224:1488; mirror --verbose /atmosphere/crash_reports ./switch_crash_reports; bye"`
- Crash investigation fallback:
  - Pull crash reports and related files from Switch storage via FTP.
  - Typical location to check: `/atmosphere/crash_reports`.
- When reporting results, include:
  - commit hash
  - test scenario
  - relevant log excerpt

## Quality Gate Before Merge
- Branch builds successfully.
- `.nro` runs on Switch.
- No known regressions vs previous working commit for the tested scenario.
- Changes are committed and history is understandable.
