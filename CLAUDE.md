# V2V Communication with SCADA — project rules for Claude Code

## Source of truth
- docs/V2V_BUILD_GUIDE.md. Part A = build phases. Part B = specification (§S1–§S17).
- Never change pins, the packet layout, the Modbus register map or thresholds unless I ask.
  If the spec looks wrong or contradictory, STOP and ask me.
- Items marked ⚠️ VERIFY: ask me. Never guess datasheet facts.
- Answers I give to VERIFY items are recorded in docs/verified.md. Read it at the start of each session.

## Hardware (you cannot see it)
- 2 × STM32F103C8T6 Blue Pill, E28-2G4M20SX (SX1281), Neo-6M GPS, MPU-6050, 18650 power chain.
- OBU #1 = vehicle only. OBU #2 = vehicle + SCADA gateway (USB CDC, Modbus RTU slave ID 1).
- When a step needs a physical action, STOP and tell me exactly what to do and what output to paste back.

## Build & test commands
- FQBN: tools/fqbn.txt
- Compile firmware: arduino-cli compile --fqbn "$(cat tools/fqbn.txt)" firmware/v2v_obu
- Upload (SWD via ST-Link): arduino-cli upload --fqbn "$(cat tools/fqbn.txt)" firmware/v2v_obu
- Read board output: tools/.venv python tools/serial_log.py <PORT> <SECONDS>
- Host unit tests: python tests/host/run_tests.py
- After every code change: compile the firmware AND run the host unit tests. Fix all errors and warnings.

## Code rules
- Portable logic files (v2v_packet, geo, neighbors, safety) must NOT include Arduino.h.
  They take time as a `uint32_t now_ms` parameter so they can be unit-tested on the PC.
- Hardware access only in the driver files (radio, gps, imu, power, alerts, gateway, sim).
- All constants live in firmware/v2v_obu/config.h. No magic numbers elsewhere.
- No dynamic memory allocation (no new/malloc/String) in firmware. No blocking delay() in loop(), except during boot.
- Keep function and file names as given in §S10.3.

## Working style
- Work only on the phase I name. At the end of a phase, list what was done, how it was tested,
  anything not finished, and suggest a git commit message.

## Environment notes (Windows, added in Phase 0)
- Python venv: tools/.venv/Scripts/python.exe  (use it for every tools/*.py script)
- g++: WinLibs MinGW (winget BrechtSanders.WinLibs.POSIX.UCRT). If not on PATH in a fresh shell, refresh PATH from the registry.
- docs/V2V_SCADA_Project_Plan.md is an OLDER draft. The build guide wins where they differ.
