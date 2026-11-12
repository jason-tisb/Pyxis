#!/usr/bin/env python3
"""M3 software-only verification of Titania-derived on-demand and UART txn.

Run: python3 tests/test_pyxis_autobaud_v2_m3.py
No STM32, FreeRTOS, hardware access or external packages required.
A native C11 compiler (CC, cc, gcc, or clang) is required.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
INC = ROOT / "App/Middleware/AutobaudV2/Inc"
SRC = ROOT / "App/Middleware/AutobaudV2/Src"
TESTS = ROOT / "tests/autobaud_v2"
PORT = ROOT / "App/Application/Ports"

# Each test explicitly lists its real portable source dependencies.  This
# deliberately prevents accidental linking against HAL/RTOS/Modbus runtime.
CASES = {
    "ondemand": (
        ["autobaud_v2.c", "autobaud_ondemand.c"],
        "test_ondemand.c"
    ),
    "ondemand_edges": (
        ["autobaud_v2.c", "autobaud_ondemand.c"],
        "test_ondemand_edges.c"
    ),
    "uart_transaction": (
        ["autobaud_v2.c", "autobaud_uart_transaction.c"],
        "test_uart_transaction.c"
    ),
}


def check_m3_scope():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    task = (ROOT / "App/Application/Tasks/task_modbus.c").read_text(
        encoding="utf-8")
    # M4 adds a compile-only adapter behind a default-OFF CMake option.
    # It must not enable on-demand service in the active Modbus task.
    assert 'set(MODBUS_AUTOBAUD_V2_PORT_ENABLE "0" CACHE STRING' in cmake
    assert 'if(MODBUS_AUTOBAUD_V2_PORT_ENABLE STREQUAL "1")' in cmake
    assert "MODBUS_AUTOBAUD_V2_ENABLE" not in cmake
    assert "autobaud_od_service(" not in task
    assert "PyxisAutobaudPort_" not in task

    required = (
        SRC / "autobaud_ondemand.c",
        SRC / "autobaud_uart_transaction.c",
        INC / "autobaud_ondemand.h",
        INC / "autobaud_uart_transaction.h",
        INC / "autobaud_v2_framing.h",
    )
    for filename in required:
        assert filename.is_file(), filename

    # Generic engine modules cannot depend on hardware headers. The common
    # CRC helper in M2 is pure and not part of an active port or responder.
    for filename in required:
        contents = filename.read_text(encoding="utf-8")
        for include in re.findall(
            r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', contents, re.M
        ):
            lower = include.lower()
            assert not any(s in lower for s in (
                "stm32", "cmsis", "freertos", "gpio", "usart", "hw_common",
                "hw_uart", "hw_dma", "main.h", "portserial",
                "modbus_engine_port", "modbus_engine_runtime",
                "modbus_request", "app_runtime", "hal_",
            )), (filename, include)
        assert "HAL_" not in contents, filename


def run_m3_native():
    compiler = os.environ.get("CC") or next(
        (c for c in ("cc", "gcc", "clang") if shutil.which(c)), None)
    if compiler is None:
        raise RuntimeError("No native C11 compiler found; set CC")

    with tempfile.TemporaryDirectory(prefix="pyxis-autobaud-m3-") as tmp:
        for label, (sources, test_name) in CASES.items():
            exe = Path(tmp) / (
                label + ".exe" if os.name == "nt" else label)
            cmd = [
                compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-pedantic", "-I", str(INC), "-I", str(PORT),
            ]
            cmd += [str(SRC / name) for name in sources]
            cmd += [str(TESTS / test_name), "-o", str(exe)]
            subprocess.run(cmd, check=True, cwd=ROOT)
            subprocess.run([str(exe)], check=True, cwd=ROOT)


def main():
    check_m3_scope()
    subprocess.run(
        [sys.executable, str(ROOT / "tests/test_pyxis_autobaud_v2_m2.py")],
        check=True, cwd=ROOT
    )
    run_m3_native()
    print("Pyxis Autobaud V2 M3 portable supervisor/transaction suite: PASS")


if __name__ == "__main__":
    main()
