#!/usr/bin/env python3
"""M0 read-only source-policy audit. Run from any cwd; no dependencies.

This does not compile the firmware, exercise Modbus, or use physical hardware.
It intentionally fails if the expected baseline policy drifts before M1.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def source(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


class PyxisAutobaudV2M0(unittest.TestCase):
    def test_reproducible_baseline_files(self):
        for path in (
            "CMakeLists.txt",
            "CMakePresets.json",
            "App/Application/Tasks/task_modbus.c",
            "App/Application/Tasks/task_vibration.c",
            "App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c",
            "App/Middleware/ModbusEngine/Src/modbus_autobaud_startup.c",
            "App/Middleware/ModbusEngine/Inc/modbus_engine_hw_config.h",
        ):
            with self.subTest(path=path):
                self.assertTrue((ROOT / path).is_file(), path)

    def test_default_build_switches(self):
        cmake = source("CMakeLists.txt")
        expected = {
            "MODBUS_ENGINE_ACTIVE": "1",
            "MODBUS_ENGINE_FORCE_FIXED_COMM": "0",
            "MODBUS_AUTOBAUD_ENABLE": "0",
            "MODBUS_PYXIS_DMA_ABSOLUTE": "0",
            "MODBUS_PYXIS_HW_RTO": "0",
            "MODBUS_PYXIS_TX_IT": "1",
        }
        for name, default in expected.items():
            with self.subTest(name=name):
                pattern = rf'(?m)^set\({name}\s+"{default}"\s+CACHE\s+STRING\b'
                self.assertRegex(cmake, pattern)
        header = source(
            "App/Middleware/ModbusEngine/Inc/modbus_engine_config.h")
        self.assertRegex(
            header, r"#ifndef\s+MODBUS_AUTOBAUD_ENABLE\s*\n#define\s+MODBUS_AUTOBAUD_ENABLE\s+0\b")

    def test_current_default_recovery_profile(self):
        module = source("App/Application/Modules/Modbus/modbus_module.c")
        for assignment in (
            "s_mb_cfg.slave_id = 3U",
            "s_mb_cfg.baud_rate = 9600U",
            "s_mb_cfg.parity = 3U",
        ):
            self.assertIn(assignment, module)

    def test_current_startup_coordinator_is_opt_in(self):
        task = source("App/Application/Tasks/task_modbus.c")
        self.assertIn("ModbusAutobaudStartup_Start();", task)
        self.assertIn("ModbusAutobaudStartup_Service()", task)
        self.assertIn("if (transport_ready && (cfg.baud_rate == 0U))", task)
        self.assertIn("#if MODBUS_AUTOBAUD_ENABLE", task)
        # M0 does not install a second engine or alter the loop.
        self.assertIn("#if MODBUS_AUTOBAUD_V2_ENABLE", task)

    def test_current_hardware_and_transport_contract(self):
        hw = source(
            "App/Middleware/ModbusEngine/Inc/modbus_engine_hw_config.h")
        for token in ("USART3", "GPDMA1_Channel2", "GPIO_PIN_14"):
            with self.subTest(token=token):
                self.assertIn(token, hw)
        serial = source(
            "App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c")
        self.assertIn("HAL_UARTEx_ReceiveToIdle_DMA", serial)
        capture = source(
            "App/Middleware/ModbusEngine/Inc/modbus_autobaud_capture.h")
        self.assertIn("TIM2_CH4", capture)
        self.assertIn("PC4", capture)

    def test_known_57600_config_discrepancy_is_tracked(self):
        module = source("App/Application/Modules/Modbus/modbus_module.c")
        scorer = source(
            "App/Middleware/ModbusEngine/Src/modbus_autobaud_score.c")
        match = re.search(
            r"static bool is_allowed_baud_rate\(.*?\n\}",
            module, flags=re.S)
        self.assertIsNotNone(match)
        self.assertNotIn("case 57600U:", match.group(0))
        self.assertIn("57600U", scorer)
        self.assertIn(
            "57600",
            source("App/Docs/PYXIS_AUTOBAUD_V2_M0_DEPENDENCIES.md"))

    def test_no_new_v2_runtime_wiring(self):
        # M1 and later may introduce dormant portable sources. The M0
        # regression invariant is that the firmware target/task is untouched.
        self.assertIn(
            'set(MODBUS_AUTOBAUD_V2_ENABLE "0" CACHE STRING',
            source("CMakeLists.txt"))
        self.assertNotIn(
            "PyxisAutobaudPort_",
            source("App/Application/Tasks/task_modbus.c"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
