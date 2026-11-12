#!/usr/bin/env python3
"""Compile and execute M1 portable C contracts; no firmware toolchain needed.

Run: python3 tests/test_pyxis_autobaud_v2_m1.py
Requires a native C11 compiler (CC, cc, gcc or clang).
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
V2 = ROOT / "App" / "Middleware" / "AutobaudV2"
INC = V2 / "Inc"
SRC = V2 / "Src"


def check_portable_includes():
    for source in (INC / "autobaud_v2.h",
                   INC / "autobaud_v2_measurement.h",
                   INC / "autobaud_ondemand.h",
                   SRC / "autobaud_v2.c"):
        data = source.read_text(encoding="utf-8")
        includes = re.findall(r"(?m)^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]", data)
        for name in includes:
            lowered = name.lower()
            assert not any(bad in lowered for bad in (
                "stm32", "main.h", "cmsis", "freertos", "hal_", "gpio",
                "usart", "hw_common", "modbus_engine", "app_runtime")), (
                    source, name)
    # Until M7 the firmware must not compile or invoke the new engine.
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    task = (ROOT / "App/Application/Tasks/task_modbus.c").read_text(
        encoding="utf-8")
    assert "App/Middleware/AutobaudV2/" not in cmake
    assert "PyxisAutobaudPort_" not in task
    assert "MODBUS_AUTOBAUD_V2_ENABLE" not in cmake


def main():
    check_portable_includes()
    compiler = os.environ.get("CC") or next(
        (cmd for cmd in ("cc", "gcc", "clang") if shutil.which(cmd)), None)
    if not compiler:
        raise SystemExit("M1 native C11 test NOT RUN: no compiler found")
    with tempfile.TemporaryDirectory(prefix="pyxis-m1-") as directory:
        executable = Path(directory) / (
            "m1_contracts.exe" if os.name == "nt" else "m1_contracts")
        cmd = [
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I" + str(INC),
            "-I" + str(ROOT / "App/Application/Ports"),
            str(SRC / "autobaud_v2.c"),
            str(ROOT / "tests/test_pyxis_autobaud_v2_m1.c"),
            "-o", str(executable)
        ]
        subprocess.run(cmd, check=True, cwd=ROOT)
        subprocess.run([str(executable)], check=True, cwd=ROOT)
    print("M1 portability/static build audit: PASS")


if __name__ == "__main__":
    main()
