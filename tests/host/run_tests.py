#!/usr/bin/env python3
"""Run the host unit tests.

1. C++ tests: compiles tests/host/*.cpp with the portable firmware files
   (v2v_packet, geo, neighbors, safety) using g++ and runs them.
2. Python tests: tests/host/test_*.py via unittest, if any exist.

Exit code is nonzero if anything fails.
Usage (from repo root): tools/.venv/Scripts/python.exe tests/host/run_tests.py
"""
import glob
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HOST = ROOT / "tests" / "host"
BUILD = HOST / "build"
FW = ROOT / "firmware" / "v2v_obu"
PORTABLE_SOURCES = ["v2v_packet", "geo", "neighbors", "safety"]
GXX_GLOB = "C:/Users/*/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs*/mingw64/bin/g++.exe"


def find_gxx():
    found = shutil.which("g++")
    if found:
        return found
    env_path = os.environ.get("GXX")
    if env_path and Path(env_path).exists():
        return env_path
    hits = sorted(glob.glob(GXX_GLOB))
    if hits:
        return hits[-1]
    return None


def run_cpp_tests():
    gxx = find_gxx()
    if gxx is None:
        print("ERROR: g++ not found (checked PATH, $GXX and the WinGet WinLibs folder)")
        return 2

    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / ("host_tests.exe" if os.name == "nt" else "host_tests")
    sources = sorted(str(p) for p in HOST.glob("*.cpp"))
    sources += [str(FW / (name + ".cpp")) for name in PORTABLE_SOURCES]
    cmd = [gxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
           "-I", str(FW), *sources, "-o", str(exe)]
    print("[build] g++ -std=c++17 -Wall -Wextra -Werror ->", exe.relative_to(ROOT))
    build = subprocess.run(cmd, cwd=str(ROOT))
    if build.returncode != 0:
        print("[build] FAILED")
        return build.returncode

    # The WinLibs runtime DLLs live next to g++; make them findable when running the test exe.
    env = os.environ.copy()
    env["PATH"] = str(Path(gxx).parent) + os.pathsep + env.get("PATH", "")
    print("[run] host_tests")
    run = subprocess.run([str(exe)], cwd=str(ROOT), env=env)
    return run.returncode


def run_python_tests():
    if not list(HOST.glob("test_*.py")):
        print("[python] no tests/host/test_*.py files, skipping")
        return 0

    env = os.environ.copy()
    tools = str(ROOT / "tools")
    env["PYTHONPATH"] = os.pathsep.join(p for p in (tools, env.get("PYTHONPATH", "")) if p)
    cmd = [sys.executable, "-m", "unittest", "discover",
           "-s", str(HOST), "-p", "test_*.py"]
    print("[python] unittest discover tests/host")
    return subprocess.run(cmd, cwd=str(ROOT), env=env).returncode


def main():
    rc_cpp = run_cpp_tests()
    rc_py = run_python_tests()
    ok = rc_cpp == 0 and rc_py == 0
    print("\nALL HOST TESTS PASSED" if ok else "\nHOST TESTS FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
