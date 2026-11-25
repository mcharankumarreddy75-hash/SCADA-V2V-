"""Gateway simulator: Modbus TCP server with the same register map as the OBU #2 gateway.

Usage:
    tools/.venv/Scripts/python.exe tools/gateway_sim.py [--host 127.0.0.1] [--port 5020]
        [--lat0 17.3850] [--lon0 78.4867] [--speedup 1.0] [--seconds N]

Scenario (spec S10.6 / S11): vehicle 1 (slot 1) is stationary at lat0/lon0, heading 90.
Vehicle 2 (gateway, slot 0) starts 120 m west, heading 90, 8 m/s, brakes to a stop
3 m behind vehicle 1, waits 10 s and then restarts from the start position.

Pymodbus API used (pymodbus 3.16.2): SimDevice + SimData (pymodbus.simulator),
ModbusTcpServer(device, address=(host, port)) whose .context is a SimCore; the
background task reads/writes registers through SimCore.async_getValues/async_setValues.

PLACEHOLDER coordinates (17.3850, 78.4867) - replace with the college coordinates.
"""

import argparse
import asyncio
import math
import random
import sys
import time

from pymodbus.server import ModbusTcpServer
from pymodbus.simulator import DataType, SimData, SimDevice

import v2v_modbus as m

SIM_UNIT_ID = 1
TICK_S = 0.1                 # 10 Hz register update
EARTH_R = 6371000.0
M_PER_DEG_LAT = 111320.0

GATEWAY_VID = 2              # vehicle 2 = gateway = slot 0
NEIGHBOUR_VID = 1            # vehicle 1 = slot 1

V2_START_X = -120.0          # metres east of vehicle 1
V2_STOP_X = -3.0             # stops 3 m behind vehicle 1
V2_SPEED = 8.0               # m/s
V2_DECEL = 4.5               # m/s^2 -> stop time 1.78 s, matches ~-450 cm/s^2 for last ~1.8 s
V2_WAIT_S = 10.0
HARD_BRAKE_S = 2.0
HAZARD_PERIOD_S = 90.0
HAZARD_LEN_S = 2.0
CRASH_PERIOD_S = 120.0
CRASH_AUTO_CLEAR_S = 30.0
LOSS_PERIOD_S = 180.0
LOSS_START_IN_PERIOD_S = 90.0  # link loss begins 90 s into each 180 s period
LOSS_RAMP_S = 1.0              # AGE_MS ramps up over this time before the slot is zeroed
LOSS_LEN_S = 5.0
BATTERY_PERIOD_S = 3600.0


def haversine_m(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = p2 - p1
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * EARTH_R * math.asin(math.sqrt(a))


def ts():
    t = time.time()
    return time.strftime("%H:%M:%S", time.localtime(t)) + f".{int(t * 1000) % 1000:03d}"


class Sim:
    """All simulated state. Physics and events advance by sim-time dt."""

    def __init__(self, lat0, lon0, rng):
        self.lat0, self.lon0 = lat0, lon0
        self.rng = rng
        self.t = 0.0                    # sim seconds since start
        # vehicle 2 (gateway) kinematics
        self.v2_x = V2_START_X
        self.v2_v = V2_SPEED
        self.v2_accel = 0.0
        self.v2_phase = "approach"      # approach | brake | wait
        self.v2_phase_t = 0.0           # time entered phase
        self.hard_brake_until = -1.0
        # events
        self.crash_active = False
        self.crash_started = -1e9
        self.loss_active = False
        self.loss_ramp_frac = None      # 0..1 while AGE_MS ramps up before a loss
        # counters
        self.rx = 0.0
        self.tx = 0.0
        self.crc = 0
        self.rtt_ms = 0
        self.acks = 0
        self.last_neighbor_report = {}
        # latest computed view (for printing / tests)
        self.ttc_s = None
        self.dist_m = None
        self.warn = 0

    # ---- physics / events ---------------------------------------------------
    def step(self, dt):
        self.t += dt
        t = self.t
        if self.v2_phase == "approach":
            self.v2_accel = 0.0
            self.v2_x += self.v2_v * dt
            # braking point so that the vehicle stops at V2_STOP_X with V2_DECEL
            brake_x = V2_STOP_X - (self.v2_v ** 2) / (2 * V2_DECEL)
            if self.v2_x >= brake_x:
                self._enter("brake")
                self.hard_brake_until = t + HARD_BRAKE_S
        elif self.v2_phase == "brake":
            self.v2_accel = -V2_DECEL
            self.v2_v = max(0.0, self.v2_v - V2_DECEL * dt)
            self.v2_x = min(self.v2_x + self.v2_v * dt, V2_STOP_X)
            if self.v2_v <= 0.0:
                self.v2_v = 0.0
                self.v2_x = V2_STOP_X
                self.v2_accel = 0.0
                self._enter("wait")
        elif self.v2_phase == "wait":
            self.v2_accel = 0.0
            if t - self.v2_phase_t >= V2_WAIT_S:
                self.v2_x = V2_START_X
                self.v2_v = V2_SPEED
                self._enter("approach")

        # crash on vehicle 1 every 2 min, auto-clear after 30 s
        if t >= CRASH_PERIOD_S and int(t // CRASH_PERIOD_S) != int((t - dt) // CRASH_PERIOD_S):
            self.crash_active = True
            self.crash_started = t
            print(f"[{ts()}] SIM event: crash on vehicle 1", flush=True)
        if self.crash_active and t - self.crash_started >= CRASH_AUTO_CLEAR_S:
            self.crash_active = False
            print(f"[{ts()}] SIM event: crash auto-cleared", flush=True)

        # link loss on neighbour (vehicle 1) every 3 min
        phase = t % LOSS_PERIOD_S
        ramp_start = LOSS_START_IN_PERIOD_S - LOSS_RAMP_S
        self.loss_ramp_frac = (phase - ramp_start) / LOSS_RAMP_S if ramp_start <= phase < LOSS_START_IN_PERIOD_S else None
        self.loss_active = LOSS_START_IN_PERIOD_S <= phase < LOSS_START_IN_PERIOD_S + LOSS_LEN_S

        # counters: ~5 packets/s each way, occasional CRC errors
        self.rx += 5.0 * dt
        self.tx += 5.0 * dt
        if self.rng.random() < 0.005:
            self.crc += 1

    def _enter(self, phase):
        self.v2_phase = phase
        self.v2_phase_t = self.t

    # ---- derived values -----------------------------------------------------
    def battery_pct(self):
        frac = (self.t % BATTERY_PERIOD_S) / BATTERY_PERIOD_S
        return int(round(100 - 80 * frac))

    def positions(self):
        """Return ((lat1, lon1), (lat2, lon2)) for vehicle 1 and vehicle 2."""
        dlon = self.v2_x / (M_PER_DEG_LAT * math.cos(math.radians(self.lat0)))
        return (self.lat0, self.lon0), (self.lat0, self.lon0 + dlon)

    def neighbour_visible(self):
        return not self.loss_active

    def compute(self):
        """Compute distance, TTC and warn for vehicle 2 relative to vehicle 1."""
        (lat1, lon1), (lat2, lon2) = self.positions()
        d = haversine_m(lat1, lon1, lat2, lon2)
        self.dist_m = d
        closing = self.v2_v                      # vehicle 1 is stationary
        ttc = d / closing if closing > 0.01 else None
        moving = self.v2_v > 0.01
        headway = d / self.v2_v if moving else None
        warn = 0
        if ttc is not None and ttc <= 2.0:
            warn = 2
        elif moving and ((ttc is not None and ttc <= 4.0) or (headway is not None and headway < 1.0)):
            warn = 1
        self.ttc_s = ttc
        self.warn = warn
        return d, ttc, warn


def build_registers(sim, prev):
    """Build the full 110-register image. prev supplies the command registers 100-102."""
    regs = [0] * m.NUM_REGS
    rng = sim.rng
    t = sim.t
    (lat1, lon1), (lat2, lon2) = sim.positions()
    d, ttc, warn2 = sim.compute()
    neighbour_present = sim.neighbour_visible()
    if not neighbour_present:
        # no neighbour known to the gateway
        d_known, ttc_known, warn = None, None, 0
    else:
        d_known, ttc_known, warn = d, ttc, warn2

    # vehicle 2 flags (gateway, slot 0)
    f2 = m.FLAG_GPS_VALID
    if t < sim.hard_brake_until:
        f2 |= m.FLAG_HARD_BRAKE
    if (t % HAZARD_PERIOD_S) < HAZARD_LEN_S:
        f2 |= m.FLAG_HAZARD
    if warn == 1:
        f2 |= m.FLAG_WARN_CAUTION
    elif warn == 2:
        f2 |= m.FLAG_WARN_CRITICAL
    batt = sim.battery_pct()
    if batt < 25:
        f2 |= m.FLAG_LOW_BATTERY

    gq = (8 << 4) | 1  # 8 satellites, HDOP 1

    def jitter(scale):
        return rng.uniform(-scale, scale)

    rssi_v1 = None
    if neighbour_present:
        rssi = -40 - 25 * math.log10(max(d, 3.0) / 3.0) + jitter(2.0)  # log-distance, -40..-90 dBm
        rssi = max(-90.0, min(-40.0, rssi))
        snr_db = max(5.0, min(20.0, 20 + (rssi + 40) * 0.3 + jitter(0.5)))
        pdr = rng.randint(950, 1000)
        rssi_v1 = int(round(rssi))
        snr_cdb = int(round(snr_db * 10))
    else:
        pdr = 0
        snr_cdb = 0

    # slot 0: gateway vehicle 2
    lowest_ttc = ttc_known
    slot0 = {
        "id": GATEWAY_VID, "lat": lat2, "lon": lon2,
        "speed_cms": int(round(sim.v2_v * 100)),
        "heading_cdeg": 9000,
        "accel_cms2": int(round(sim.v2_accel * 100)),
        "roll": 0 if sim.v2_phase != "brake" else -2,
        "pitch": 0 if sim.v2_phase != "brake" else -3,
        "flags": f2,
        "battery": batt,
        "rssi": 0,
        "snr_cdb": 0,
        "pdr_permille": 1000,
        "distance_dm": None,
        "ttc_ds": None if lowest_ttc is None else int(round(min(lowest_ttc, 6553.0) * 10)),
        "warn": warn,
        "gps_quality": gq,
        "age_ms": 0,
    }
    regs[m.SLOT_BASE:m.SLOT_BASE + m.SLOT_SIZE] = m.encode_slot(slot0)

    # slot 1: neighbour vehicle 1 (zeroed during link loss)
    if neighbour_present:
        f1 = m.FLAG_GPS_VALID | (m.FLAG_CRASH if sim.crash_active else 0)
        if batt < 25:
            f1 |= m.FLAG_LOW_BATTERY
        # AGE_MS: normal ~100 ms; ramps up in the last second before loss
        if sim.loss_ramp_frac is not None:
            age = int(100 + 900 * sim.loss_ramp_frac)
        else:
            age = rng.randint(50, 150)
        slot1 = {
            "id": NEIGHBOUR_VID, "lat": lat1, "lon": lon1,
            "speed_cms": 0, "heading_cdeg": 9000, "accel_cms2": 0,
            "roll": int(round(jitter(1.0))),
            "pitch": int(round(jitter(1.0))),
            "flags": f1, "battery": batt,
            "rssi": rssi_v1, "snr_cdb": snr_cdb, "pdr_permille": pdr,
            "distance_dm": int(round(d * 10)),
            "ttc_ds": None if ttc is None else int(round(min(ttc, 6553.0) * 10)),
            "warn": 0, "gps_quality": gq, "age_ms": age,
        }
        regs[m.SLOT_BASE + m.SLOT_SIZE:m.SLOT_BASE + 2 * m.SLOT_SIZE] = m.encode_slot(slot1)

    # gateway registers 0-9
    regs[m.FW_VERSION] = 100
    regs[m.STATUS] = m.STATUS_RADIO_OK | m.STATUS_GPS_FIX | m.STATUS_IMU_OK | m.STATUS_SIM_MODE
    regs[m.NEIGHBOR_COUNT] = 1 if neighbour_present else 0
    regs[m.LAST_RTT_MS] = sim.rtt_ms & 0xFFFF
    regs[m.CMD_ACK_COUNT] = sim.acks & 0xFFFF
    regs[m.RX_TOTAL] = int(sim.rx) & 0xFFFF
    regs[m.TX_TOTAL] = int(sim.tx) & 0xFFFF
    regs[m.CRC_ERRORS] = sim.crc & 0xFFFF
    regs[m.GATEWAY_ID] = GATEWAY_VID
    regs[m.UPTIME_S] = int(t) & 0xFFFF

    # command registers 100-102 keep whatever the master wrote; 103 is cleared by the handler
    regs[m.CMD_CODE], regs[m.CMD_PARAM], regs[m.CMD_TARGET] = prev[100], prev[101], prev[102]
    return regs


async def handle_command(sim, code, param, target, loop):
    names = {1: "HAZARD_ALERT", 2: "SPEED_ADVISORY", 3: "CLEAR_SOS", 4: "BUZZER_TEST", 5: "PING"}
    print(f"[{ts()}] CMD code={code} ({names.get(code, '?')}) param={param} target={target}", flush=True)
    if code == m.PING:
        sim.rtt_ms = sim.rng.randint(15, 40)
        return
    if code not in (1, 2, 3, 4):
        print(f"[{ts()}] CMD code {code} unknown, ignored", flush=True)
        return
    if code == m.CLEAR_SOS and target in (0, NEIGHBOUR_VID) and sim.crash_active:
        sim.crash_active = False
        print(f"[{ts()}] SIM: crash cleared by CLEAR_SOS", flush=True)

    def ack():
        sim.acks += 1

    loop.call_later(0.05, ack)  # ACK after ~50 ms


async def tick_loop(core, sim, speedup, stop_event):
    """Background 10 Hz update: advance the simulation and write the register image."""
    loop = asyncio.get_running_loop()
    last = time.monotonic()
    while not stop_event.is_set():
        await asyncio.sleep(TICK_S)
        now = time.monotonic()
        dt_sim = (now - last) * speedup
        last = now
        sim.step(dt_sim)

        current = await core.async_getValues(SIM_UNIT_ID, 3, 0, m.NUM_REGS)
        if isinstance(current, list):
            prev = current
        else:
            prev = [0] * m.NUM_REGS
        if prev[m.CMD_TRIGGER] == 1:
            await handle_command(sim, prev[m.CMD_CODE], prev[m.CMD_PARAM], prev[m.CMD_TARGET], loop)
        regs = build_registers(sim, prev)
        regs[m.CMD_TRIGGER] = 0
        await core.async_setValues(SIM_UNIT_ID, 3, 0, regs)


async def run(args):
    rng = random.Random(args.seed)
    sim = Sim(args.lat0, args.lon0, rng)
    device = SimDevice(
        id=SIM_UNIT_ID,
        simdata=SimData(address=0, count=m.NUM_REGS, values=0, datatype=DataType.REGISTERS),
    )
    server = ModbusTcpServer(device, address=(args.host, args.port))
    core = server.context
    stop_event = asyncio.Event()
    print(f"[{ts()}] gateway_sim listening on {args.host}:{args.port} unit={SIM_UNIT_ID} "
          f"speedup={args.speedup} lat0={args.lat0} lon0={args.lon0} (placeholder coordinates)",
          flush=True)

    # seed the register image before the first request
    await core.async_setValues(SIM_UNIT_ID, 3, 0, build_registers(sim, [0] * m.NUM_REGS))

    serve_task = asyncio.create_task(server.serve_forever())
    tick_task = asyncio.create_task(tick_loop(core, sim, args.speedup, stop_event))
    try:
        if args.seconds > 0:
            await asyncio.sleep(args.seconds)
        else:
            await serve_task
    finally:
        stop_event.set()
        tick_task.cancel()
        try:
            await server.shutdown()
        except Exception:
            pass
        serve_task.cancel()
        print(f"[{ts()}] gateway_sim stopped", flush=True)


def main(argv=None):
    p = argparse.ArgumentParser(description="V2V gateway Modbus TCP simulator (unit 1, 127.0.0.1:5020)")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=5020)
    p.add_argument("--lat0", type=float, default=17.3850,
                   help="placeholder - replace with college coordinates")
    p.add_argument("--lon0", type=float, default=78.4867,
                   help="placeholder - replace with college coordinates")
    p.add_argument("--speedup", type=float, default=1.0, help="simulation time multiplier")
    p.add_argument("--seconds", type=float, default=0.0, help="auto-exit after N wall seconds (0 = run forever)")
    p.add_argument("--seed", type=int, default=None, help=argparse.SUPPRESS)
    args = p.parse_args(argv)
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
