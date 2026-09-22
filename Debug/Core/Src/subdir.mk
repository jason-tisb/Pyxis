################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/app_freertos.c \
../Core/Src/app_led.c \
../Core/Src/main.c \
../Core/Src/stm32h5xx_hal_msp.c \
../Core/Src/stm32h5xx_hal_timebase_tim.c \
../Core/Src/stm32h5xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32h5xx.c 

OBJS += \
./Core/Src/app_freertos.o \
./Core/Src/app_led.o \
./Core/Src/main.o \
./Core/Src/stm32h5xx_hal_msp.o \
./Core/Src/stm32h5xx_hal_timebase_tim.o \
./Core/Src/stm32h5xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32h5xx.o 

C_DEPS += \
./Core/Src/app_freertos.d \
./Core/Src/app_led.d \
./Core/Src/main.d \
./Core/Src/stm32h5xx_hal_msp.d \
./Core/Src/stm32h5xx_hal_timebase_tim.d \
./Core/Src/stm32h5xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32h5xx.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m33 -std=gnu11 -g3 -DDEBUG -DEEPROM_MAX_VIRTUAL_ID=1000 -DFEATURE_STORAGE_USE_RTOS=1 -DFLASH_LINES_128B=1 -DUSE_HAL_DRIVER -DSTM32H563xx -c -I../Core/Inc -I../Drivers/STM32H5xx_HAL_Driver/Inc -I../Drivers/STM32H5xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H5xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/Third_Party/ARM_CMSIS/PrivateInclude/ -I../Middlewares/Third_Party/ARM_CMSIS/Include/ -I"C:/Users/anwar/PiLoT-V/Middlewares/Third_Party/ARM_CMSIS/Source/CommonTables" -I"C:/Users/anwar/PiLoT-V/Middlewares/Third_Party/ARM_CMSIS/Source/TransformFunctions" -I../Middlewares/Third_Party/FreeRTOS/Source/include/ -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/ -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/ -I../Middlewares/Third_Party/CMSIS/RTOS2/Include/ -I"C:/Users/anwar/PiLoT-V/Drivers/IIS3DWB" -I"C:/Users/anwar/PiLoT-V/Drivers/Flash_Storage" -I../Middlewares/Third_Party/ARM_CMSIS/Include -I"C:/Users/anwar/PiLoT-V/App/Libraries/DSP/CommonTables" -I"C:/Users/anwar/PiLoT-V/App/Libraries/DSP/ComplexMathFunctions" -I"C:/Users/anwar/PiLoT-V/App/Libraries/DSP/TransformFunctions" -I"C:/Users/anwar/PiLoT-V/App/Libraries/DSP/StatisticsFunctions" -I"C:/Users/anwar/PiLoT-V/App/Libraries/DSP/BasicMathFunctions" -I"C:/Users/anwar/PiLoT-V/App/Application/Tasks" -I"C:/Users/anwar/PiLoT-V/App/Application/Modules/Modbus" -I"C:/Users/anwar/PiLoT-V/App/Application/Modules/Vibration" -I"C:/Users/anwar/PiLoT-V/App/Application/Ports" -I"C:/Users/anwar/PiLoT-V/App/Application/Services/Vibration" -I"C:/Users/anwar/PiLoT-V/App/BSP/Sensors/IIS3DWB" -I"C:/Users/anwar/PiLoT-V/App/BSP/Storage" -I"C:/Users/anwar/PiLoT-V/App/Product/Configurations" -I"C:/Users/anwar/PiLoT-V/App/Product/Validation" -I"C:/Users/anwar/PiLoT-V/App/Product/RegisterMap/Modbus" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus/port" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus/port/cmsis_rtos2" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus/modbus" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus/modbus/include" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus/modbus/functions" -I"C:/Users/anwar/PiLoT-V/App/Middleware/FreeModbus/modbus/rtu" -I"C:/Users/anwar/PiLoT-V/App/Middleware/EEPROM_Emul/Core" -I"C:/Users/anwar/PiLoT-V/App/Middleware/EEPROM_Emul/Porting" -I"C:/Users/anwar/PiLoT-V/App/Middleware/EEPROM_Emul/Porting/STM32H5" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/app_freertos.cyclo ./Core/Src/app_freertos.d ./Core/Src/app_freertos.o ./Core/Src/app_freertos.su ./Core/Src/app_led.cyclo ./Core/Src/app_led.d ./Core/Src/app_led.o ./Core/Src/app_led.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/stm32h5xx_hal_msp.cyclo ./Core/Src/stm32h5xx_hal_msp.d ./Core/Src/stm32h5xx_hal_msp.o ./Core/Src/stm32h5xx_hal_msp.su ./Core/Src/stm32h5xx_hal_timebase_tim.cyclo ./Core/Src/stm32h5xx_hal_timebase_tim.d ./Core/Src/stm32h5xx_hal_timebase_tim.o ./Core/Src/stm32h5xx_hal_timebase_tim.su ./Core/Src/stm32h5xx_it.cyclo ./Core/Src/stm32h5xx_it.d ./Core/Src/stm32h5xx_it.o ./Core/Src/stm32h5xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32h5xx.cyclo ./Core/Src/system_stm32h5xx.d ./Core/Src/system_stm32h5xx.o ./Core/Src/system_stm32h5xx.su

.PHONY: clean-Core-2f-Src

