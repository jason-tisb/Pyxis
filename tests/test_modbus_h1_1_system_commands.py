"""Host checks for one-shot System Command actions."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAP_H = ROOT / "App/Product/RegisterMap/Modbus/modbus_map.h"
MAP_C = ROOT / "App/Product/RegisterMap/Modbus/modbus_map.c"
RUNTIME_H = ROOT / "App/Middleware/ModbusEngine/Inc/modbus_engine_runtime.h"
RUNTIME_C = ROOT / "App/Middleware/ModbusEngine/Src/modbus_engine_runtime.c"
TASK_C = ROOT / "App/Application/Tasks/task_modbus.c"
PERSIST_C = ROOT / "App/Middleware/ModbusEngine/Src/modbus_persistence.c"


def test_canonical_command_contract():
    source = MAP_H.read_text(encoding="utf-8")
    assert "PYXIS_SYSTEM_COMMAND_NONE" in source and "0x0000U" in source
    assert "PYXIS_SYSTEM_COMMAND_RESTART" in source and "0x0001U" in source
    assert "PYXIS_SYSTEM_COMMAND_SYSTEM_RESET     0x0002U" in source
    assert "PYXIS_SYSTEM_COMMAND_CAPTURE_BASELINE 0x0004U" in source
    assert "PYXIS_SYSTEM_COMMAND_RESET_BASELINE   0x0005U" in source
    assert "PYXIS_SYSTEM_COMMAND_FACTORY_RESET" in source and "0xDEADU" in source
    assert '"system_command", 5U, 1U' in source
    assert '"gear_ratio", 12544U, 1U' in source
    assert '"remapping", 12672U, 125U' in source
    assert '"temperature", 2624U, 2U' in source
    assert '"x_vibration", 2640U, 50U' in source
    assert '"y_vibration", 2736U, 50U' in source
    assert '"z_vibration", 2832U, 50U' in source
    assert '"analytics", 2928U, 30U' in source
    assert '"baseline_command"' not in source
    assert "MODBUS_MAP_PERSIST_VOLATILE" in source
    assert "MODBUS_APPLY_IMMEDIATE" in source
    assert "PYXIS_SYSTEM_COMMAND_SAVE_NOW" not in source


def test_only_approved_values_are_legal():
    source = MAP_C.read_text(encoding="utf-8")
    assert "value == PYXIS_SYSTEM_COMMAND_NONE" in source
    assert "value == PYXIS_SYSTEM_COMMAND_RESTART" in source
    assert "value == PYXIS_SYSTEM_COMMAND_SYSTEM_RESET" in source
    assert "value == PYXIS_SYSTEM_COMMAND_CAPTURE_BASELINE" in source
    assert "value == PYXIS_SYSTEM_COMMAND_RESET_BASELINE" in source
    assert "value == PYXIS_SYSTEM_COMMAND_FACTORY_RESET" in source
    assert "value == PYXIS_SYSTEM_COMMAND_SAVE_NOW" not in source


def test_commands_are_staged_and_serviced_after_tx():
    runtime = RUNTIME_C.read_text(encoding="utf-8")
    task = TASK_C.read_text(encoding="utf-8")
    header = RUNTIME_H.read_text(encoding="utf-8")
    assert "MODBUS_SYSTEM_ACTION_RESTART = 1" in header
    assert "MODBUS_SYSTEM_ACTION_SYSTEM_RESET = 2" in header
    assert "MODBUS_SYSTEM_ACTION_FACTORY_RESET = 3" in header
    assert "MODBUS_SYSTEM_ACTION_CAPTURE_BASELINE = 4" in header
    assert "MODBUS_SYSTEM_ACTION_RESET_BASELINE = 5" in header
    assert "pending_system_action" in runtime
    assert "MODBUS_SYSTEM_ACTION_NONE" in runtime
    assert "ModbusEngineRuntime_ServiceSystemAction" in runtime
    assert "ModbusEngineTx_Send(response, response_length)" in task
    assert "ModbusEngineRuntime_GetPendingSystemAction" in task
    assert task.index("ModbusEngineTx_Send(response, response_length)") < task.index(
        "ModbusEngineRuntime_ServiceSystemAction()"
    )


def test_fc16_broadcast_and_legacy_migration_safety_are_present():
    runtime = RUNTIME_C.read_text(encoding="utf-8")
    persistence = PERSIST_C.read_text(encoding="utf-8")
    assert "Destructive commands are FC06-only" in runtime
    assert "Never execute destructive system commands from a broadcast frame" in runtime
    assert "ModbusPersistence_BlockLegacyConfig" in runtime
    assert "ModbusPersistence_IsLegacyConfigBlocked" in persistence


def test_factory_defaults_persist_before_live_defaults_are_published():
    runtime = RUNTIME_C.read_text(encoding="utf-8")
    assert "runtime_read_defaults" in runtime
    assert "ModbusPersistence_SaveFactoryDefaults" in runtime
    assert runtime.index("ModbusPersistence_SaveFactoryDefaults") < runtime.index(
        "ModbusEngineMap_RestoreHoldingDefaults"
    )
    assert "VibrationModule_ResetBaseline" in runtime


if __name__ == "__main__":
    for name, test in sorted(globals().items()):
        if name.startswith("test_"):
            test()
    print("modbus H1.1 system command tests: PASS")
