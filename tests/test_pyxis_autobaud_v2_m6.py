#!/usr/bin/env python3
"""M6 software-only passive Modbus evidence and handoff regression.

Runs inherited M0-M5 suite, Titania original passive framing fixture,
pure initial grace policy and real M6 board adapter with a fake transport.
Requires native C11 compiler; ARM cross-compile is separate CI job.
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
BSP = ROOT / "App/BSP/Communication"
MB_INC = ROOT / "App/Middleware/ModbusEngine/Inc"
MB_SRC = ROOT / "App/Middleware/ModbusEngine/Src"
TESTS = ROOT / "tests/autobaud_v2"

CORE = [
    "autobaud_v2.c", "autobaud_validate.c", "autobaud_v2_evidence.c",
    "autobaud_v2_candidate.c", "autobaud_v2_framing.c",
]


def check_scoping():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    header = (MB_INC / "modbus_engine_config.h").read_text(encoding="utf-8")
    task = (ROOT / "App/Application/Tasks/task_modbus.c").read_text(
        encoding="utf-8")
    port = (ROOT / "App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c").read_text(
        encoding="utf-8")
    board = (BSP / "pyxis_autobaud_framing_stm32h563.c").read_text(
        encoding="utf-8")
    assert 'set(MODBUS_AUTOBAUD_V2_FRAMING_ENABLE "0" CACHE STRING' in cmake
    assert 'if(MODBUS_AUTOBAUD_V2_FRAMING_ENABLE STREQUAL "1")' in cmake
    assert "#define MODBUS_AUTOBAUD_V2_FRAMING_ENABLE 0" in header
    assert "MODBUS_AUTOBAUD_V2_FRAMING_ENABLE=${MODBUS_AUTOBAUD_V2_FRAMING_ENABLE}" in cmake
    assert "#if MODBUS_AUTOBAUD_V2_FRAMING_ENABLE" in port
    assert "v2_publish_passive_frames(void)" in port
    assert "ModbusEngineV2_TakePassiveFrame(" in port
    assert "ModbusEngineV2_VerifyPassiveFrame(" in port
    assert "ModbusEngineV2_AcceptPassiveFrame(" in port
    assert "s_v2_last_consumed_frame" in port
    assert "s_v2_session_error_baseline" in port
    assert "v2_drop_stale_rx();" in port
    assert "if (!ModbusEngineV2_Quiesce() ||" in port
    assert "ModbusEngineV2_Release(false, false);" in port
    assert "autobaud_candidate_observe(&k_e_ops" in board
    assert "autobaud_candidate_commit(&k_e_ops" in board
    assert "autobaud_e_start_dwell(" in board
    assert "autobaud_initial_window_trigger(" in board
    assert "ModbusEngineRuntime_ProcessFrame" not in board
    assert "ModbusEngineTx_Send" not in board
    assert "HAL_UART_Transmit" not in board
    assert "osDelay" not in board
    assert "PyxisAutobaudFraming_" not in task
    assert "MODBUS_AUTOBAUD_V2_FRAMING_ENABLE" not in (
        ROOT / "CMakePresets.json").read_text(encoding="utf-8")
    for source in list(INC.glob("*.h")) + list(SRC.glob("*.c")):
        contents = source.read_text(encoding="utf-8")
        for include in re.findall(
                r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', contents, re.M):
            lower = include.lower()
            assert not any(s in lower for s in (
                "stm32", "cmsis", "freertos", "gpio", "usart",
                "main.h", "portserial", "modbus_engine_runtime",
                "modbus_engine_port", "modbus_engine_tx", "hal_"
            )), (source, include)


def compile_and_run(cc, directory, name, code_sources, test):
    exe = Path(directory) / (
        name + ".exe" if os.name == "nt" else name)
    cmd = [
        cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-pedantic", "-DMODBUS_AUTOBAUD_V2_FRAMING_ENABLE=1",
        "-I", str(INC), "-I", str(BSP),
        "-I", str(MB_INC),
    ]
    cmd += [str(SRC / f) for f in code_sources]
    cmd += [str(MB_SRC / "modbus_crc.c")]
    if name == "board_port":
        cmd += [str(BSP / "pyxis_autobaud_framing_stm32h563.c")]
    cmd += [str(TESTS / test), "-o", str(exe)]
    subprocess.run(cmd, check=True, cwd=ROOT)
    subprocess.run([str(exe)], check=True, cwd=ROOT)


def main():
    check_scoping()
    subprocess.run([sys.executable,
                    str(ROOT / "tests/test_pyxis_autobaud_v2_m5.py")],
                   check=True, cwd=ROOT)
    cc = os.environ.get("CC") or next(
        (x for x in ("cc", "gcc", "clang") if shutil.which(x)), None)
    if cc is None:
        raise RuntimeError("No native C11 compiler: set CC")
    with tempfile.TemporaryDirectory(prefix="pyxis-v2-m6-") as tmp:
        compile_and_run(cc, tmp, "initial",
                        ["autobaud_activity.c", "autobaud_initial_window.c"],
                        "test_initial_window.c")
        compile_and_run(cc, tmp, "titania_framing", CORE,
                        "test_framing.c")
        compile_and_run(cc, tmp, "board_port",
                        CORE + ["autobaud_activity.c",
                                "autobaud_initial_window.c"],
                        "test_pyxis_framing_port.c")
    print("Pyxis Autobaud V2 M6 native passive framing suite: PASS")


if __name__ == "__main__":
    main()
