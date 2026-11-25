# Phase 2 bring-up tests

Seven independent sketches. Each one proves one part of the OBU works before the real firmware is written.
All output goes to USB CDC `Serial` at 115200 baud. Every line starts with `PASS:`, `FAIL:` or `INFO:`.
Pass criteria are from spec section S10.7. Pins match `config.h` (S10.2).

Compile and upload (run from the project root). Replace `<test>` with the folder name.

```bash
"/c/Program Files/Arduino CLI/arduino-cli.exe" compile --fqbn "$(cat tools/fqbn.txt)" firmware/bringup/<test>
"/c/Program Files/Arduino CLI/arduino-cli.exe" upload  --fqbn "$(cat tools/fqbn.txt)" firmware/bringup/<test>
```

| Test | Sketch | What to connect / do | Pass criterion (S10.7) |
|---|---|---|---|
| T1 Blink | `t1_blink` | Nothing extra. Upload via ST-Link (SWD). PC13 LED should blink at 1 Hz. | PC13 blinks; SWD upload works. Output: `PASS: running (SWD upload worked)` |
| T2 I/O | `t2_io` | Red LED PB14, yellow LED PB15, green LED PA8 (each 330 ohm to GND). Buzzer via BC547 base on PB8. Button PB9 to GND. Reset the board after upload to rerun the sequence. | 3 LEDs light in order, buzzer beeps twice, button press and release register. Output: `PASS: button press detected` |
| T3 IMU | `t3_imu` | MPU-6050: VCC 3.3 V, GND, SCL PB6, SDA PB7, AD0 to GND. Keep the board **flat and still** for 2 s. | WHO_AM_I = 0x68. Flat and still: az about +9.8 m/s2, ax and ay about 0. Output: `PASS: flat-still mean ...` |
| T4 Radio | `t4_radio` | Both OBUs, antennas fitted **before power**, boards 1 to 2 m apart. OBU #1 = NODE 1 (default). OBU #2 = NODE 2 (see below). | Init OK (`PASS: radio init`). Both boards show RX lines with RSSI and SNR. Node 1 prints `PASS: round trip ok`. |
| T5 GPS | `t5_gps` | Neo-6M: VCC 5 V, GND, GPS TX to PA3, GPS RX to PA2. **Go outdoors**, antenna facing the sky. | NMEA valid. Fix in under 60 s (a cold start can take up to 5 min). Output: `PASS: fix after N s` |
| T6 USB | `t6_usb` | Blue Pill USB to laptop. Open the COM port and type a few characters. | Laptop sees a COM port. `PASS: USB CDC alive n` every second, and typed characters are echoed. |
| T7 Battery | `t7_battery` | Divider: battery+ to 100k to PA0, 100k to GND. Measure the battery with a multimeter at the same time. | Vbat within +/-0.05 V of the multimeter. |

### T4 for NODE 2

The sketch defaults to NODE 1. Build the NODE 2 image without editing the file:

```bash
"/c/Program Files/Arduino CLI/arduino-cli.exe" compile --fqbn "$(cat tools/fqbn.txt)" --build-property "build.extra_flags=-DNODE=2" firmware/bringup/t4_radio
"/c/Program Files/Arduino CLI/arduino-cli.exe" upload  --fqbn "$(cat tools/fqbn.txt)" firmware/bringup/t4_radio
```

Upload NODE 1 to OBU #1 and NODE 2 to OBU #2. Use the same command for upload, but compile NODE 2 first so the right image is uploaded.

### Reading the output

```bash
tools/.venv/Scripts/python.exe tools/serial_log.py <PORT> <SECONDS>
```
