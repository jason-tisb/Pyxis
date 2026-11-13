#!/usr/bin/env python3
"""M5 software-only verification for TIM2/PC4 async edge capture.

Runs existing M0-M4 suites, native edge-stream+scorer tests and strict
source isolation checks. ARM syntax compilation runs in the M5 CI job.
Hardware electrical/interrupt performance is explicitly not tested.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
V2 = ROOT / "App/Middleware/AutobaudV2"
BSP = ROOT / "App/BSP/Communication"


def audit_scope():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    cfg = (ROOT / "App/Middleware/ModbusEngine/Inc/modbus_engine_config.h").read_text(
        encoding="utf-8")
    task = (ROOT / "App/Application/Tasks/task_modbus.c").read_text(
        encoding="utf-8")
    capture = (ROOT / "App/Middleware/ModbusEngine/Src/modbus_autobaud_capture.c").read_text(
        encoding="utf-8")
    irq = (ROOT / "Core/Src/stm32h5xx_it.c").read_text(encoding="utf-8")
    adapter = (BSP / "pyxis_autobaud_capture_stm32h563.c").read_text(
        encoding="utf-8")
    assert 'set(MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE "0" CACHE STRING' in cmake
    assert 'if(MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE STREQUAL "1")' in cmake
    assert "MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE=${MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE}" in cmake
    assert "#define MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE 0" in cfg
    assert "(MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE && !MODBUS_AUTOBAUD_V2_PORT_ENABLE)" in cfg
    assert "#if MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE" in capture
    assert "#if MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE" in irq
    assert "PyxisAutobaudCapture_OnIrqBeforeHal();" in irq
    assert "PyxisAutobaudCapture_OnCaptureIrq();" in capture
    assert "PYXIS_CAPTURE_CLOCK_HZ 10000000U" in adapter
    assert "HAL_TIM_IC_Start_IT" in adapter
    assert "HAL_TIM_IC_Stop_IT" in adapter
    assert "ModbusEngineV2_Claim()" in adapter
    assert "ModbusEngineV2_Quiesce()" in adapter
    assert "ModbusEngineV2_Apply(&s_previous)" in adapter
    assert "ModbusEngineV2_Verify(&s_previous, s_previous_passive)" in adapter
    assert "TIM_SR_CC4OF" in adapter and "TIM_SR_UIF" in adapter
    assert "autobaud_score_intervals" in adapter
    assert "osDelay" not in adapter
    assert "PyxisAutobaudCapture_" not in task
    assert "MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE" not in (
        ROOT / "CMakePresets.json").read_text(encoding="utf-8")


def native_capture():
    cc = os.environ.get("CC") or next(
        (c for c in ("cc", "gcc", "clang") if shutil.which(c)), None)
    if cc is None:
        raise RuntimeError("No native C11 compiler found; set CC")
    with tempfile.TemporaryDirectory(prefix="pyxis-m5-") as tmp:
        exe = Path(tmp) / ("edge.exe" if os.name == "nt" else "edge")
        cmd = [
            cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-pedantic", "-I", str(V2 / "Inc"),
            str(V2 / "Src/autobaud_edge_stream.c"),
            str(V2 / "Src/autobaud_score.c"),
            str(ROOT / "tests/autobaud_v2/test_edge_stream.c"),
            "-o", str(exe)
        ]
        subprocess.run(cmd, check=True, cwd=ROOT)
        subprocess.run([str(exe)], check=True, cwd=ROOT)


def main():
    audit_scope()
    subprocess.run(
        [sys.executable, str(ROOT / "tests/test_pyxis_autobaud_v2_m4.py")],
        check=True, cwd=ROOT
    )
    native_capture()
    print("Pyxis Autobaud V2 M5 portable TIM2/software regression: PASS")


if __name__ == "__main__":
    main()
