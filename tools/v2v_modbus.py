"""Shared Modbus register map and encode/decode helpers for the V2V gateway.

Pure logic, no I/O. Register map and decoding follow docs/V2V_BUILD_GUIDE.md
section S11 (registers) and S12.4 (decoder). Do not change the map here
without changing the spec first.
"""

# --- Gateway registers 0-9 (S11.2) -------------------------------------------
FW_VERSION = 0          # 100 = v1.00
STATUS = 1              # bit0 radio OK, bit1 GPS fix, bit2 IMU OK, bit3 SIM_MODE
NEIGHBOR_COUNT = 2
LAST_RTT_MS = 3
CMD_ACK_COUNT = 4
RX_TOTAL = 5            # low 16 bits
TX_TOTAL = 6            # low 16 bits
CRC_ERRORS = 7
GATEWAY_ID = 8
UPTIME_S = 9            # low 16 bits

# --- Vehicle slots (S11.3) ---------------------------------------------------
SLOT_BASE = 10          # base = SLOT_BASE + SLOT_SIZE * slot
SLOT_SIZE = 20
NUM_SLOTS = 4           # slot 0 = gateway vehicle, slots 1-3 = neighbours

# Offsets inside a slot
OFF_ID = 0
OFF_LAT_HI = 1
OFF_LAT_LO = 2
OFF_LON_HI = 3
OFF_LON_LO = 4
OFF_SPEED = 5           # u16 cm/s
OFF_HEADING = 6         # u16 0.01 deg
OFF_ACCEL = 7           # s16 cm/s^2
OFF_ROLL = 8            # s16 deg
OFF_PITCH = 9           # s16 deg
OFF_FLAGS = 10          # u16, S8.3
OFF_BATTERY = 11        # u16 %
OFF_RSSI = 12           # s16 dBm
OFF_SNR = 13            # s16 0.1 dB
OFF_PDR = 14            # u16 0.1 %
OFF_DISTANCE = 15       # u16 0.1 m
OFF_TTC = 16            # u16 0.1 s
OFF_WARN = 17           # u16 0/1/2
OFF_GPS_QUALITY = 18    # u16, (sats << 4) | hdop
OFF_AGE_MS = 19         # u16 ms

# --- Commands 100-103 (S11.4) ------------------------------------------------
CMD_CODE = 100
CMD_PARAM = 101
CMD_TARGET = 102
CMD_TRIGGER = 103

HAZARD_ALERT = 1
SPEED_ADVISORY = 2
CLEAR_SOS = 3
BUZZER_TEST = 4
PING = 5

NUM_REGS = 110          # holding registers 0-109
NONE = 65535           # "no value" for distance / ttc / etc.

# --- Flag bits (S8.3) --------------------------------------------------------
FLAG_HARD_BRAKE = 1 << 0
FLAG_CRASH = 1 << 1
FLAG_ROLLOVER = 1 << 2
FLAG_HAZARD = 1 << 3
FLAG_WARN_CAUTION = 1 << 4
FLAG_WARN_CRITICAL = 1 << 5
FLAG_GPS_VALID = 1 << 6
FLAG_LOW_BATTERY = 1 << 7

# --- Status bits (register 1) ------------------------------------------------
STATUS_RADIO_OK = 1 << 0
STATUS_GPS_FIX = 1 << 1
STATUS_IMU_OK = 1 << 2
STATUS_SIM_MODE = 1 << 3


# --- Integer helpers ---------------------------------------------------------
def to_u16(v):
    """Encode a (possibly negative) integer as an unsigned 16-bit register value."""
    return int(v) & 0xFFFF


def s16(v):
    """Interpret an unsigned 16-bit register value as signed."""
    v = int(v) & 0xFFFF
    return v - 0x10000 if v >= 0x8000 else v


def split_s32(v):
    """Split a signed 32-bit integer into (hi, lo) unsigned 16-bit words, high word first."""
    u = int(v) & 0xFFFFFFFF
    return (u >> 16) & 0xFFFF, u & 0xFFFF


def join_s32(hi, lo):
    """Join two unsigned 16-bit words (high word first) into a signed 32-bit integer."""
    u = ((int(hi) & 0xFFFF) << 16) | (int(lo) & 0xFFFF)
    return u - 0x100000000 if u >= 0x80000000 else u


def _clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def _u16c(v):
    """Clamp to unsigned 16-bit range, None -> NONE."""
    if v is None:
        return NONE
    return _clamp(int(round(v)), 0, 65535)


def _s16c(v):
    """Clamp to signed 16-bit range and encode, None -> NONE."""
    if v is None:
        return NONE
    return to_u16(_clamp(int(round(v)), -32768, 32767))


# --- Encoding ----------------------------------------------------------------
def encode_slot(vehicle):
    """Encode one vehicle dict into the 20 register values of a slot.

    Expected keys: id, lat, lon (degrees), speed_cms, heading_cdeg, accel_cms2,
    roll, pitch, flags, battery, rssi, snr_cdb, pdr_permille, distance_dm,
    ttc_ds, warn, gps_quality, age_ms. A value of None is encoded as NONE (65535).
    """
    v = vehicle
    lat_hi, lat_lo = split_s32(round(v["lat"] * 1e7))
    lon_hi, lon_lo = split_s32(round(v["lon"] * 1e7))
    regs = [0] * SLOT_SIZE
    regs[OFF_ID] = _u16c(v["id"])
    regs[OFF_LAT_HI] = lat_hi
    regs[OFF_LAT_LO] = lat_lo
    regs[OFF_LON_HI] = lon_hi
    regs[OFF_LON_LO] = lon_lo
    regs[OFF_SPEED] = _u16c(v["speed_cms"])
    regs[OFF_HEADING] = _u16c(v["heading_cdeg"])
    regs[OFF_ACCEL] = _s16c(v["accel_cms2"])
    regs[OFF_ROLL] = _s16c(v["roll"])
    regs[OFF_PITCH] = _s16c(v["pitch"])
    regs[OFF_FLAGS] = _u16c(v["flags"])
    regs[OFF_BATTERY] = _u16c(v["battery"])
    regs[OFF_RSSI] = _s16c(v["rssi"])
    regs[OFF_SNR] = _s16c(v["snr_cdb"])
    regs[OFF_PDR] = _u16c(v["pdr_permille"])
    regs[OFF_DISTANCE] = _u16c(v.get("distance_dm"))
    regs[OFF_TTC] = _u16c(v.get("ttc_ds"))
    regs[OFF_WARN] = _u16c(v["warn"])
    regs[OFF_GPS_QUALITY] = _u16c(v["gps_quality"])
    regs[OFF_AGE_MS] = _u16c(v["age_ms"])
    return regs


# --- Decoding (mirrors the Node-RED function in S12.4) -----------------------
def decode(regs):
    """Decode 110 holding-register values into {'gateway': {...}, 'vehicles': [...]}.

    Empty slots (VEHICLE_ID == 0) are skipped. Field names match S12.4.
    """
    if len(regs) < NUM_REGS:
        raise ValueError(f"expected {NUM_REGS} registers, got {len(regs)}")
    r = [int(x) & 0xFFFF for x in regs]
    gateway = {
        "fw": r[FW_VERSION],
        "status": r[STATUS],
        "neighbors": r[NEIGHBOR_COUNT],
        "rttMs": r[LAST_RTT_MS],
        "acks": r[CMD_ACK_COUNT],
        "rx": r[RX_TOTAL],
        "tx": r[TX_TOTAL],
        "crcErr": r[CRC_ERRORS],
        "gatewayId": r[GATEWAY_ID],
        "uptime": r[UPTIME_S],
    }
    vehicles = []
    for slot in range(NUM_SLOTS):
        b = SLOT_BASE + SLOT_SIZE * slot
        if r[b] == 0:
            continue
        f = r[b + OFF_FLAGS]
        vehicles.append({
            "slot": slot,
            "id": r[b],
            "lat": join_s32(r[b + OFF_LAT_HI], r[b + OFF_LAT_LO]) / 1e7,
            "lon": join_s32(r[b + OFF_LON_HI], r[b + OFF_LON_LO]) / 1e7,
            "speedKmh": r[b + OFF_SPEED] / 100 * 3.6,
            "heading": r[b + OFF_HEADING] / 100,
            "accel": s16(r[b + OFF_ACCEL]) / 100,
            "roll": s16(r[b + OFF_ROLL]),
            "pitch": s16(r[b + OFF_PITCH]),
            "flags": f,
            "hardBrake": bool(f & FLAG_HARD_BRAKE),
            "crash": bool(f & FLAG_CRASH),
            "rollover": bool(f & FLAG_ROLLOVER),
            "hazard": bool(f & FLAG_HAZARD),
            "gpsValid": bool(f & FLAG_GPS_VALID),
            "lowBattery": bool(f & FLAG_LOW_BATTERY),
            "battery": r[b + OFF_BATTERY],
            "rssi": s16(r[b + OFF_RSSI]),
            "snr": s16(r[b + OFF_SNR]) / 10,
            "pdr": r[b + OFF_PDR] / 10,
            "distance": None if r[b + OFF_DISTANCE] == NONE else r[b + OFF_DISTANCE] / 10,
            "ttc": None if r[b + OFF_TTC] == NONE else r[b + OFF_TTC] / 10,
            "warn": r[b + OFF_WARN],
            "sats": r[b + OFF_GPS_QUALITY] >> 4,
            "hdop": r[b + OFF_GPS_QUALITY] & 15,
            "ageMs": r[b + OFF_AGE_MS],
        })
    return {"gateway": gateway, "vehicles": vehicles}
