"""Unit tests for tools/v2v_modbus.py (register encoding / decoding, S11 and S12.4)."""

import math
import os
import sys
import unittest

TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import v2v_modbus as m  # noqa: E402


def vehicle(**overrides):
    v = {
        "id": 7, "lat": 17.385, "lon": 78.4867, "speed_cms": 800, "heading_cdeg": 9000,
        "accel_cms2": -450, "roll": -3, "pitch": 2, "flags": m.FLAG_GPS_VALID | m.FLAG_HAZARD,
        "battery": 88, "rssi": -72, "snr_cdb": 65, "pdr_permille": 987,
        "distance_dm": 315, "ttc_ds": 39, "warn": 1, "gps_quality": (8 << 4) | 1, "age_ms": 120,
    }
    v.update(overrides)
    return v


def place(slots):
    """Build a 110-register image from {slot_index: encoded_slot_list}."""
    regs = [0] * m.NUM_REGS
    regs[m.GATEWAY_ID] = 2
    regs[m.FW_VERSION] = 100
    for slot, sr in slots.items():
        base = m.SLOT_BASE + m.SLOT_SIZE * slot
        regs[base:base + m.SLOT_SIZE] = sr
    return regs


class SignedHelpers(unittest.TestCase):
    def test_negative_one_round_trip(self):
        self.assertEqual(m.to_u16(-1), 65535)
        self.assertEqual(m.s16(65535), -1)
        self.assertEqual(m.s16(m.to_u16(-1)), -1)

    def test_min_int16_round_trip(self):
        self.assertEqual(m.to_u16(-32768), 32768)
        self.assertEqual(m.s16(32768), -32768)

    def test_s16_range_round_trip(self):
        for v in (-32768, -1000, -1, 0, 1, 1000, 32767):
            self.assertEqual(m.s16(m.to_u16(v)), v)

    def test_s16_positive_unchanged(self):
        self.assertEqual(m.s16(32767), 32767)
        self.assertEqual(m.s16(0), 0)


class Int32Coordinates(unittest.TestCase):
    def test_negative_lat_high_word_has_sign_bit(self):
        lat = round(-33.8688197 * 1e7)
        hi, lo = m.split_s32(lat)
        self.assertGreaterEqual(hi, 0x8000)
        self.assertEqual(m.join_s32(hi, lo), lat)
        self.assertAlmostEqual(m.join_s32(hi, lo) / 1e7, -33.8688197, places=7)

    def test_positive_lon_round_trip(self):
        lon = round(151.2092955 * 1e7)
        hi, lo = m.split_s32(lon)
        self.assertLess(hi, 0x8000)
        self.assertEqual(m.join_s32(hi, lo), lon)

    def test_small_positive_lat(self):
        lat = round(17.385 * 1e7)
        hi, lo = m.split_s32(lat)
        self.assertEqual(m.join_s32(hi, lo) / 1e7, 17.385)

    def test_negative_lon_high_word(self):
        lon = round(-122.4194155 * 1e7)
        hi, lo = m.split_s32(lon)
        self.assertGreaterEqual(hi, 0x8000)
        self.assertEqual(m.join_s32(hi, lo), lon)

    def test_high_word_first(self):
        hi, lo = m.split_s32(0x12345678)
        self.assertEqual((hi, lo), (0x1234, 0x5678))
        self.assertEqual(m.join_s32(0x1234, 0x5678), 0x12345678)


class NoneSentinel(unittest.TestCase):
    def test_distance_and_ttc_none_encode_to_65535(self):
        regs = m.encode_slot(vehicle(distance_dm=None, ttc_ds=None))
        self.assertEqual(regs[m.OFF_DISTANCE], 65535)
        self.assertEqual(regs[m.OFF_TTC], 65535)

    def test_distance_and_ttc_none_decode_to_none(self):
        regs = place({0: m.encode_slot(vehicle(distance_dm=None, ttc_ds=None))})
        v = m.decode(regs)["vehicles"][0]
        self.assertIsNone(v["distance"])
        self.assertIsNone(v["ttc"])

    def test_values_present_decode_scaled(self):
        regs = place({1: m.encode_slot(vehicle(distance_dm=315, ttc_ds=39))})
        v = m.decode(regs)["vehicles"][0]
        self.assertAlmostEqual(v["distance"], 31.5)
        self.assertAlmostEqual(v["ttc"], 3.9)


class FlagDecoding(unittest.TestCase):
    def test_all_bits_set(self):
        regs = place({0: m.encode_slot(vehicle(flags=0xFF))})
        v = m.decode(regs)["vehicles"][0]
        self.assertTrue(v["hardBrake"])
        self.assertTrue(v["crash"])
        self.assertTrue(v["rollover"])
        self.assertTrue(v["hazard"])
        self.assertTrue(v["gpsValid"])
        self.assertTrue(v["lowBattery"])
        self.assertEqual(v["flags"], 0xFF)

    def test_individual_bits(self):
        cases = {
            m.FLAG_HARD_BRAKE: "hardBrake", m.FLAG_CRASH: "crash", m.FLAG_ROLLOVER: "rollover",
            m.FLAG_HAZARD: "hazard", m.FLAG_GPS_VALID: "gpsValid", m.FLAG_LOW_BATTERY: "lowBattery",
        }
        for bit, name in cases.items():
            regs = place({0: m.encode_slot(vehicle(flags=bit))})
            v = m.decode(regs)["vehicles"][0]
            self.assertTrue(v[name], name)
            for other in cases.values():
                if other != name:
                    self.assertFalse(v[other], f"{other} set by bit for {name}")

    def test_warn_bits_not_in_named_flags(self):
        regs = place({0: m.encode_slot(vehicle(flags=m.FLAG_WARN_CRITICAL, warn=2))})
        v = m.decode(regs)["vehicles"][0]
        self.assertEqual(v["warn"], 2)
        self.assertFalse(v["hardBrake"] or v["crash"] or v["hazard"])


class SlotRoundTrip(unittest.TestCase):
    def test_full_slot_round_trip(self):
        src = vehicle()
        regs = place({1: m.encode_slot(src)})
        out = m.decode(regs)
        self.assertEqual(out["gateway"]["gatewayId"], 2)
        self.assertEqual(len(out["vehicles"]), 1)
        v = out["vehicles"][0]
        self.assertEqual(v["slot"], 1)
        self.assertEqual(v["id"], 7)
        self.assertAlmostEqual(v["lat"], 17.385, places=7)
        self.assertAlmostEqual(v["lon"], 78.4867, places=7)
        self.assertAlmostEqual(v["speedKmh"], 8.0 * 3.6)
        self.assertAlmostEqual(v["heading"], 90.0)
        self.assertAlmostEqual(v["accel"], -4.5)
        self.assertEqual(v["roll"], -3)
        self.assertEqual(v["pitch"], 2)
        self.assertEqual(v["battery"], 88)
        self.assertEqual(v["rssi"], -72)
        self.assertAlmostEqual(v["snr"], 6.5)
        self.assertAlmostEqual(v["pdr"], 98.7)
        self.assertAlmostEqual(v["distance"], 31.5)
        self.assertAlmostEqual(v["ttc"], 3.9)
        self.assertEqual(v["warn"], 1)
        self.assertEqual(v["sats"], 8)
        self.assertEqual(v["hdop"], 1)
        self.assertEqual(v["ageMs"], 120)
        self.assertTrue(v["hazard"])
        self.assertTrue(v["gpsValid"])

    def test_negative_lat_lon_slot_round_trip(self):
        regs = place({0: m.encode_slot(vehicle(lat=-33.8688197, lon=-122.4194155))})
        v = m.decode(regs)["vehicles"][0]
        self.assertAlmostEqual(v["lat"], -33.8688197, places=7)
        self.assertAlmostEqual(v["lon"], -122.4194155, places=7)

    def test_empty_slot_skipped(self):
        regs = place({0: m.encode_slot(vehicle(id=3)), 2: m.encode_slot(vehicle(id=0))})
        # slot 2 encoded with id 0 must not be reported; slot 1 is all zero (empty)
        out = m.decode(regs)
        self.assertEqual([v["slot"] for v in out["vehicles"]], [0])

    def test_zero_slots_skipped(self):
        out = m.decode([0] * m.NUM_REGS)
        self.assertEqual(out["vehicles"], [])
        self.assertEqual(out["gateway"]["fw"], 0)

    def test_gateway_fields(self):
        regs = [0] * m.NUM_REGS
        regs[m.FW_VERSION] = 100
        regs[m.STATUS] = 0x0F
        regs[m.NEIGHBOR_COUNT] = 2
        regs[m.LAST_RTT_MS] = 27
        regs[m.CMD_ACK_COUNT] = 5
        regs[m.RX_TOTAL] = 1234
        regs[m.TX_TOTAL] = 1100
        regs[m.CRC_ERRORS] = 3
        regs[m.GATEWAY_ID] = 2
        regs[m.UPTIME_S] = 600
        g = m.decode(regs)["gateway"]
        self.assertEqual(g, {
            "fw": 100, "status": 0x0F, "neighbors": 2, "rttMs": 27, "acks": 5,
            "rx": 1234, "tx": 1100, "crcErr": 3, "gatewayId": 2, "uptime": 600,
        })

    def test_none_slot_encoding_and_negative_values_clamped(self):
        regs = m.encode_slot(vehicle(accel_cms2=-40000, roll=-100, rssi=-120))
        self.assertEqual(m.s16(regs[m.OFF_ACCEL]), -32768)
        self.assertEqual(m.s16(regs[m.OFF_ROLL]), -100)
        self.assertEqual(m.s16(regs[m.OFF_RSSI]), -120)

    def test_slot_length(self):
        self.assertEqual(len(m.encode_slot(vehicle())), m.SLOT_SIZE)

    def test_decode_rejects_short_input(self):
        with self.assertRaises(ValueError):
            m.decode([0] * 10)


if __name__ == "__main__":
    unittest.main()
