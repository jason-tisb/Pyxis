################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/Middleware/EEPROM_Emul/Core/eeprom_emul.c 

OBJS += \
./App/Middleware/EEPROM_Emul/Core/eeprom_emul.o 

C_DEPS += \
./App/Middleware/EEPROM_Emul/Core/eeprom_emul.d 


# Each subdirectory must supply rules for building sources it contributes
App/Middleware/EEPROM_Emul/Core/%.o App/Middleware/EEPROM_Emul/Core/%.su App/Middleware/EEPROM_Emul/Core/%.cyclo: ../App/Middleware/EEPROM_Emul/Core/%.c App/Middleware/EEPROM_Emul/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m33 -std=gnu11 -g3 -DDEBUG -DEEPROM_MAX_VIRTUAL_ID=1000 -DFEATURE_STORAGE_USE_RTOS=1 -DFLASH_LINES_128B=1 -DUSE_HAL_DRIVER -DSTM32H563xx -c -I../Core/Inc -I../Drivers/STM32H5xx_HAL_Driver/Inc -I../Drivers/STM32H5xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H5xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/Third_Party/ARM_CMSIS/PrivateInclude/ -I../Middlewares/Third_Party/ARM_CMSIS/Include/ -I"C:/Users/anwar/PiLoT-V/Middlewares/Third_Party/ARM_CMSIS/Source/CommonTables" -I"C:/Users/anwar/PiLoT-V/Middlewares/Third_Party/ARM_CMSIS/Source/TransformFunctions" -I../Middlewares/Third_Party/FreeRTOS/Source/include/ -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/ -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/ -I../Middlewares/Third_Party/CMSIS/RTOS2/Include/ -I"C:/Users/anwar/PiLoT-V/Drivers/IIS3DWB" -I"C:/Users/anwar/PiLoT-V/Drivers/Flash_Storage" -I../Middlewares/Third_Party/ARM_CMSIS/Include -I"C:/Users/anwar/PiLoT-V/App/Libraries/DSP/CommonTables" -I"C:/Users/anwar/PiLoT-V/App/Libraries/DSP/ComplexMathFunctions" -I"C:/Users/anwar/PiLoT-V/App/Libraries/DSP/TransformFunctions" -I"C:/Users/anwar/PiLoT-V/App/Libraries/DSP/StatisticsFunctions" -I"C:/Users/anwar/PiLoT-V/App/Libraries/DSP/BasicMathFunctions" -I"C:/Users/anwar/PiLoT-V/App/Application/Tasks" -I"C:/Users/anwar/PiLoT-V/App/Application/Modules/Modbus" -I"C:/Users/anwar/PiLoT-V/App/Application/Modules/Vibration" -I"C:/Users/anwar/PiLoT-V/App/Application/Ports" -I"C:/Users/anwar/PiLoT-V/App/Application/Services/Vibration" -I"C:/Users/anwar/PiLoT-V/App/BSP/Sensors/IIS3DWB" -I"C:/Users/anwar/PiLoT-V/App/BSP/Storage" -I"C:/Users/anwar/PiLoT-V/App/Product/Configurations" -I"C:/Users/anwar/PiLoT-V/App/Product/Validation" -I"C:/Users/anwar/PiLoT-V/App/Product/RegisterMap/Modbus" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus/port" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus/port/cmsis_rtos2" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus/modbus" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus/modbus/include" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus/modbus/functions" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus/modbus/rtu" -I"C:/Users/anwar/PiLoT-V/App/Middleware/EEPROM_Emul/Core" -I"C:/Users/anwar/PiLoT-V/App/Middleware/EEPROM_Emul/Porting" -I"C:/Users/anwar/PiLoT-V/App/Middleware/EEPROM_Emul/Porting/STM32H5" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-Middleware-2f-EEPROM_Emul-2f-Core

clean-App-2f-Middleware-2f-EEPROM_Emul-2f-Core:
	-$(RM) ./App/Middleware/EEPROM_Emul/Core/eeprom_emul.cyclo ./App/Middleware/EEPROM_Emul/Core/eeprom_emul.d ./App/Middleware/EEPROM_Emul/Core/eeprom_emul.o ./App/Middleware/EEPROM_Emul/Core/eeprom_emul.su

.PHONY: clean-App-2f-Middleware-2f-EEPROM_Emul-2f-Core

