# Demo script: 10-minute viva demonstration

Two versions. Use the **outdoor** version if the hardware is ready and the GPS has a fix. Otherwise use the **indoor backup**,
which runs the same SCADA and the same radio code without a GPS sky view.

> **Status:** nothing in this script has been rehearsed yet. The outdoor steps depend on hardware that has not been powered
> (see README §10). Mark each step ✔ in the rehearsal log at the end of this file, with the date. Do not claim a step worked until you have seen it work.

Times are targets for a 10-minute slot. Keep a stopwatch running.

---

## 0. Pre-demo checklist (complete the day before, and again 30 minutes before)

### 0.1 Hardware (outdoor version)

- [ ] Both 18650 cells charged on the TP4056. Check the charge indicator as described in your module's documentation (it differs between modules; confirm with the seller page). Note the battery percentage on SCADA for each vehicle.
- [ ] MT3608 output set to 7.0 V with a multimeter, checked before connecting the boards.
- [ ] Both 2.4 GHz antennas fitted to the E28 modules **before** power is applied. Never transmit without an antenna.
- [ ] Both boards: no loose wires, GPS antenna facing the sky, MPU-6050 secured flat to the chassis.
- [ ] Both boards: flashed with the correct build (see 0.2). Firmware version: `FW_VERSION 100` (v1.00).
- [ ] USB cable from the gateway (OBU #2) to the laptop. USB-TTL adapter wired to the debug UART if you want the log.
- [ ] Spare battery, spare USB cable, spare antenna, small screwdriver, tape measure.

### 0.2 Firmware versions and build identities

Run from the repository root (Git Bash). Use the commands exactly as written in README §6.

| Board | Build | Vehicle ID | Check |
|---|---|---|---|
| OBU #1 (vehicle) | `flash.py --id 1` | 1 | `tools/.venv/Scripts/python.exe tools/serial_log.py <COM> 5` shows the 1 Hz debug line (`S=`, `pos=`, `lvl=`, `bat=`). The vehicle ID is set by the build flag, so the build command is the record. |
| OBU #2 (vehicle + gateway) | `flash.py --id 2 --gateway` | 2 | SCADA `/v2v/status` shows `"gatewayId":2` and `"fw":100` |

- [ ] Record the flash date and time of each board in the rehearsal log.
- [ ] Compile check before leaving: `tools/.venv/Scripts/python.exe tools/flash.py --id 1 --compile-only` and `tools/.venv/Scripts/python.exe tools/flash.py --id 2 --gateway --compile-only` (both succeeded on 2026-11-25).
- [ ] Host tests: `tools/.venv/Scripts/python.exe tests/host/run_tests.py` (passed on 2026-11-25).

### 0.3 Laptop and SCADA

- [ ] Find the COM port of the gateway: `tools/.venv/Scripts/python.exe tools/serial_log.py --list`. Write it here: COM ____.
- [ ] Check that no other program uses that COM port (SCADA with GATEWAY source is the only user during the demo).
- [ ] Historian cleared or moved aside, so that the demo's rows are easy to find. Move the old file: `move scada\historian\v2v.db scada\historian\v2v_before_demo.db` (only when SCADA is stopped). Note the time of the demo start.
- [ ] Node-RED started with the right source:
  - Outdoor: `set V2V_SOURCE=GATEWAY` and `set V2V_PORT=COM<n>` then `scada\start_scada.bat`.
  - Indoor backup: `scada\start_scada.bat` (source SIM, Modbus TCP 127.0.0.1:5020), with the simulator running (see §4).
- [ ] Browser tabs open (in this order, one window):
  1. http://localhost:1880/dashboard/map
  2. http://localhost:1880/dashboard/link
  3. http://localhost:1880/dashboard/alarms
  4. http://localhost:1880/dashboard/control
  5. http://localhost:1880/v2v/status (JSON, for the examiner if asked)
  6. Handout: `docs/viva_questions.md` (for you, not the examiner)
- [ ] Phone stopwatch ready. Phone camera ready for the EEBL frame count (optional).
- [ ] Printed copy of §5 (the fallback plan) and of the README §10 status table.
- [ ] Speakers or buzzer audible from where the examiner stands.

### 0.4 Before each run

- [ ] Both boards on their batteries, GPS fix shown on SCADA (`gpsValid: true` for both vehicles; `sats` 4 or more).
- [ ] IMU calibrated: keep both boards still and level for 2 s after power-up. If a board moved during boot, reboot it.
- [ ] Vehicles about 10 m apart, stationary, on open ground, for the first screen.

---

## 1. Introduction (0:00–1:00)

**Say:** "This is a low-cost vehicle-to-vehicle safety prototype. Two vehicles exchange status messages over a 2.4 GHz LoRa radio. Each vehicle warns its driver of collisions, hard braking, crashes and rollovers. The second vehicle also reports to a SCADA station, which shows the vehicles on a map and logs alarms. It is an educational prototype, not a certified safety system."

**Show:** the map page (dashboard/map). Both vehicle markers should be visible. Point at the gateway marker (V2) and the neighbour (V1).

---

## 2. Live status (1:00–2:30)

**Show:** dashboard/link.

- Distance between the two vehicles (metres, from GPS).
- TTC (shows "none" when the vehicles are not closing).
- PDR (packet delivery ratio) and RSSI (signal strength) for the neighbour.
- Gateway counters: rx, tx, CRC errors. Point out the CRC error counter.

**Say:** "Each vehicle sends a status packet about every 220 ms on average. The packets carry a CRC-16 checksum, and each is checked before use. The gateway counts the packets it accepted and the ones it rejected."

---

## 3. Forward collision warning (2:30–4:30)

**Do:** Move vehicle 2 (gateway) towards vehicle 1, straight ahead, on the same heading, at about 15 km/h (the A6 speed; use the RC chassis or a trolley on a straight, private, open road). The FCW ignores own speeds below 1 m/s, and GPS heading is not reliable at walking pace, so a slow walk may show no TTC.

- At about TTC 4 s (watch the TTC value on SCADA), the **Caution** warning should sound on vehicle 2: a 100 ms beep every 500 ms, and the yellow LED on.
- At about TTC 2 s, **Critical**: the buzzer on continuously and the red LED on. SCADA shows warning level 2 and the Collision risk alarm (HIGH).
- Stop vehicle 2 about 2 m short. The level only drops after it has stayed low for 1 s (hysteresis), so the alert clears a moment after the stop.

**Say:** "Time-to-collision is the distance divided by the closing speed. Caution at 4 seconds, Critical at 2 seconds. The level only drops after it has been low for one second, so it does not flicker."

**Note the TTC at each level on the sheet below.** The actual values are what you report, not the targets:
- Caution TTC observed: ______ s    Critical TTC observed: ______ s

> **Rehearsal caution:** this approach is the A6 test. Run it on a private road or car park, never on a public road. If you cannot reach 15 km/h safely, say so and show the approach at the speed you used.

---

## 4. Backup indoor version (use if the boards or the GPS are not ready)

Use this version if the boards do not power up, the GPS has no fix, or the sky view is blocked. It can also be used
before the outdoor demo, to check the software chain.

### 4.1 SCADA and simulator only (no boards)

1. Terminal 1 (repository root): `tools/.venv/Scripts/python.exe tools/gateway_sim.py --speedup 10`
   - The simulator plays a scripted scenario: vehicle 2 approaches vehicle 1 at 8 m/s, brakes to a stop 3 m behind it, and repeats. Every 90 s it raises a hazard; every 120 s it raises a crash on vehicle 1 (auto-cleared after 30 s); every 180 s it drops vehicle 1's link for 5 s.
2. Terminal 2: `scada\start_scada.bat`. Wait for "Server now running" (about 20–30 s).
3. Browser: dashboard/map, then dashboard/alarms.

**Say:** "This is the same SCADA and the same register map as the real gateway. The vehicles come from a simulator, so this shows the software chain."

### 4.2 Two boards in simulation mode (SIM_MODE)

Use this if you want two real radios in the demo but no GPS:

1. Compile and flash: `tools/.venv/Scripts/python.exe tools/flash.py --id 1 --sim` (vehicle 1, stationary) and `tools/.venv/Scripts/python.exe tools/flash.py --id 2 --gateway --sim` (vehicle 2, approaches).
2. Before flashing: replace the placeholder coordinates in `firmware/v2v_obu/config.h` (`SIM_LAT0_PLACEHOLDER_E7` and `SIM_LON0_PLACEHOLDER_E7`) with the college coordinates × 10⁷, or accept the placeholders and say so.
3. Start SCADA with `V2V_SOURCE=GATEWAY` and the gateway's COM port.
4. Vehicle 2 approaches vehicle 1 in a loop (about 14 s approach, 10 s wait). Caution and Critical appear on vehicle 2 and on SCADA.

**Limit to state:** SIM_MODE replaces only the GPS. Radio, IMU, alerts and SCADA are real. The approach and crash logic
of the simulator belong to `tools/gateway_sim.py`, not to the firmware. Do not claim that the firmware SIM build runs the crash scenario unless you have tested it.

### 4.3 What the indoor version can show, and what it cannot

| Feature | Indoor SIM with two boards | Indoor, simulator only | Outdoor, real GPS |
|---|---|---|---|
| Radio packets, CRC, counters | Yes | No (simulated) | Yes |
| FCW Caution and Critical | Yes (vehicle 2 approaches in the simulated GPS) | Yes (scripted approach) | Yes |
| Hard braking alert | Not practical: needs a real deceleration above 0.4 g held for 200 ms | Yes (scripted) | Yes, on a private road |
| Crash SOS | Yes on vehicle 1 (stationary in SIM): drop it onto foam and keep it still | Yes (scripted every 120 s) | Yes, with the drop test |
| Rollover | Yes, by laying the board on its side for more than 1 s | No | Yes |
| Pothole / hazard | Not practical: needs real vertical acceleration above 1.5 g while the simulated speed is above 3 m/s | No | Yes, on a private road |
| SCADA map, alarms, history, commands | Yes | Yes | Yes |
| Real distance and GPS accuracy | No | No | Yes |

---

## 5. Outdoor demonstration (4:30–8:30)

Run this part only if the GPS shows a fix on both vehicles.

### 5.1 Crash and SOS (about 1 min)

**Do:** drop vehicle 2 gently on a padded surface, or tap it hard enough to exceed 4 g and then leave it still.

- Expected on vehicle 2: SOS (fast red LED, buzzer pattern), the CRASH packet is sent three times and then every 1 s.
- Expected on vehicle 1: the remote SOS pattern (red LED, three short beeps each second).
- Expected on SCADA: **Crash / SOS** alarm (CRITICAL) with the position on the map (red marker).
- Clear the SOS: hold the button on the affected board for 3 s, or send Clear SOS (code 3) from dashboard/control.

**Say:** "The crash detector needs a stop within three seconds of the impact. A crash where the car keeps moving is not detected. This is one of the limitations."

**Record:** delay from impact to SCADA alarm: ______ s (stopwatch or the alarm timestamp).

### 5.2 Hard braking (about 30 s)

**Do:** on a private road only, drive vehicle 1 forward above 2 m/s (about 7 km/h), then brake hard, with a deceleration above 0.4 g held for 200 ms. Vehicle 2 must be behind it. The firmware needs its own speed above 2 m/s (or no GPS) for the braking trigger.

- Expected on vehicle 2: the EEBL pattern within the time you record.
- Expected on SCADA: Hard braking alarm (MEDIUM) for the braking vehicle.

**Record:** time to alert on vehicle 2 (video frame count if you film it): ______ ms. The target is below 200 ms; this is not yet measured.

### 5.3 Pothole and hazard (about 30 s)

**Do:** drive over a speed bump or a rough patch at more than 3 m/s (about 11 km/h) on a private road, with GPS valid. A bench knock does not count: the pothole logic ignores it unless the vehicle is moving. Only if you are sure it is safe.

- Expected: a Road hazard alarm (MEDIUM) on SCADA with a yellow marker. The hazard alert reaches the other vehicle if it is within 200 m and ahead (±30° of its heading), with GPS on both sides.
- If nothing happens on the day, say that the pothole test is planned and show the threshold in `config.h` (`POTHOLE_G` 1.5).

### 5.4 Commands (about 1 min)

**Do:** on dashboard/control, press **Buzzer test (4)** targeting vehicle 1. Then press **Hazard alert (1)**.

- Expected: vehicle 1 buzzes, and the ACK counter on dashboard/link goes up by 1 for each command.

**Say:** "Commands go from the SCADA to the gateway by Modbus write, then over the radio, and the target sends an acknowledgement."

### 5.5 Link loss (about 30 s)

**Do:** switch off vehicle 1 (the on/off switch on the battery board).

- Expected: **V2V link lost** alarm (MEDIUM) on SCADA. Record the time from the switch-off to the alarm: ______ s. The target is below 2.5 s; not yet measured.
- Switch vehicle 1 back on, wait for the GPS fix, and show the alarm cleared on the alarm page.

---

## 6. Closing and questions (8:30–10:00)

**Show:** dashboard/alarms (the history of the run), and the export button (CSV) if asked for data.

**Say:** "This is what was built and tested in software. The field tests are in the test plan. The results so far are: [state only the results you have measured, using the values written on the rehearsal sheet]. The main limitations are: the crash detector needs a stop, the packets are not encrypted, and the GPS accuracy limits the collision warnings."

**Be ready for:** the viva questions in `docs/viva_questions.md`.

---

## 7. Fallback plan if something fails

| Problem | Quick action |
|---|---|
| A board does not boot or the radio fails | The red LED blinks at 5 Hz when the radio fails at boot. Switch to the indoor backup (§4) and say so. |
| No GPS fix after 5 min outdoors | Use the SIM build (§4.2) or the simulator (§4.1). State that the GPS has no fix. |
| SCADA shows no data from the gateway | Check the COM port (`serial_log.py --list`), that the source is GATEWAY, and that no other program has the port open. Restart SCADA. |
| SCADA page does not load | Wait 30 s after the banner. If it still fails, check that port 1880 is free, then restart. |
| A command does not reach the vehicle | Repeat once. Show the ACK counter. Explain that the ACK counter is the evidence; if it does not change, the link is down. |
| Battery low on a board | Radio stops below 3.2 V. Charge it, or use the indoor backup. |
| Examiner asks for a number you do not have | Say that the test is planned, show the test plan, and state the design target as a target. Do not invent a measured value. |

---

## 8. Rehearsal log (fill in during rehearsal)

| Date | Version (indoor / outdoor) | Step | Worked? (✔ / ✘) | Observed value | Notes |
|---|---|---|---|---|---|
| | | §0 checklist | | | |
| | | §1 introduction and map | | | |
| | | §2 live status | | | |
| | | §3 FCW Caution / Critical | | TTC ___ s / ___ s | |
| | | §4 indoor backup (simulator) | | | |
| | | §4.2 SIM boards | | | |
| | | §5.1 crash SOS | | delay ___ s | |
| | | §5.2 hard braking | | ___ ms | |
| | | §5.3 hazard | | | |
| | | §5.4 commands | | ACK +___ | |
| | | §5.5 link lost | | ___ s | |
| | | Timing (total run) | | ___ min | |

The v1.0 git tag is created only after a full rehearsal indoors and a full rehearsal outdoors are recorded here.
