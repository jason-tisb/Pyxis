#!/usr/bin/env python3
"""M8 software-only acceptance for Titania-like Pyxis Autobaud V2.

Runs original M0-M7 regressions, 5 baud x 4 UART profiles x 3 master
poll intervals through a real RTU fragment assembler and CRC validator,
strict compile-time mode matrix, and runtime integration safety checks.
Physical RS485/UART/DMA/FFT qualification remains deferred.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "App/Middleware/AutobaudV2"
MB = ROOT / "App/Middleware/ModbusEngine"
TX = ROOT / "App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c"


def compiler():
    cc = os.environ.get("CC") or next(
        (x for x in ("cc", "gcc", "clang") if shutil.which(x)), None)
    if cc is None:
        raise RuntimeError("Native C11 compiler required: set CC")
    return cc


def audit_m8_source():
    serial = TX.read_text(encoding="utf-8")
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    m7 = (ROOT / "App/BSP/Communication/pyxis_autobaud_v2_stm32h563.c").read_text(
        encoding="utf-8")
    task = (ROOT / "App/Application/Tasks/task_modbus.c").read_text(
        encoding="utf-8")
    assert "autobaud_rtu_assembly.h" in serial
    assert "autobaud_rtu_quiet_ms(" in serial
    assert "uart3_rx_boundary_tick[" in serial
    assert "v2_publish_assembled_frame(" in serial
    assert "autobaud_rtu_assembly_append(" in serial
    assert "autobaud_rtu_assembly_ready(" in serial
    assert "autobaud_rtu_assembly_reset(&s_v2_assembly);" in serial
    assert "MODBUS_AUTOBAUD_V2_FRAMING_ENABLE" in serial
    assert "PYXIS_V2_FRAME_QUIET_MS" not in serial
    assert "autobaud_rtu_assembly.c" in cmake
    assert 'set(MODBUS_AUTOBAUD_V2_ENABLE "0" CACHE STRING' in cmake
    assert 'set(MODBUS_AUTOBAUD_V2_FRAMING_ENABLE "0" CACHE STRING' in cmake
    assert "ModbusEngineV2_TxAllowed()" in (
        ROOT / "App/Middleware/ModbusEngine/Src/modbus_engine_tx.c").read_text(
            encoding="utf-8")
    assert "autobaud_od_service(&s_supervisor);" in m7
    assert "autobaud_uart_txn_capture(" not in m7
    assert "ModbusAutobaudStartup_Service()" in task
    assert "#if MODBUS_AUTOBAUD_ENABLE" in task
    assert "if (!v2_auto_selected || PyxisAutobaudV2_Locked())" in task
    assert "VibrationAdapter_CheckConfigUpdate(" in task
    assert "MODBUS_AUTOBAUD_V2_ENABLE" not in (
        ROOT / "CMakePresets.json").read_text(encoding="utf-8")
    for f in CORE.rglob("*.c"):
        code = f.read_text(encoding="utf-8")
        assert '#include "main.h"' not in code, f
        assert '#include "stm32h5xx_hal.h"' not in code, f
        assert "osDelay(" not in code, f


def configure_matrix(cc):
    source = '#include "modbus_engine_config.h"\nint a;\n'
    inc = str(MB / "Inc")
    scenarios = [
        ("default-fixed", {}, True),
        ("v2-complete", dict(
            MODBUS_ENGINE_ACTIVE=1, MODBUS_AUTOBAUD_ENABLE=0,
            MODBUS_AUTOBAUD_V2_PORT_ENABLE=1,
            MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE=1,
            MODBUS_AUTOBAUD_V2_FRAMING_ENABLE=1,
            MODBUS_AUTOBAUD_V2_ENABLE=1), True),
        ("v2-without-dependencies",dict(
            MODBUS_AUTOBAUD_V2_ENABLE=1),False),
        ("v2-without-framing",dict(
            MODBUS_AUTOBAUD_V2_ENABLE=1,
            MODBUS_AUTOBAUD_V2_PORT_ENABLE=1,
            MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE=1),False),
        ("v2-plus-legacy",dict(
            MODBUS_AUTOBAUD_V2_ENABLE=1,
            MODBUS_AUTOBAUD_V2_PORT_ENABLE=1,
            MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE=1,
            MODBUS_AUTOBAUD_V2_FRAMING_ENABLE=1,
            MODBUS_AUTOBAUD_ENABLE=1),False),
        ("v2-plus-force-fixed",dict(
            MODBUS_AUTOBAUD_V2_ENABLE=1,
            MODBUS_AUTOBAUD_V2_PORT_ENABLE=1,
            MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE=1,
            MODBUS_AUTOBAUD_V2_FRAMING_ENABLE=1,
            MODBUS_ENGINE_FORCE_FIXED_COMM=1),False),
        ("v2-without-custom-engine",dict(
            MODBUS_AUTOBAUD_V2_ENABLE=1,
            MODBUS_AUTOBAUD_V2_PORT_ENABLE=1,
            MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE=1,
            MODBUS_AUTOBAUD_V2_FRAMING_ENABLE=1,
            MODBUS_ENGINE_ACTIVE=0),False),
        ("capture-without-uart",dict(
            MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE=1),False),
        ("framing-without-capture",dict(
            MODBUS_AUTOBAUD_V2_FRAMING_ENABLE=1,
            MODBUS_AUTOBAUD_V2_PORT_ENABLE=1),False),
        ("invalid-boolean",dict(
            MODBUS_AUTOBAUD_V2_ENABLE=2),False),
        ("legacy-only",dict(
            MODBUS_AUTOBAUD_ENABLE=1),True),
    ]
    for label, defs, expected in scenarios:
        args = [cc, "-std=c11", "-x", "c", "-fsyntax-only",
                "-I", inc, "-"]
        args += ["-D%s=%s" % (k,v) for k,v in defs.items()]
        result = subprocess.run(
            args, input=source, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, cwd=ROOT)
        assert (result.returncode == 0) == expected, (
            label, result.returncode, result.stderr[:1200])
    print("Pyxis M8 compile-time build-mode matrix: PASS (11 configurations)")


def test_fragments(cc):
    with tempfile.TemporaryDirectory(prefix="pyxis-v2-m8-") as work:
        exe = Path(work) / (
            "rtu_matrix.exe" if os.name == "nt" else "rtu_matrix")
        args = [cc, "-std=c11", "-O2", "-Wall", "-Wextra",
                "-Werror", "-pedantic", "-I", str(CORE / "Inc"),
                "-I", str(MB / "Inc"),
                str(CORE / "Src/autobaud_rtu_assembly.c"),
                str(CORE / "Src/autobaud_validate.c"),
                str(MB / "Src/modbus_crc.c"),
                str(ROOT / "tests/autobaud_v2/test_rtu_assembly.c"),
                "-o", str(exe)]
        subprocess.run(args, check=True, cwd=ROOT)
        subprocess.run([str(exe)], check=True, cwd=ROOT)


def main():
    audit_m8_source()
    cc = compiler()
    configure_matrix(cc)
    subprocess.run([sys.executable,
                    str(ROOT / "tests/test_pyxis_autobaud_v2_m7.py")],
                   check=True, cwd=ROOT)
    test_fragments(cc)
    print("Pyxis Autobaud V2 M8 software acceptance: PASS")


if __name__ == "__main__":
    main()
