"""Read and decode the V2V gateway holding registers 0-109 (Modbus FC03).

Examples:
    tools/.venv/Scripts/python.exe tools/modbus_probe.py                      # TCP 127.0.0.1:5020, forever
    tools/.venv/Scripts/python.exe tools/modbus_probe.py --tcp 127.0.0.1:5020 --count 5
    tools/.venv/Scripts/python.exe tools/modbus_probe.py --serial COM7 --raw --count 1
    tools/.venv/Scripts/python.exe tools/modbus_probe.py --tcp 127.0.0.1:5020 --command 4 0 1

Serial: 115200 8N1 RTU, slave ID 1, timeout 0.5 s.
"""

import argparse
import sys
import time

from pymodbus.client import ModbusSerialClient, ModbusTcpClient

import v2v_modbus as m

UNIT = 1
FLAG_NAMES = [
    (m.FLAG_HARD_BRAKE, "HARD_BRAKE"),
    (m.FLAG_CRASH, "CRASH"),
    (m.FLAG_ROLLOVER, "ROLLOVER"),
    (m.FLAG_HAZARD, "HAZARD"),
    (m.FLAG_WARN_CAUTION, "WARN_CAUTION"),
    (m.FLAG_WARN_CRITICAL, "WARN_CRITICAL"),
    (m.FLAG_GPS_VALID, "GPS_VALID"),
    (m.FLAG_LOW_BATTERY, "LOW_BATTERY"),
]
STATUS_NAMES = [
    (m.STATUS_RADIO_OK, "radio"),
    (m.STATUS_GPS_FIX, "gps"),
    (m.STATUS_IMU_OK, "imu"),
    (m.STATUS_SIM_MODE, "SIM"),
]


def open_client(args):
    if args.serial:
        client = ModbusSerialClient(args.serial, baudrate=115200, bytesize=8,
                                    parity="N", stopbits=1, timeout=0.5)
        label = f"serial {args.serial}"
    else:
        host, _, port = args.tcp.rpartition(":")
        if not host:
            raise SystemExit(f"--tcp must be HOST:PORT, got {args.tcp!r}")
        client = ModbusTcpClient(host, port=int(port), timeout=0.5)
        label = f"tcp {host}:{port}"
    if not client.connect():
        raise SystemExit(f"cannot open {label} (is the simulator running / is the port right?)")
    return client, label


def read_regs(client):
    rr = client.read_holding_registers(0, count=m.NUM_REGS, device_id=UNIT)
    if rr.isError():
        raise IOError(f"Modbus exception response: {rr}")
    return list(rr.registers)


def flag_text(flags):
    names = [name for bit, name in FLAG_NAMES if flags & bit]
    return ",".join(names) if names else "-"


def format_gateway(g):
    st = " ".join(name for bit, name in STATUS_NAMES if g["status"] & bit) or "none"
    return (f"GW fw={g['fw']} id={g['gatewayId']} status=0x{g['status']:02X}({st}) "
            f"nb={g['neighbors']} rtt={g['rttMs']}ms acks={g['acks']} "
            f"rx={g['rx']} tx={g['tx']} crc={g['crcErr']} up={g['uptime']}s")


def format_vehicle(v):
    dist = "none" if v["distance"] is None else f"{v['distance']:.1f}m"
    ttc = "none" if v["ttc"] is None else f"{v['ttc']:.1f}s"
    return (f"  slot{v['slot']} V{v['id']} lat={v['lat']:.7f} lon={v['lon']:.7f} "
            f"spd={v['speedKmh']:5.1f}km/h hdg={v['heading']:6.2f} acc={v['accel']:+.2f}m/s2 "
            f"roll={v['roll']:+d} pitch={v['pitch']:+d} bat={v['battery']}% "
            f"rssi={v['rssi']}dBm snr={v['snr']:.1f}dB pdr={v['pdr']:.1f}% "
            f"dist={dist} ttc={ttc} warn={v['warn']} sats={v['sats']} hdop={v['hdop']} "
            f"age={v['ageMs']}ms flags=[{flag_text(v['flags'])}]")


def print_snapshot(regs):
    d = m.decode(regs)
    stamp = time.strftime("%H:%M:%S")
    print(f"{stamp} {format_gateway(d['gateway'])}")
    if not d["vehicles"]:
        print("  (no vehicles in slots)")
    for v in d["vehicles"]:
        print(format_vehicle(v))
    sys.stdout.flush()


def do_command(client, code, param, target):
    values = [code, param, target, 1]
    rr = client.write_registers(m.CMD_CODE, values, device_id=UNIT)
    if rr.isError():
        raise SystemExit(f"command write failed: {rr}")
    print(f"sent command code={code} param={param} target={target} (regs 100-103 = {values})")
    time.sleep(0.2)  # give the gateway time to act and count the ACK
    regs = read_regs(client)
    print(f"readback: reg103(trigger)={regs[m.CMD_TRIGGER]} "
          f"reg4(CMD_ACK_COUNT)={regs[m.CMD_ACK_COUNT]} reg3(LAST_RTT_MS)={regs[m.LAST_RTT_MS]}")


def main(argv=None):
    p = argparse.ArgumentParser(description="Probe the V2V gateway Modbus registers")
    link = p.add_mutually_exclusive_group()
    link.add_argument("--tcp", default="127.0.0.1:5020", help="HOST:PORT (default 127.0.0.1:5020)")
    link.add_argument("--serial", metavar="PORT", help="serial port, e.g. COM7 (115200 8N1 RTU, unit 1)")
    p.add_argument("--count", type=int, default=0, help="stop after N reads (0 = forever)")
    p.add_argument("--raw", action="store_true", help="print the raw 110 register values")
    p.add_argument("--command", nargs=3, type=int, metavar=("CODE", "PARAM", "TARGET"),
                   help="write [CODE, PARAM, TARGET, 1] to register 100, then exit")
    args = p.parse_args(argv)

    client, label = open_client(args)
    try:
        if args.command:
            do_command(client, *args.command)
            return 0
        print(f"probing {label}, unit {UNIT}, 1 read/s", flush=True)
        n = 0
        while True:
            t0 = time.monotonic()
            try:
                regs = read_regs(client)
                if args.raw:
                    print(time.strftime("%H:%M:%S"), regs)
                else:
                    print_snapshot(regs)
            except Exception as e:  # noqa: BLE001 - keep probing on transient errors
                print(f"{time.strftime('%H:%M:%S')} read error: {e}", flush=True)
            n += 1
            if args.count and n >= args.count:
                break
            time.sleep(max(0.0, 1.0 - (time.monotonic() - t0)))
    except KeyboardInterrupt:
        pass
    finally:
        client.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
