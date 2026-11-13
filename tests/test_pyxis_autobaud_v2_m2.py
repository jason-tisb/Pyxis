#!/usr/bin/env python3
"""Pyxis M2: native hardware-free tests of Titania-derived portable algorithms.

Usage: python3 tests/test_pyxis_autobaud_v2_m2.py
Requires a C11 host compiler (CC, cc, gcc or clang) and Python >=3.8.
Only reads repository files; temporary host binaries live outside the repo.
Does NOT build firmware or perform UART/RS-485 hardware qualification.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
V2 = ROOT / "App" / "Middleware" / "AutobaudV2"
V2_INC = V2 / "Inc"
V2_SRC = V2 / "Src"
MODBUS_INC = ROOT / "App/Middleware/ModbusEngine/Inc"
MODBUS_SRC = ROOT / "App/Middleware/ModbusEngine/Src"

# Executables link only the minimal pure C dependencies listed here.
# In particular, no Modbus request handler, UART, GPIO, FreeRTOS or STM32 HAL.
CASES = {
    "activity": (("autobaud_activity.c",), ()),
    "score": (("autobaud_score.c",), ()),
    "validate": (("autobaud_validate.c",), ("modbus_crc.c",)),
    "evidence": (("autobaud_v2_evidence.c",), ()),
    "timestamp32": (("autobaud_timestamp32.c",), ()),
}


def check_source_separation():
    # This migration must remain inactive in the embedded firmware until M7.
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    task = (ROOT / "App/Application/Tasks/task_modbus.c").read_text(
        encoding="utf-8")
    # M4 adds a compile-only adapter behind a default-OFF CMake option.
    # It must not enable on-demand service in the active Modbus task.
    assert 'set(MODBUS_AUTOBAUD_V2_PORT_ENABLE "0" CACHE STRING' in cmake
    assert 'if(MODBUS_AUTOBAUD_V2_PORT_ENABLE STREQUAL "1")' in cmake
    assert 'set(MODBUS_AUTOBAUD_V2_ENABLE "0" CACHE STRING' in cmake
    assert "PyxisAutobaudPort_" not in task

    for source in sorted(V2_INC.glob("*.h")) + sorted(V2_SRC.glob("*.c")):
        contents = source.read_text(encoding="utf-8")
        for include in re.findall(
            r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', contents, re.M
        ):
            lowered = include.lower()
            forbidden = (
                "stm32", "cmsis", "freertos", "gpio", "usart", "hw_common",
                "hw_uart", "hw_dma", "main.h", "modbus_engine_port",
                "modbus_engine_runtime", "modbus_request", "app_runtime",
                "portserial", "hal_"
            )
            assert not any(term in lowered for term in forbidden), (
                source.relative_to(ROOT), include
            )
        assert "HAL_" not in contents, source
    # The request validator reuses the existing *pure* shared Modbus CRC.
    assert '#include "modbus_crc.h"' in (
        V2_SRC / "autobaud_validate.c").read_text(encoding="utf-8")


def test_m2_host_compilation():
    compiler = os.environ.get("CC") or next(
        (candidate for candidate in ("cc", "gcc", "clang")
         if shutil.which(candidate)), None)
    if not compiler:
        raise RuntimeError("No native C11 compiler found (set CC)")

    with tempfile.TemporaryDirectory(prefix="pyxis-autobaud-v2-m2-") as tmp:
        for name, (sources, dependencies) in CASES.items():
            exe = Path(tmp) / (
                name + ".exe" if os.name == "nt" else name)
            args = [
                compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-pedantic", "-I", str(V2_INC), "-I", str(MODBUS_INC),
            ]
            args.extend(str(V2_SRC / file) for file in sources)
            args.extend(str(MODBUS_SRC / file) for file in dependencies)
            args.extend([str(ROOT / "tests/autobaud_v2" /
                             ("test_" + name + ".c")), "-o", str(exe)])
            subprocess.run(args, check=True, cwd=ROOT)
            subprocess.run([str(exe)], check=True, cwd=ROOT)


def main():
    check_source_separation()
    # Confirm inherited M0/M1 and original Pyxis policy still pass.
    scripts = [
        "tests/test_pyxis_autobaud_v2_m0.py",
        "tests/test_pyxis_autobaud_v2_m1.py",
        "tests/test_modbus_autobaud_score.py",
        "tests/test_modbus_autobaud_validate.py",
        "tests/test_modbus_autobaud_scan.py",
        "tests/test_modbus_autobaud_startup.py",
        "tests/test_modbus_autobaud_integration.py",
        "tests/test_modbus_autobaud_diag.py",
    ]
    for script in scripts:
        subprocess.run([sys.executable, str(ROOT / script)], check=True, cwd=ROOT)
    test_m2_host_compilation()
    print("Pyxis Autobaud V2 M2 portable suite: PASS")


if __name__ == "__main__":
    main()
