# Verified items (§S17)

Record answers here. Claude Code reads this at the start of every session.

| # | Item | Answer | Status |
|---|---|---|---|
| 1 | E28-2G4M20SX pin names/order | | ⏳ check datasheet/silkscreen |
| 2 | TXEN/RXEN driven externally? | | ⏳ check datasheet |
| 3 | Max SX1281 chip power before PA; peak TX current | | ⏳ check datasheet (RF_CHIP_POWER_DBM stays 0 until then) |
| 4 | Antenna connector type; antennas included? | | ⏳ check module |
| 5 | TP4056 with protection (OUT+/OUT−)? | | ⏳ check module |
| 6 | Blue Pill genuine/clone; **flash size**; R10 value | Firmware needs >64 KB. tools/fqbn.txt set to BLUEPILL_F103CB (128 KB) on 2026-11-25 | ⚠️ MUST VERIFY: `STM32_Programmer_CLI -c port=SWD` → check "Flash size". If only 64 KB, tell Claude Code (fallback: opt=oslto + feature trimming) |
| 7 | RadioLib begin() order + CR convention | RadioLib 8.0.0: `begin(const ConfigLoRa_t&)` (freq/bw in Hz); codingRate 5 = 4/5; IRQ setter `setDio1Action`; `setRfSwitchPins(rxEn, txEn)`; `getTimeOnAir()` in µs; CRC error = `RADIOLIB_ERR_CRC_MISMATCH` | ✅ checked in source (Phase 2/4) |
| 8 | Neo-6M accepts 5 V on VCC | | ⏳ check module/seller page |

## Toolchain notes
- STM32duino core 3.1.0: `HardwareSerial` has no (rx, tx) pin constructor → use `Uart GPSSerial(PIN_GPS_RX, PIN_GPS_TX);`.
- RadioLib prints a harmless `#warning` about USB CDC debug output on every build.
- node-red-contrib-modbus could not be installed (dependency only on a third-party tarball host, blocked by npm 12); SCADA uses `modbus-serial` in function nodes instead.
