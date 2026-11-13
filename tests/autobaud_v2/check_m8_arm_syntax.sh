#!/bin/sh
# M8: Cross-compile M4-M7 plus complete baud-aware passive fragment assembler.
# Does not link/flash or perform physical tests. Run from repository root.
set -eu
cd "$(dirname "$0")/../.."
: "${ARM_CC:=arm-none-eabi-gcc}"
"$ARM_CC" --version | head -n 1
incs="
-I./Core/Inc
-I./Drivers/STM32H5xx_HAL_Driver/Inc
-I./Drivers/STM32H5xx_HAL_Driver/Inc/Legacy
-I./Drivers/CMSIS/Device/ST/STM32H5xx/Include
-I./Drivers/CMSIS/Include
-I./Drivers/Flash_Storage
-I./Middlewares/Third_Party/FreeRTOS/Source/include
-I./Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure
-I./Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
-I./Middlewares/Third_Party/CMSIS/RTOS2/Include
-I./App/Application/Tasks
-I./App/Application/Modules/Modbus
-I./App/Application/Modules/Vibration
-I./App/Application/Ports
-I./App/BSP/Communication
-I./App/Middleware/AutobaudV2/Inc
-I./App/Middleware/ModbusEngine/Inc
-I./App/Middleware/FreeModbus
-I./App/Middleware/FreeModbus/port
-I./App/Middleware/FreeModbus/port/cmsis_rtos2
-I./App/Middleware/FreeModbus/modbus
-I./App/Middleware/FreeModbus/modbus/include
-I./App/Middleware/FreeModbus/modbus/functions
-I./App/Product/Configurations
-I./App/Product/RegisterMap/Modbus
-I./App/BSP/Sensors/IIS3DWB
-I./App/BSP/Storage
-I./App/Middleware/EEPROM_Emul/Core
-I./App/Middleware/EEPROM_Emul/Porting
-I./App/Middleware/EEPROM_Emul/Porting/STM32H5
"
# Word splitting is intentional: these are fixed repo-local include paths.
# shellcheck disable=SC2086
for file in \
  Core/Src/stm32h5xx_it.c \
  App/Middleware/ModbusEngine/Src/modbus_autobaud_capture.c \
  App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c \
  App/Middleware/ModbusEngine/Src/modbus_engine_tx.c \
  App/BSP/Communication/pyxis_autobaud_uart_stm32h563.c \
  App/BSP/Communication/pyxis_autobaud_capture_stm32h563.c \
  App/BSP/Communication/pyxis_autobaud_framing_stm32h563.c \
  App/BSP/Communication/pyxis_autobaud_v2_stm32h563.c \
  App/Application/Tasks/task_modbus.c \
  App/Application/Modules/Modbus/modbus_module.c \
  App/Middleware/AutobaudV2/Src/autobaud_ondemand.c \
  App/Middleware/AutobaudV2/Src/autobaud_activity.c \
  App/Middleware/AutobaudV2/Src/autobaud_initial_window.c \
  App/Middleware/AutobaudV2/Src/autobaud_validate.c \
  App/Middleware/AutobaudV2/Src/autobaud_v2_evidence.c \
  App/Middleware/AutobaudV2/Src/autobaud_rtu_assembly.c \
  App/Middleware/AutobaudV2/Src/autobaud_v2_candidate.c \
  App/Middleware/AutobaudV2/Src/autobaud_v2_framing.c \
  App/Middleware/ModbusEngine/Src/modbus_crc.c \
  App/Middleware/AutobaudV2/Src/autobaud_edge_stream.c \
  App/Middleware/AutobaudV2/Src/autobaud_score.c \
  App/Middleware/AutobaudV2/Src/autobaud_v2.c \
  App/Middleware/AutobaudV2/Src/autobaud_uart_transaction.c
do
  "$ARM_CC" -std=gnu11 -fsyntax-only -mcpu=cortex-m33 -mthumb \
      -mfpu=fpv5-sp-d16 -mfloat-abi=hard -Wall \
      -DSTM32H563xx -DUSE_HAL_DRIVER \
      -DMODBUS_ENGINE_ACTIVE=1 -DMODBUS_AUTOBAUD_ENABLE=0 \
      -DMODBUS_AUTOBAUD_V2_PORT_ENABLE=1 \
      -DMODBUS_AUTOBAUD_V2_CAPTURE_ENABLE=1 \
      -DMODBUS_AUTOBAUD_V2_FRAMING_ENABLE=1 \
      -DMODBUS_AUTOBAUD_V2_ENABLE=1 \
      -DMODBUS_PYXIS_TX_IT=1 -DMODBUS_PYXIS_HW_DE=0 \
      -DMODBUS_PYXIS_HW_RTO=0 -DMODBUS_PYXIS_DMA_ABSOLUTE=0 \
      $incs "$file"
done
echo "Pyxis Autobaud V2 M8 guarded RTU assembler and Modbus task ARM syntax: PASS"
