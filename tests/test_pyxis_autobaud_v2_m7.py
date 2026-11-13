#!/usr/bin/env python3
"""M7 software verification: dormant-by-default, on-demand task control.

Runs full M0-M6 native regressions and compiles the *actual* M7 board
supervisor with native mocks for no-master, direct lock, capture/framing,
capture-timeout cooldown, startup BUSY budget and terminal UART fault.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
INC = ROOT / "App/Middleware/AutobaudV2/Inc"
SRC = ROOT / "App/Middleware/AutobaudV2/Src"
BSP = ROOT / "App/BSP/Communication"
MB_INC = ROOT / "App/Middleware/ModbusEngine/Inc"


def inspect_scope():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    cfg = (MB_INC / "modbus_engine_config.h").read_text(encoding="utf-8")
    task = (ROOT / "App/Application/Tasks/task_modbus.c").read_text(
        encoding="utf-8")
    module = (ROOT / "App/Application/Modules/Modbus/modbus_module.c").read_text(
        encoding="utf-8")
    coordinator = (BSP / "pyxis_autobaud_v2_stm32h563.c").read_text(
        encoding="utf-8")
    preset = (ROOT / "CMakePresets.json").read_text(encoding="utf-8")
    assert 'set(MODBUS_AUTOBAUD_V2_ENABLE "0" CACHE STRING' in cmake
    assert 'if(MODBUS_AUTOBAUD_V2_ENABLE STREQUAL "1")' in cmake
    assert "MODBUS_AUTOBAUD_V2_ENABLE=${MODBUS_AUTOBAUD_V2_ENABLE}" in cmake
    assert "#define MODBUS_AUTOBAUD_V2_ENABLE 0" in cfg
    assert "(MODBUS_AUTOBAUD_V2_ENABLE && MODBUS_AUTOBAUD_ENABLE)" in cfg
    assert "(MODBUS_AUTOBAUD_V2_ENABLE && !MODBUS_AUTOBAUD_V2_FRAMING_ENABLE)" in cfg
    assert "#if MODBUS_AUTOBAUD_ENABLE || MODBUS_AUTOBAUD_V2_ENABLE" in module
    assert 'v2_auto_selected = config_valid && (cfg.baud_rate == 0U)' in task
    assert "PYXIS_AUTOBAUD_V2_INITIAL_BAUD" in task
    assert "uart_startup_parity = v2_auto_selected ? 0U : cfg.parity;" in task
    assert "v2_auto_selected ? UART_STOPBITS_1" in task
    assert "if (v2_auto_selected && transport_ready)" in task
    assert "PyxisAutobaudV2_Service();" in task
    assert "if (!v2_auto_selected || PyxisAutobaudV2_Locked())" in task
    assert "if (transport_ready && (cfg.baud_rate == 0U))" in task  # legacy
    assert "#if !MODBUS_AUTOBAUD_ENABLE && !MODBUS_AUTOBAUD_V2_ENABLE" in task
    assert "PyxisAutobaudV2_Start();" not in task  # checked return
    assert "if (!PyxisAutobaudV2_Start())" in task
    assert "ModbusEngineRuntime_SetActiveCommConfig(&cfg)" in task
    assert "modbus_task_service_comm_persistence();" in task
    assert "VibrationAdapter_CheckConfigUpdate(" in task
    assert "MODBUS_AUTOBAUD_V2_ENABLE" not in preset

    # Explicit behavior protection: pure M3 supervisor, board adaptation
    # is outside portable middleware, and no sync capture or legacy scan.
    for name in ("autobaud_ondemand.c", "autobaud_score.c",
                 "autobaud_v2_framing.c"):
        pure = (SRC / name).read_text(encoding="utf-8")
        assert "stm32" not in pure.lower()
        assert "HAL_UART" not in pure
        assert "osDelay" not in pure
    assert "autobaud_od_service(&s_supervisor);" in coordinator
    assert "PyxisAutobaudCapture_Begin(budget)" in coordinator
    assert "PyxisAutobaudCapture_Poll(measurement, &status)" in coordinator
    assert "PyxisAutobaudFraming_ProbeInitial(profile)" in coordinator
    assert "PyxisAutobaudFraming_Start(measurement, budget, dwell)" in coordinator
    assert "PyxisAutobaudFraming_ResetInitial();" in coordinator
    assert "PyxisAutobaudFraming_Service();" in coordinator
    assert "ModbusEngineV2_LatchFatal();" in coordinator
    assert "autobaud_uart_txn_capture(" not in coordinator
    assert "ModbusAutobaudStartup_Start" not in coordinator
    assert "ModbusEngineRuntime_ProcessFrame" not in coordinator
    assert "HAL_Delay" not in coordinator and "osDelay" not in coordinator


def native_coordinator():
    cc = os.environ.get("CC") or next(
        (v for v in ("cc", "gcc", "clang") if shutil.which(v)), None)
    if cc is None:
        raise RuntimeError("No native C11 compiler found; set CC")

    with tempfile.TemporaryDirectory(prefix="pyxis-v2-m7-") as directory:
        exe = Path(directory) / (
            "m7_coordinator.exe" if os.name == "nt" else "m7_coordinator")
        cmd = [
            cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-pedantic", "-I", str(INC), "-I", str(BSP),
            "-I", str(MB_INC),
            str(SRC / "autobaud_v2.c"),
            str(SRC / "autobaud_ondemand.c"),
            str(BSP / "pyxis_autobaud_v2_stm32h563.c"),
            str(ROOT / "tests/autobaud_v2/test_pyxis_v2_coordinator.c"),
            "-o", str(exe)
        ]
        subprocess.run(cmd, check=True, cwd=ROOT)
        for scenario in range(6):
            subprocess.run([str(exe), str(scenario)], check=True, cwd=ROOT)


def main():
    inspect_scope()
    subprocess.run(
        [sys.executable, str(ROOT / "tests/test_pyxis_autobaud_v2_m6.py")],
        check=True, cwd=ROOT)
    native_coordinator()
    print("Pyxis Autobaud V2 M7 software and inherited regressions: PASS")


if __name__ == "__main__":
    main()
