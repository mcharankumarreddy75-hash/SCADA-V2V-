# Viva questions and answers

Thirty likely examiner questions, with short answers based on this design. Constants are from
`firmware/v2v_obu/config.h` and the safety code (`safety.cpp`, `v2v_packet.cpp`, `neighbors.cpp`).

Rules for answering:
- If the question asks for a result (range, latency, battery life, crash delay), say what was **tested** and what was **measured**.
  Where the field tests are not done, say they are planned and give the design target **as a target**.
- Do not guess. If you are not sure, say so and say where the answer is recorded (`docs/test_plan.md`, `docs/verified.md`).
- Citations are marked `[CITATION NEEDED]`. Look up the source before you quote it.

---

## A. Radio and communication

### 1. Why a 2.4 GHz LoRa radio (SX1281) and not DSRC, C-V2X or a sub-GHz LoRa module?
DSRC (IEEE 802.11p, 5.9 GHz) and C-V2X need vehicle-grade radios and, in DSRC's case, a dedicated band
[CITATION NEEDED: IEEE 802.11p; 3GPP C-V2X]. The SX1281 module costs about ₹650 each, and the 2.4 GHz ISM band is
licence-free [CITATION NEEDED: Indian licence-free band rules]. It also has time-of-flight ranging, which
we keep as a future option. The trade-off: 2.4 GHz has higher path loss than 868 or 915 MHz LoRa, so range is shorter, and
the band is shared with Wi-Fi and Bluetooth. Our range is not measured yet (A2).

### 2. Why SF7, 812.5 kHz bandwidth, coding rate 4/5 and a 32-byte payload?
SF7 with 812.5 kHz is a fast LoRa setting (low spreading factor, wide bandwidth), so the packet is on air for a short time (about 12 ms, a specification
estimate that the firmware reads with `getTimeOnAir`). Short airtime keeps the channel load low. Coding rate 4/5 adds little
overhead. Every packet is 32 bytes, so the receiver checks the length first.

### 3. What is the channel load, and how many vehicles can share the channel?
Each vehicle sends about 4.5 BSMs per second. At about 12 ms each, that is about 5–6 % of airtime per vehicle. The
specification estimate is about 8–10 vehicles before the channel is crowded. This is a calculation, not a measurement;
the load is not measured yet.

### 4. What is the transmit power, and what is the range?
The firmware sets the chip output to 0 dBm (`RF_CHIP_POWER_DBM` 0). The module's amplifier and the maximum chip output are open
verification items in `docs/verified.md` (items 3 and 4). The range is not measured yet. The field test A2 measures the largest distance
with packet delivery ratio (PDR) of at least 90 %. **Design target: at least 200 m line of sight (target, not a result).**

### 5. Do you listen before you talk?
No. The specification lists listen-before-talk (`scanChannel`) as optional, and it is not implemented. The OBU sends its BSM
at a random interval (200 ms plus 0–40 ms), and event packets are repeated three times with random 20–50 ms gaps. We say
this openly as a limitation.

### 6. What is the BSM rate, and why is it not exactly 5 Hz?
The specification asks for 5 Hz. The firmware schedules the next BSM 200 ms plus a random 0–40 ms after the last one
(`BSM_PERIOD_MS` and `BSM_JITTER_MS`), so the mean interval is 220 ms, about 4.5 packets/s. The jitter spreads the transmissions
of two vehicles so that they do not repeatedly collide. The report says 4.5 packets/s, and the tests use that number.
DSRC BSMs are commonly quoted at 10 Hz [CITATION NEEDED].

### 7. How does the receiver know that a packet is valid?
`pkt_validate` checks in order: length 32 bytes; first byte 0xA5; CRC-16 matches; not the vehicle's own packet; sequence number
not a duplicate or older (per sender); freshness (GPS time no more than 500 ms old, when both sides have GPS and not in SIM); and
for commands, ACKs, pings and pongs, that the target is this vehicle. The first failure drops the packet.

---

## B. Packets, integrity and security

### 8. Why CRC-16/CCITT-FALSE, and what does it detect?
The packet ends with a 16-bit CRC over the first 30 bytes, polynomial 0x1021, initial value 0xFFFF. It detects bit errors that the
radio's own CRC does not catch, and it is cheap on a Cortex-M3. The unit test checks the standard vector
"123456789" → 0x29B1. A CRC is **not** a security measure: anyone can compute it.

### 9. What does the sequence number protect against?
Each sender numbers its packets with an 8-bit counter. The receiver drops a packet with the same or an older number
from the same sender, so duplicates from the three-copy event repeat are discarded. A silent sender for more than 5 s is treated as
rebooted and accepted again. The table holds eight senders.

### 10. Can a packet be replayed?
Partly. A replay of an old packet is dropped if its sequence number is not newer. A replay of a packet that is still
fresh (under 500 ms by GPS time) with a newer sequence number is accepted. The freshness check does not run if either side has no GPS.
Real replay protection needs authentication and a longer nonce or time window. This is future work.

### 11. Why is there no encryption or authentication?
It is outside the Level 1 features of the specification. The packets are open, so any radio with the same settings can read them. That is
the main security limitation. The future-scope section proposes message authentication and encryption, with the key handling
to be designed [CITATION NEEDED: lightweight authentication for vehicular messages].

### 12. How is the freshness check done, and why GPS time?
Each packet carries the sender's GPS time of day in milliseconds. The receiver compares it with its own GPS time, using modular
arithmetic across midnight, and drops the packet if it is more than 500 ms old (`PACKET_MAX_AGE_MS`). GPS time is the only
clock the two boards share without a network. Without a GPS fix on either side, the check is skipped.

---

## C. Collision warning and TTC

### 13. How is time-to-collision (TTC) computed?
For each neighbour that is ahead (bearing within ±20° of own heading) and moving in a similar direction (heading within ±30°),
the closing speed is `v_close = own speed − neighbour speed × cos(heading difference)`. TTC = distance / v_close, if v_close
is above 0.1 m/s. Otherwise TTC is infinite. The FCW ignores own speeds below 1 m/s (`MIN_SPEED_FCW`).

### 14. What is headway, and how is it used?
Headway is the time gap to the vehicle ahead: distance divided by own speed. A headway below 1.0 s (`HEADWAY_MIN_S`) gives a
Caution even when TTC is larger. This catches a short gap at a slow speed, where TTC can be long.

### 15. Why are the Caution and Critical thresholds 4 s and 2 s?
They are the design values in the specification (S9.3), and the A6 test checks them at 4 ± 0.5 s and 2 ± 0.5 s
(`TTC_CAUTION_S` 4.0 and `TTC_CRITICAL_S` 2.0). They are not yet validated by measurement. The basis should be cited from the
driving-safety literature [CITATION NEEDED: TTC thresholds and driver reaction time].

### 16. What is hysteresis in the FCW, and why is it there?
A higher level is shown at once. A lower level is shown only after it has been the highest level for 1 s (`WARN_HOLD_MS` 1000).
Without it, a TTC that moves around 2.0 s would make the alarm flicker between Critical and Caution, which confuses the driver.

### 17. What about vehicles beside you or coming towards you?
They are not warned about. The FCW gates on "ahead and same direction". An oncoming vehicle has a heading more than 30° away
from yours, and a vehicle beside you is outside the ±20° ahead gate. This is a limitation. The gate also stops warnings for
vehicles behind you. Those are handled by the EEBL alerts and the hazard alerts instead.

### 18. Why is the FCW limited to own speed of 1 m/s or more?
GPS course (heading) is not reliable at very low speed. Below 1 m/s the heading is held, not updated, and the TTC
calculation would use a wrong direction. Requiring 1 m/s avoids warnings from noise.

---

## D. Events: braking, crash, rollover, pothole

### 19. How is hard braking detected?
The longitudinal acceleration from the IMU is low-passed (factor 0.2). If it is below −3.9 m/s² (about −0.4 g) for 200 ms, and the
own speed is above 2 m/s (or there is no GPS), the EEBL event fires. A 2 s lock-out stops repeated events. The event is sent
three times with random 20–50 ms gaps (`EVENT_COPIES` 3).

### 20. How does the receiver decide whether an EEBL alert is for it?
`should_alert_eebl`: the sender must be ahead (bearing within ±45°) and within 300 m. With no GPS on either side, it alerts anyway,
which is the safer default.

### 21. How does crash detection work, and why does it need a stop?
When the total acceleration magnitude exceeds 4 g (`CRASH_G`), a 3 s window starts (`CRASH_CONFIRM_MS`). If the vehicle is
stopped in that window (speed below 1 m/s, or no GPS), the SOS fires. If it is not stopped, the event is discarded. This stops
an impact that is not followed by a stop (for example, a pothole taken at speed) from raising SOS. The cost is that a crash without a stop is missed, which is a limitation. The threshold
of 4 g and the 3 s window are design values; the A8 test (drop on foam) checks them.

### 22. What happens after an SOS?
The OBU repeats the CRASH packet three times, then once a second (`SOS_PERIOD_MS` 1000), and turns on the local SOS pattern.
The other vehicle gets the remote SOS pattern for 5 s. SCADA raises a CRITICAL Crash / SOS alarm with the position. The SOS is cancelled
by holding the button for 3 s, or by the Clear SOS command (code 3).

### 23. How is rollover detected?
Roll and pitch come from the complementary filter. If either is above 60° (`ROLLOVER_DEG`) for 1000 ms (`ROLLOVER_TIME_MS`), the rollover flag fires.
It re-arms when both angles are back within the limit.

### 24. How does pothole detection work, and why a high-pass filter?
Vertical acceleration minus a slow low-pass (factor 0.02, time constant about 0.5 s) gives the high-passed value, which removes gravity and slow tilt. A
value above 1.5 g (`POTHOLE_G` 1.5 × 9.81 m/s²) while moving faster than 3 m/s with GPS fires a hazard event, with a 3 s lock-out. The speed
gate stops a knock on the bench from being reported as a pothole.

---

## E. GPS and IMU

### 25. What are the GPS limitations?
The Neo-6M gives position to a few metres in open sky, and worse under buildings or trees. The planning figure in the specification is
±2.5–10 m; check the datasheet before quoting it [CITATION NEEDED: u-blox Neo-6M datasheet]. The firmware needs at least four satellites for a fix, and
drops the fix after 2 s without an update. The heading is only updated above 1 m/s. Cold start can take minutes. The receiver is set to 5 Hz at boot
(`GPS_RATE_MS` 200) by UBX commands, and GPS distance is checked in test A5.

### 26. What is the IMU doing, and why a complementary filter?
The MPU-6050 is sampled at 100 Hz with ±8 g and ±500°/s ranges and a digital low-pass of about 44 Hz. Roll and pitch come from the gyro (integrated) blended with the
accelerometer (gravity direction) with weights 0.98 and 0.02. It is cheap, fast and needs no matrix maths on the Cortex-M3. A Kalman filter would
estimate the gyro bias; that is future work.

### 27. Why is the IMU calibrated at every boot?
The MPU-6050 has offsets that change with temperature and mounting. The firmware keeps still for 2 s, averages 200 samples and subtracts the
offsets. If the board moves during boot, the offsets are wrong, so the rule is to reboot.

### 28. What can make the IMU give a false alarm?
Hard acceleration or braking can tilt the estimate, so roll and pitch can briefly look large. The rollover rule needs 1 s above 60°, which
rejects short spikes. A sharp pothole may also give a high vertical value. These are the cases the normal-driving test (A12) and the tuning
(§S13.3, threshold at 60 % of maximum) are meant to find.

---

## F. Gateway, Modbus and SCADA

### 29. Why Modbus RTU for the SCADA link?
Modbus is a simple, widely used master-slave protocol for industrial devices, with 16-bit registers and a CRC. Node-RED has
Modbus libraries, and the gateway can use a USB serial port with no extra hardware. The alternatives are Modbus TCP, MQTT and OPC UA [CITATION NEEDED: Modbus specification; SCADA protocols].

### 30. Why is the gateway on OBU #2? Is it a single point of failure?
It saves a board, and the gateway only reads data that the vehicle already receives. Yes, it is a single point of failure for the
SCADA view, but not for the safety: the vehicles still warn each other if the gateway fails. SCADA raises a "Gateway offline" CRITICAL alarm
when Modbus has failed for more than 2 s.

### 31. Which SCADA layers are in this design?
Level 0, field devices: GPS, IMU, buzzer, LEDs, button and battery sensing. Level 1, control: the OBUs and the V2V network. Level 2, communication:
Modbus RTU over USB. Level 3, supervision: the Node-RED dashboard, the alarm logic and the SQLite historian. The layers are shown in README §2.4.

### 32. How does the alarm logic work?
Rules are evaluated on each poll (every 250 ms). An alarm is raised on the rising edge and cleared when the condition is false. CRITICAL alarms (crash,
rollover, gateway offline) stay in the active list until someone acknowledges them. Link loss is raised when a vehicle's packet age is above 2 s or the
vehicle disappears; GPS loss after 5 s without a fix.

### 33. What is the historian, and why SQLite?
The historian is a time-stamped database. Every vehicle gets one telemetry row a second, and alarms go in their own table. SQLite is one file, needs no server,
and the CSV export is one click. A 1 Hz log of two vehicles is a small load, so SQLite is enough.

### 34. How are operator commands kept safe?
A command is a code (1 hazard alert, 2 speed advisory, 3 clear SOS, 4 buzzer test, 5 ping), a parameter and a target. None of them drives or brakes the vehicle; they only warn,
clear an SOS or test. Each command goes out three times, the target acknowledges it, and SCADA shows the acknowledgement counter. A command to a vehicle that does not answer is visible as no ACK increase.

### 35. What happens when the link is lost?
The vehicle drops a neighbour after 1.5 s without a packet (`NEIGHBOR_TIMEOUT_MS`). SCADA raises "V2V link lost" when the packet age is above 2 s. The
field test A11 measures the time from switch-off to the alarm; the target is below 2.5 s. The measured value is not yet known.

---

## G. Engineering, testing and limitations

### 36. Why does the code avoid dynamic memory, and how is the loop kept non-blocking?
The STM32F103 has 20 KB of RAM and no heap is used, so memory use is fixed and known. The main loop uses timed tasks (IMU and detectors every 10 ms, FCW every 50 ms,
gateway registers every 100 ms, once-a-second tasks). The only blocking calls are the radio transmission (about 12 ms) and the boot sequence. A 2 s
watchdog resets the board if the loop stops.

### 37. How did you test the logic without the hardware?
The portable logic (packet, CRC, geometry, neighbour table, safety rules, Modbus slave) is compiled with g++ on the PC and checked by 66 C++ unit tests
(290 checks). The Python tools have 26 unit tests. Both pass on the development PC. The firmware compiles for both builds. This is not a substitute for the field tests.

### 38. What did you actually test in the field?
Nothing yet. No hardware has been powered for a field test, so no range, latency, PDR, battery or warning-timing result is reported.
The results section has placeholders for A1–A14, and the test plan says how each one is measured. I will not quote a number I have not measured.

### 39. What is the simulation mode for?
It lets the SCADA and the radio be shown indoors. `gateway_sim.py` is a Modbus server with a scripted scenario (approach, braking, crash, link loss) and no hardware. The
SIM_MODE firmware replaces only the GPS, so the radio and the IMU run for real. The simulator does not show real sensing or real driving.

### 40. How does this compare with a commercial V2V system, and what are its limitations?
A commercial system uses dedicated spectrum or C-V2X, standard messages (SAE J2735 BSM), certified hardware, security certificates and tested
performance [CITATION NEEDED: SAE J2735; V2X security certificates]. This prototype is not J2735-compliant, not encrypted, not certified, and its warnings
depend on the accuracy of consumer GPS. Its strengths are cost, openness and a supervisory view, which the commercial systems do not show to a fleet operator in the same way.

---

## Quick numbers (from `config.h`, for the viva)

| Item | Value |
|---|---|
| Frequency, SF, BW, CR, preamble | 2445.0 MHz, SF7, 812.5 kHz, 4/5, 12 |
| Packet | 32 bytes, start 0xA5, CRC-16/CCITT-FALSE |
| BSM period | 200 ms + 0–40 ms (mean 220 ms, about 4.5/s) |
| Event copies | 3, gaps 20–50 ms |
| SOS repeat | 1000 ms |
| Neighbours / timeout | 3 / 1.5 s |
| Freshness | 500 ms (GPS time) |
| FCW gates | ahead ±20°, same direction ±30°, own speed ≥ 1 m/s |
| FCW levels | Caution TTC ≤ 4 s or headway < 1 s; Critical TTC ≤ 2 s; hold 1 s |
| EEBL | a_long < −3.9 m/s² for 200 ms, speed > 2 m/s, lock-out 2 s; receiver ±45°, 300 m |
| Crash | > 4 g, stopped within 3 s |
| Rollover | > 60° for 1 s |
| Pothole | > 1.5 g, speed > 3 m/s, GPS, lock-out 3 s; hazard receiver ±30°, 200 m |
| IMU | 100 Hz, ±8 g, ±500°/s, complementary 0.98, a_long α 0.2, az α 0.02 |
| GPS | 5 Hz, 38400 baud, ≥ 4 satellites, fix lost after 2 s |
| Battery | divider 2.0; full 4.20 V, low 3.40 V, empty 3.30 V, radio off 3.20 V |
| Modbus | RTU, 115200 8N1, slave 1, holding registers 0–109, commands 100–103 |
| SCADA | poll every 250 ms, timeout 500 ms; gateway offline after 2 s; GPS lost after 5 s |
