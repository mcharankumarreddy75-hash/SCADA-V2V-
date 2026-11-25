# Field test plan (Phase 8)

Acceptance tests A1-A14 from spec §S14, the logging build, the data files, and the threshold tuning of §S13.3.
Run from the project root in Git Bash or PowerShell. The Python commands use `tools/.venv/Scripts/python.exe`.

Replace `COM5` (gateway, OBU #2 USB) and `COM7` (debug UART, OBU #1 or #2 Serial1) with the real ports. Run `tools/.venv/Scripts/python.exe tools/serial_log.py --list` to see them.

---

## 0. Before any test

1. **Clean the historian.** `scada/historian/v2v.db` already holds 361 telemetry rows and 165 alarm rows from earlier simulator runs. Move it aside before A1 (`move scada\historian\v2v.db scada\historian\v2v_before_field.db`). Or record your test windows and pass them to the analyzer with `--from` / `--to`.
2. **One program per COM port.** The gateway port (COM5) belongs to either Node-RED (SCADA) or `tools/modbus_probe.py`, never both. For probe-based tests, stop the SCADA first.
3. **Calibrate the IMU every boot (§S13.2).** Keep both boards still and level for 2 s after power-up. If they moved during boot, reboot.
4. **Write the local start and stop times** of every run in the Notes column. The SCADA database is indexed by epoch ms; the analyzer converts local times with `--from` / `--to`.
5. **GPS fix outdoors.** Positions and distances need `gps=1` in the debug line and `dist=` in the probe output.

## 1. Firmware builds and flashing

All builds go to `build/obu<ID>/`. A flash is one command: without `--compile-only` the board is uploaded via SWD (ST-Link). Compile first to check, then upload.

| Build | Command | Used for |
|---|---|---|
| OBU #1 normal | `tools/.venv/Scripts/python.exe tools/flash.py --id 1` | A1, A2, A4, A5, A10, A11, A13 |
| OBU #1 data logging | `tools/.venv/Scripts/python.exe tools/flash.py --id 1 --log` | A6, A7, A8, A9, A12 and §S13.3 tuning (CSV on Serial1) |
| OBU #2 gateway (SCADA) | `tools/.venv/Scripts/python.exe tools/flash.py --id 2 --gateway` | Gateway for all tests, Modbus on USB |
| OBU #2 gateway, logging | `tools/.venv/Scripts/python.exe tools/flash.py --id 2 --gateway --log` | A6 (TTC of the approaching vehicle, CSV on Serial1) |
| SIM demo | `tools/.venv/Scripts/python.exe tools/flash.py --id 1 --sim` and `tools/.venv/Scripts/python.exe tools/flash.py --id 2 --gateway --sim` | A14 only |

Notes:
- `--log` changes only the output: the 1 Hz text line is off, and a 20 Hz CSV is printed on Serial1 (and on USB for OBU builds). Behaviour is otherwise the same as the normal build, so run A12 and the tuning with the `--log` build of the vehicle you log.
- In a gateway build, USB belongs to Modbus. Its CSV therefore goes out on Serial1 only, through a USB-UART adapter.
- **To confirm before the first run:** which pins carry Serial1 (the debug UART), so the adapter is wired correctly. I have not verified this from the datasheet.

## 2. Data logging (what the files contain)

### 2.1 CSV from the logging build (`--log`)

Header (once at boot, per port), then one row every 50 ms (20 Hz, emitted in the FCW tick):

```
ms,a_long_x100,a_total_x100,az_hp_x100,roll_x100,pitch_x100,speed_x100,level,ttc_x100
```

Fixed point: the integer is the value times 100 (no float printf on newlib-nano). The analyzer divides by 100 for you.

| Column | Unit after /100 | Meaning |
|---|---|---|
| `ms` | ms | `millis()` on the board (time since boot) |
| `a_long` | m/s² | low-passed forward acceleration (EEBL uses it, threshold `HARD_BRAKE_MS2`) |
| `a_total` | g | magnitude of the acceleration vector (crash uses it, threshold `CRASH_G`) |
| `az_hp` | m/s² | high-passed vertical acceleration (pothole, threshold `POTHOLE_G` × 9.81) |
| `roll`, `pitch` | deg | complementary-filter angles (rollover uses `ROLLOVER_DEG`) |
| `speed` | m/s | GPS speed (or sim speed) |
| `level` | 0 / 1 / 2 | FCW level after hysteresis: 0 safe, 1 Caution, 2 Critical |
| `ttc` | s | lowest TTC over all neighbours; **-1 = no TTC (infinite)** |

Record it with `tools/serial_log.py`. The `--out` file keeps the raw lines; the console also shows a `HH:MM:SS.mmm` prefix on each line:

```
tools/.venv/Scripts/python.exe tools/serial_log.py COM7 600 --out data/normal_drive_1.csv
```

When a test needs the laptop clock (A8, A11, A7), keep the timestamped console too:

```
tools/.venv/Scripts/python.exe tools/serial_log.py COM7 600 --out data/a8_run1.csv > data/a8_run1_console.txt
```

`log_analyzer.py` accepts both (it skips the prefix and any non-data line).

### 2.2 SCADA historian (`scada/historian/v2v.db`)

One row per vehicle per second in `telemetry`: `ts` (epoch ms), `vehicle_id`, `lat`, `lon`, `speed_kmh`, `heading`, `accel`, `roll`, `pitch`, `flags`, `battery` (%), `rssi` (dBm), `pdr` (%), `distance` (m, NULL for own vehicle), `ttc` (s, NULL when none), `warn` (0/1/2).

Alarms in `alarms`: `ts`, `vehicle_id`, `type` (`Crash / SOS`, `Rollover`, `Collision risk`, `Hard braking`, `Road hazard`, `V2V link lost`, `GPS fix lost`, `Low battery`, `Gateway offline`), `severity`, `lat`, `lon`, `acked`, `cleared_ts`.

### 2.3 Gateway probe (`tools/modbus_probe.py`, Modbus FC03 at 1 Hz)

```
tools/.venv/Scripts/python.exe tools/modbus_probe.py --serial COM5 --count 300 > data/a1_probe.txt
```

Prints one line per read: gateway counters (`rx`, `tx`, `crc`, `rtt`, `acks`) and one line per vehicle slot (`dist=`, `ttc=`, `pdr=`, `rssi=`, `sats=`). Stop the SCADA first (see §0).

### 2.4 Where the files go

Everything field-related goes in `data/` and is committed as evidence (`.gitignore` excludes only `*.db`, so copy the historian to `data/field_<date>.db` only if you want it in git; otherwise keep it local). Use these names:

| File | Test |
|---|---|
| `data/a1_probe.txt` | A1 |
| `data/a2_range_run1.txt` (SCADA export or probe) and `data/a2_range_run2.txt` | A2 |
| `data/a3_ping_probe.txt` | A3 (the 100 PING readbacks) |
| `data/a4_pdr50m_probe.txt` | A4 |
| `data/distance_tape.csv` (you type the averages) | A5 |
| `data/a6_fcw_run1.csv` … `run5` | A6 |
| `data/a7_eebl_run1.csv` + the video file (not in git) | A7 |
| `data/a8_run1.csv` + `data/a8_run1_console.txt` | A8 |
| `data/a9_rollover_run1.csv` | A9 |
| `data/a10_commands.txt` | A10 |
| `data/a11_linkloss_console.txt` | A11 |
| `data/a12_normal_30min.csv` + `data/a12_normal_30min_console.txt` | A12 |
| `data/a13_battery_run1.txt` | A13 |
| `data/a14_sim_notes.txt` | A14 |
| `data/normal_drive_1.csv` | §S13.3 threshold tuning |
| `scada/historian/v2v.db` (copy it to `data/field_<date>.db` at the end of each day) | A2, A6, A11, A12, A13 |

Analysis commands (all PNGs and `summary.md` go to `docs/results/`):

```
tools/.venv/Scripts/python.exe tools/log_analyzer.py --db scada/historian/v2v.db --from "2026-12-01T10:00:00" --to "2026-12-01T10:30:00"
tools/.venv/Scripts/python.exe tools/log_analyzer.py --rtt data/a3_ping_probe.txt
tools/.venv/Scripts/python.exe tools/log_analyzer.py --distance-error data/distance_tape.csv
tools/.venv/Scripts/python.exe tools/log_analyzer.py --imu-log data/a6_fcw_run1.csv
tools/.venv/Scripts/python.exe tools/log_analyzer.py --imu-log data/normal_drive_1.csv
```

The analyzer reads every `--imu-log` file, so run it once per file. Each run replaces only its own sections in `docs/results/summary.md`.

---

## 3. The tests

Pass criteria are those of §S14. The Phase 8 exit needs PASS for the Level-1 features (§S3).

### A1 Packet exchange, 5 min
- **Set-up:** OBU #1 (normal build) and OBU #2 (gateway) stationary, 10 m apart, open area. SCADA stopped.
- **Procedure:** power both, wait for the first GPS fix, then run the probe for 300 s.
- **Data:** `data/a1_probe.txt`.
- **Pass:** about 5 packets/s per neighbour, i.e. the `rx` counter rises by ~1500 in 300 s with 1 neighbour; CRC errors `crc / (rx + crc) < 1 %`.

### A2 Range, 50 m steps, open ground
- **Set-up:** OBU #2 (gateway, with SCADA) stationary at the start; OBU #1 walks or drives away on a straight open road, line of sight.
- **Procedure:** stop at 50, 100, 150, 200, 250, 300 m (GPS distance shown on SCADA). Dwell 60 s at each step, then move on. Stop when PDR falls below 90 % for 30 s. Do two runs in opposite directions.
- **Data:** SCADA DB (`pdr`, `distance`, `rssi` for each step). Note each step's start and stop times in the Notes column.
- **Analysis:** `log_analyzer.py --db ... --from ... --to ...` (PDR vs distance, RSSI vs distance).
- **Pass:** PDR ≥ 90 % at ≥ 200 m line of sight. Record the largest distance with PDR ≥ 90 %.

### A3 Latency, 100 PINGs
- **Set-up:** SCADA stopped. Both OBUs as in A1.
- **Procedure:** run 100 PINGs (command 5, target 1), one per second, and record the gateway's last-RTT readback:
  ```
  for i in $(seq 100); do tools/.venv/Scripts/python.exe tools/modbus_probe.py --serial COM5 --command 5 0 1; sleep 1; done > data/a3_ping_probe.txt
  ```
  PowerShell: `1..100 | ForEach-Object { tools/.venv/Scripts/python.exe tools/modbus_probe.py --serial COM5 --command 5 0 1; Start-Sleep 1 } | Out-File -Encoding utf8 data/a3_ping_probe.txt`
  Check that the ACK counter (`reg4`) rises by 1 each time. A repeated RTT value with no ACK change is a stale readback: note it.
- **Data:** `data/a3_ping_probe.txt`.
- **Analysis:** `log_analyzer.py --rtt data/a3_ping_probe.txt` (one-way = RTT/2).
- **Pass:** mean RTT/2 < 50 ms and max RTT/2 < 100 ms.

### A4 PDR at 50 m, 1000 BSMs
- **Set-up:** both stationary, exactly 50 m apart (tape or GPS distance on SCADA).
- **Procedure:** BSMs run at 5 Hz, so 1000 BSMs take about 200 s. Run the probe for 240 s.
- **Data:** `data/a4_pdr50m_probe.txt` (rx and tx counters; the `pdr` field in the vehicle line), plus the SCADA DB for the same window.
- **Pass:** PDR ≥ 95 %. PDR here is the rx counter increase divided by the expected count (1000 for 1000 BSMs). Note the calculation in Notes.

### A5 Distance vs tape at 10 / 25 / 50 / 100 m
- **Set-up:** OBU #1 and OBU #2 stationary, GPS fixed. Measure the spacing with a tape, to 0.1 m.
- **Procedure:** at each distance, run `tools/.venv/Scripts/python.exe tools/modbus_probe.py --serial COM5 --count 60 > data/a5_probe_<m>m.txt` (60 s). Average the `dist=` values in the file. Put the tape value and the average into `data/distance_tape.csv` (header `tape_m,gps_m`, one row per reading; repeat each distance 2-3 times if you can).
- **Analysis:** `log_analyzer.py --distance-error data/distance_tape.csv`.
- **Pass:** |GPS − tape| ≤ 5 m at each distance.
- **Not in this test:** §S13.4 radio ranging is an L3 feature. Skip it unless ranging is implemented.

### A6 FCW approach
- **Set-up:** OBU #1 (normal build) stationary at the side of a straight road. OBU #2 (gateway, `--log`, Serial1 CSV) starts about 80 m away. Both have GPS fix. Close the gap at about 15 km/h, straight towards OBU #1, stop about 2 m short.
- **Procedure:** 5 approaches. For each one, start the serial log before you move, stop it after the stop. Watch the SCADA link page (warning and TTC) at the same time.
- **Data:** `data/a6_fcw_run<N>.csv`, SCADA DB for the window.
- **Analysis:** `log_analyzer.py --imu-log data/a6_fcw_run<N>.csv` (the TTC table at each level transition) and the SCADA `ttc` / `warn` columns.
- **Pass:** Caution at TTC 3.5-4.5 s and Critical at TTC 1.5-2.5 s (the 4 s and 2 s targets ±0.5 s). The warning must also appear on SCADA.

### A7 EEBL, follower alert < 200 ms
- **Set-up:** two vehicles in one lane, OBU #2 (the follower) about 20 m behind OBU #1, both at about 20 km/h in a closed area. OBU #1 is the `--log` build. Film both vehicles' brake lights and OBU #2's buzzer/LED with a phone in slow motion (240 fps) so the onset is visible in the same frame.
- **Procedure:** OBU #1 brakes hard (a_long below −3.9 m/s², about 0.4 g), then coasts to a stop. Repeat 5 times.
- **Data:** the CSV from OBU #1 (`a_long`, time of the first sample below −3.9 m/s²), the video, and the SCADA `Hard braking` alarm (the leader's side only).
- **Pass:** the follower's alert starts < 200 ms after the leader's brake onset. Count the frames between the two events and write the figure in Notes.
- **Limitation:** the firmware does not log the follower's EEBL event, so the follower time comes from video. If you want an instrumented number, tell me and I will add an event line in LOG_MODE.

### A8 Crash, padded drop on foam, then still
- **Set-up:** OBU #1 (`--log`) in a padded box on foam. Laptop with SCADA open and the serial log running with the timestamped console (§2.1).
- **Procedure:** drop the box from the height that gives a_total above 4 g (check the CSV on a test drop first). Leave it still. Repeat 3 times, with a minute between drops.
- **Data:** `data/a8_run<N>.csv` and `data/a8_run<N>_console.txt`, SCADA DB around the drop.
- **Pass:** the `Crash / SOS` alarm appears on SCADA within 2 s of the impact (both timestamps from the laptop clock) and carries a valid position (lat/lon not 0).
- **Watch:** SOS fires only when the vehicle is detected as still within 3 s of the impact (`CRASH_CONFIRM_MS`), using GPS speed. If GPS speed drops slowly, the SOS can be late or missing. Report the measured delay, do not change the threshold without me.

### A9 Rollover, on its side for 2 s
- **Set-up:** OBU #1 (`--log`) on a padded surface.
- **Procedure:** lay it on one side and hold it for 3 s (the trigger is above 60° for 1 s). Return it upright. Repeat 3 times.
- **Data:** `data/a9_rollover_run<N>.csv`, SCADA alarms.
- **Pass:** the `Rollover` alarm appears on SCADA for each run, and the CSV roll or pitch shows the tilt above 60°.

### A10 Each SCADA command
- **Set-up:** OBU #1 normal build, OBU #2 gateway, SCADA running (use the Control dashboard) or the probe with SCADA stopped.
- **Procedure:** for each code 1-5, target vehicle 1, three times:
  - 1 Hazard alert, 2 Speed advisory, 3 Clear SOS, 4 Buzzer test, 5 Ping. Use the `param` values from §S11.4 (I will confirm them with you before the run).
  - Command line: `tools/.venv/Scripts/python.exe tools/modbus_probe.py --serial COM5 --command <CODE> <PARAM> 1`
  - Note the time between sending the command and the reaction (buzzer, LED or screen) with the phone's stopwatch.
- **Data:** `data/a10_commands.txt` (the probe readbacks: ACK counter before and after each command).
- **Pass:** every command reacts in < 1 s and the ACK counter rises by 1 each time.

### A11 Switch off OBU #1, link lost in < 2.5 s
- **Set-up:** both running, SCADA showing both vehicles, OBU #1 on the battery.
- **Procedure:** switch OBU #1 off. Film the switch and the SCADA alarms list in the same frame, or note the time on the laptop clock.
- **Data:** `data/a11_linkloss_console.txt` (or the video), SCADA DB (`V2V link lost` alarm, `ts`).
- **Pass:** the `V2V link lost` alarm appears < 2.5 s after the switch-off.

### A12 30 min normal use
- **Set-up:** OBU #1 `--log` (serial logger in vehicle 1, see §2.4) and OBU #2 gateway with SCADA in vehicle 2. Both with GPS fix.
- **Procedure:** drive normally for 30 min as in real use (stops, traffic, bumps). No deliberate hard braking, no crash tests. Note the start and stop times.
- **Data:** `data/a12_normal_30min.csv`, SCADA DB for the window.
- **Analysis:** `log_analyzer.py --db scada/historian/v2v.db --from <start> --to <end>` (false-alarm summary), and `log_analyzer.py --imu-log data/a12_normal_30min.csv`.
- **Pass:** 0 `Crash / SOS` and 0 `Rollover` alarms. Count the `Collision risk`, `Hard braking` and `Road hazard` alarms and review each one by hand: they are the FCW / EEBL / pothole false-alarm count.

### A13 Battery life ≥ 5 h
- **Set-up:** both OBUs on their batteries (18650 chain). Fully charge first. Normal operation (GPS on, BSMs at 5 Hz, IMU on). Say in Notes whether the boards are stationary or driven.
- **Procedure:** start the clock, leave both running until the cutoff (radio off below 3.2 V per cell). Stop the clock.
- **Data:** SCADA telemetry `battery` (%) for both vehicles; copy the DB at the end (`data/field_<date>.db`); note the start and stop times.
- **Pass:** runtime ≥ 5 h.

### A14 SIM_MODE indoor demo
- **Set-up:** build `--id 1 --sim` for OBU #1, and `--id 2 --gateway --sim` for OBU #2. Replace the placeholder coordinates first (`SIM_LAT0_PLACEHOLDER_E7` / `SIM_LON0_PLACEHOLDER_E7` in `config.h`); this is an open VERIFY item.
- **Procedure:** run the simulator, then SCADA with `V2V_SOURCE=GATEWAY`. Check Definition-of-Done items 2-6 (§S3): the map shows both vehicles moving, Caution then Critical on B, hard-brake alert, crash SOS with location, hazard alert reaching A.
- **Before you run it:** I will check `firmware/v2v_obu/sim.cpp` for which DoD items the simulator actually exercises (for example, whether it simulates a hard brake), so the test is not run against items the simulator cannot produce.
- **Data:** `data/a14_sim_notes.txt` with a checkmark per DoD item, and screenshots.
- **Pass:** DoD items 2-6 work.

---

## 4. §S13.3 threshold tuning (10 min of normal use, 60 % rule)

**Rule:** keep the default thresholds if the normal-use maxima stay below 60 % of each threshold. Otherwise a threshold may be raised, but only with evidence. Do not change `config.h` without showing me the plot and the table first.

Thresholds in the current `config.h`, in the CSV's units:

| Channel | Threshold | 60 % line |
|---|---|---|
| a_long (m/s²) | 3.90 (from `HARD_BRAKE_MS2` = −3.9) | 2.34 |
| a_total (g) | 4.00 (`CRASH_G`) | 2.40 |
| az_hp (m/s²) | 14.71 (`POTHOLE_G` 1.5 × 9.81) | 8.83 |
| roll / pitch (deg) | 60 (`ROLLOVER_DEG`) | 36 |

The analyzer reads these from `firmware/v2v_obu/config.h`; it does not hard-code them.

Procedure:
1. Flash `tools/flash.py --id 1 --log`. IMU calibration at boot (§S13.2).
2. Drive normally for 10 min (the same driving as A12: stops, traffic, bumps, no crash tests). Record it:
   `tools/.venv/Scripts/python.exe tools/serial_log.py COM7 600 --out data/normal_drive_1.csv`
3. Analyse: `tools/.venv/Scripts/python.exe tools/log_analyzer.py --imu-log data/normal_drive_1.csv`
4. Read the verdict column in `docs/results/summary.md`. Repeat for at least 2 drives if any channel is above 60 %.

Result table:

| Run | Duration | a_long max | a_total max | az_hp max | roll/pitch max | Verdict | Notes |
|---|---|---|---|---|---|---|---|
| T1 normal_drive_1 | | | | | | | |
| T2 normal_drive_2 | | | | | | | |

---

## 5. Results (blank: fill after each test)

PASS/FAIL is written after the analysis. Attach the file names in Notes.

| ID | Date | Result | Measured value | Notes | PASS/FAIL |
|---|---|---|---|---|---|
| A1 Packet exchange 5 min | | | | | |
| A2 Range (50 m steps) | | | | | |
| A3 Latency (100 PINGs) | | | | | |
| A4 PDR at 50 m, 1000 BSMs | | | | | |
| A5 Distance vs tape | | | | | |
| A6 FCW approach | | | | | |
| A7 EEBL (follower < 200 ms) | | | | | |
| A8 Crash on foam, SOS < 2 s | | | | | |
| A9 Rollover | | | | | |
| A10 Each SCADA command | | | | | |
| A11 Link lost < 2.5 s | | | | | |
| A12 30 min normal use | | | | | |
| A13 Battery life ≥ 5 h | | | | | |
| A14 SIM indoor demo (DoD 2-6) | | | | | |
| T1-T2 Threshold tuning (§S13.3) | | | | | |

---

## 6. Known limits of this plan

- **A7 needs video.** The EEBL event on the follower is not logged by the firmware.
- **A8 depends on GPS speed.** The SOS waits for a still vehicle within 3 s (`CRASH_CONFIRM_MS`). A late GPS speed means a late or missing SOS.
- **Stale readbacks in A3.** The gateway's last-RTT register is only updated when a PONG arrives. Check the ACK counter.
- **Two laptops.** Vehicle #1's debug CSV (Serial1) needs a second laptop in that vehicle. A serial logger in vehicle #2 is not enough for A7, A8 or A12.
- **Serial1 wiring** is not verified. Confirm before the first logging run.
- **SIM coordinates** are placeholders (see A14).
- **§S13.4 ranging** is L3 and is not in this plan.
