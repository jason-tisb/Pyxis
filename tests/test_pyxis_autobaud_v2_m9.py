#!/usr/bin/env python3
"""M9 source qualification. No hardware testing or flashing is performed.

M9 extends M8 with a request-activity gate and an *optional*, default-off
FE/PE-only rearm path. Runs the complete M0-M8 regressions first.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "App/Middleware/AutobaudV2/Src"
INC = ROOT / "App/Middleware/AutobaudV2/Inc"
BSP = ROOT / "App/BSP/Communication"
MB = ROOT / "App/Middleware/ModbusEngine/Inc"


def compiler():
    cc = os.environ.get("CC") or next(
        (x for x in ("cc", "gcc", "clang") if shutil.which(x)), None)
    if cc is None:
        raise RuntimeError("Native C11 compiler required")
    return cc


def static_safety_audit():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    config = (MB / "modbus_engine_config.h").read_text(encoding="utf-8")
    coord = (BSP / "pyxis_autobaud_v2_stm32h563.c").read_text(
        encoding="utf-8")
    serial = (ROOT /
              "App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c"
              ).read_text(encoding="utf-8")
    first = (SRC / "autobaud_initial_window.c").read_text(
        encoding="utf-8")
    linker = (ROOT / "STM32H563xx_FLASH.ld").read_text(
        encoding="utf-8")
    assert 'set(MODBUS_AUTOBAUD_V2_REACQUIRE_ENABLE "0"' in cmake
    assert "MODBUS_AUTOBAUD_V2_REACQUIRE_ENABLE=${MODBUS_AUTOBAUD_V2_REACQUIRE_ENABLE}" in cmake
    assert "#define MODBUS_AUTOBAUD_V2_REACQUIRE_ENABLE 0" in config
    assert "MODBUS_AUTOBAUD_V2_REACQUIRE_ENABLE && !MODBUS_AUTOBAUD_V2_ENABLE" in config
    assert "MODBUS_AUTOBAUD_V2_REACQUIRE_ENABLE" not in (
        ROOT / "CMakePresets.json").read_text(encoding="utf-8")
    assert "AUTOBAUD_INITIAL_GRACE_MS" in first
    assert "autobaud_activity_observe(&window->error_activity, now_ms," in first
    assert "return window->first_rx_seen &&" not in first
    assert "PyxisAutobaudV2_NotifyHealthyExchange" in coord
    assert "autobaud_reacquire_observe" in coord
    assert "AUTOBAUD_UART_TXN_BUSY" in coord
    assert "PYXIS_AUTOBAUD_V2_REARMING" in coord
    assert "HAL_UART_ERROR_FE | HAL_UART_ERROR_PE" in serial
    assert "s_v2_uart_framing_error_count" in serial
    assert "0x08020000" in linker
    for name in ("autobaud_initial_window.c", "autobaud_reacquire.c"):
        portable = (SRC / name).read_text(encoding="utf-8")
        assert "HAL_" not in portable
        assert "osDelay(" not in portable
        assert "FLASH_" not in portable


def native_test(cc, src, label):
    with tempfile.TemporaryDirectory(prefix="pyxis-m9-" + label) as tmp:
        binary = Path(tmp) / ("test.exe" if os.name == "nt" else "test")
        args = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-pedantic", "-I", str(INC), "-I", str(BSP),
                "-I", str(MB)] + [str(x) for x in src] + ["-o", str(binary)]
        subprocess.run(args, check=True, cwd=ROOT)
        if label == "coordinator":
            for scenario in range(10):
                subprocess.run([str(binary), str(scenario)], check=True,
                               cwd=ROOT)
        else:
            subprocess.run([str(binary)], check=True, cwd=ROOT)


def compile_mode_matrix(cc):
    source = '#include "modbus_engine_config.h"\nint x;\n'
    flags = dict(MODBUS_ENGINE_ACTIVE=1, MODBUS_AUTOBAUD_ENABLE=0,
                 MODBUS_AUTOBAUD_V2_PORT_ENABLE=1,
                 MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE=1,
                 MODBUS_AUTOBAUD_V2_FRAMING_ENABLE=1,
                 MODBUS_AUTOBAUD_V2_ENABLE=1)
    for extra, expected in [
        ({}, True),
        ({"MODBUS_AUTOBAUD_V2_REACQUIRE_ENABLE": 1}, True),
        ({"MODBUS_AUTOBAUD_V2_ENABLE": 0,
          "MODBUS_AUTOBAUD_V2_REACQUIRE_ENABLE": 1}, False),
        ({"MODBUS_AUTOBAUD_V2_REACQUIRE_ENABLE": 2}, False),
    ]:
        defs = flags.copy()
        defs.update(extra)
        args = [cc, "-std=c11", "-x", "c", "-fsyntax-only",
                "-I", str(MB)] + ["-D%s=%s" % (k, v)
                                  for k, v in defs.items()] + ["-"]
        result = subprocess.run(args, input=source, text=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, cwd=ROOT)
        assert (result.returncode == 0) == expected, (
            defs, result.stderr)
    print("M9 mode guard matrix: PASS")


def main():
    static_safety_audit()
    cc = compiler()
    subprocess.run(
        [sys.executable, str(ROOT / "tests/test_pyxis_autobaud_v2_m8.py")],
        check=True, cwd=ROOT)
    native_test(cc, [SRC / "autobaud_reacquire.c",
                     ROOT / "tests/autobaud_v2/test_reacquire.c"], "guard")
    native_test(cc, [
        SRC / "autobaud_v2.c", SRC / "autobaud_ondemand.c",
        SRC / "autobaud_reacquire.c",
        BSP / "pyxis_autobaud_v2_stm32h563.c",
        ROOT / "tests/autobaud_v2/test_pyxis_v2_coordinator.c"
    ], "coordinator")
    compile_mode_matrix(cc)
    print("Pyxis Autobaud V2 M9 native qualification: PASS")


if __name__ == "__main__":
    main()
