#!/usr/bin/env python3
"""M4 host-only test: actual board adapter + fake UART/DMA transport.

Also runs all inherited M0-M3 tests. No live STM32 or RS-485 access.
To perform an embedded compile, use -DMODBUS_AUTOBAUD_V2_PORT_ENABLE=1
with the project's installed arm-none-eabi toolchain.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "App/Middleware/AutobaudV2/Src"
INC = ROOT / "App/Middleware/AutobaudV2/Inc"
BSP = ROOT / "App/BSP/Communication"
PORT = ROOT / "App/Middleware/ModbusEngine/Inc"
PORTSERIAL = ROOT / "App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c"


def audit_m4_opt_in():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    cfg = (ROOT / "App/Middleware/ModbusEngine/Inc/modbus_engine_config.h").read_text(
        encoding="utf-8")
    serial = PORTSERIAL.read_text(encoding="utf-8")
    task = (ROOT / "App/Application/Tasks/task_modbus.c").read_text(
        encoding="utf-8")
    assert 'set(MODBUS_AUTOBAUD_V2_PORT_ENABLE "0" CACHE STRING' in cmake
    assert 'if(MODBUS_AUTOBAUD_V2_PORT_ENABLE STREQUAL "1")' in cmake
    assert "MODBUS_AUTOBAUD_V2_PORT_ENABLE=${MODBUS_AUTOBAUD_V2_PORT_ENABLE}" in cmake
    assert re.search(
        r"#ifndef\s+MODBUS_AUTOBAUD_V2_PORT_ENABLE\s*\n"
        r"#define\s+MODBUS_AUTOBAUD_V2_PORT_ENABLE\s+0\b", cfg)
    assert "#if MODBUS_AUTOBAUD_V2_PORT_ENABLE" in serial
    assert "s_v2_worker_parked = 1U" in serial
    assert "ModbusEngineV2_Claim(void)" in serial
    assert "ModbusEngineV2_Quiesce(void)" in serial
    assert "ModbusEngineV2_Apply(const autobaud_v2_profile_t *profile)" in serial
    assert "ModbusEngineV2_Restart(bool passive)" in serial
    assert "ModbusEngineV2_Verify(const autobaud_v2_profile_t *profile," in serial
    assert "v2_pin_uart_verified(void)" in serial
    assert "v2_dma_enabled(void)" in serial
    assert "ModbusEngineV2_SnapshotSession(uint32_t *session_id," in serial
    assert "s_v2_passive == 0U" in serial
    assert "++s_v2_rx_error_generation" in serial
    assert "AutobaudV2" not in task
    assert "PyxisAutobaudUart_" not in task
    assert "if (v2_auto_selected && transport_ready)" in task
    assert (
        "MODBUS_AUTOBAUD_V2_PORT_ENABLE" not in
        (ROOT / "CMakePresets.json").read_text(encoding="utf-8")
    )


def run_mock_adapter():
    compiler = os.environ.get("CC") or next(
        (name for name in ("cc", "gcc", "clang") if shutil.which(name)), None)
    if compiler is None:
        raise RuntimeError("No native C11 compiler found: set CC")
    with tempfile.TemporaryDirectory(prefix="pyxis-v2-m4-") as directory:
        exe = Path(directory) / (
            "m4_adapter.exe" if os.name == "nt" else "m4_adapter")
        cmd = [
            compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-pedantic", "-I", str(INC), "-I", str(BSP), "-I", str(PORT),
            str(SRC / "autobaud_v2.c"),
            str(SRC / "autobaud_uart_transaction.c"),
            str(BSP / "pyxis_autobaud_uart_stm32h563.c"),
            str(ROOT / "tests/autobaud_v2/test_pyxis_uart_adapter.c"),
            "-o", str(exe)
        ]
        subprocess.run(cmd, check=True, cwd=ROOT)
        subprocess.run([str(exe)], check=True, cwd=ROOT)


def main():
    audit_m4_opt_in()
    subprocess.run(
        [sys.executable, str(ROOT / "tests/test_pyxis_autobaud_v2_m3.py")],
        check=True, cwd=ROOT
    )
    run_mock_adapter()
    print("Pyxis Autobaud V2 M4 native UART transaction adapter suite: PASS")


if __name__ == "__main__":
    main()
