"""Compile (and optionally upload) the OBU firmware for one vehicle ID and options.

Usage (from the repo root):
    tools/.venv/Scripts/python.exe tools/flash.py --id 1
    tools/.venv/Scripts/python.exe tools/flash.py --id 2 --sim
    tools/.venv/Scripts/python.exe tools/flash.py --id 1 --sim --compile-only
    tools/.venv/Scripts/python.exe tools/flash.py --id 2 --gateway --port COM7

Options:
    --id N          vehicle ID 1..254 (-DMY_VEHICLE_ID=N)
    --sim           simulation mode (-DSIM_MODE=1)
    --gateway       gateway build (-DIS_GATEWAY=1); needs gateway.cpp
    --log           LOG_MODE=1: 20 Hz CSV log (a_long, a_total, az_hp, roll, pitch, speed, level, TTC) for threshold tuning
    --compile-only  build only, do not upload
    --port PORT     upload port (not needed for SWD upload; passed through if given)

Build output goes to build/obu<N>/. The options are passed with
--build-property build.extra_flags=..., which is empty in the STM32duino 3.1.0 GenF1
platform (USB CDC comes from build.st_extra_flags, which is not touched).
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW_DIR = "firmware/v2v_obu"
FQBN_FILE = ROOT / "tools" / "fqbn.txt"
ARDUINO_CLI_FALLBACK = "C:/Program Files/Arduino CLI/arduino-cli.exe"


def find_arduino_cli():
    found = shutil.which("arduino-cli")
    if found:
        return found
    if os.path.exists(ARDUINO_CLI_FALLBACK):
        return ARDUINO_CLI_FALLBACK
    sys.exit("arduino-cli not found on PATH or at " + ARDUINO_CLI_FALLBACK)


def build_extra_flags(args):
    flags = [f"-DMY_VEHICLE_ID={args.id}"]
    if args.sim:
        flags.append("-DSIM_MODE=1")
    if args.gateway:
        flags.append("-DIS_GATEWAY=1")
    if args.log:
        flags.append("-DLOG_MODE=1")
    return " ".join(flags)


def run(cmd):
    print("+ " + " ".join(f'"{c}"' if " " in c else c for c in cmd), flush=True)
    return subprocess.call(cmd, cwd=ROOT)


def main():
    p = argparse.ArgumentParser(description="Compile and upload the V2V OBU firmware.")
    p.add_argument("--id", type=int, required=True, help="vehicle ID, 1..254")
    p.add_argument("--sim", action="store_true", help="simulation mode (SIM_MODE=1)")
    p.add_argument("--gateway", action="store_true", help="gateway build (IS_GATEWAY=1)")
    p.add_argument("--log", action="store_true", help="LOG_MODE=1: 20 Hz CSV data log for threshold tuning (S13.3)")
    p.add_argument("--compile-only", action="store_true", help="compile, do not upload")
    p.add_argument("--port", help="upload port (optional)")
    args = p.parse_args()

    if not 1 <= args.id <= 254:
        p.error("--id must be 1..254")

    fqbn = FQBN_FILE.read_text().strip()
    cli = find_arduino_cli()
    build_path = f"build/obu{args.id}"
    extra = build_extra_flags(args)

    rc = run([cli, "compile", "--fqbn", fqbn,
              "--build-property", f"build.extra_flags={extra}",
              "--build-path", build_path, "--warnings", "all", FW_DIR])
    if rc != 0:
        return rc
    if args.compile_only:
        return 0

    upload = [cli, "upload", "--fqbn", fqbn, "--input-dir", build_path]
    if args.port:
        upload += ["--port", args.port]
    return run(upload)


if __name__ == "__main__":
    sys.exit(main())
