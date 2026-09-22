################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/subdir.mk
-include Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/subdir.mk
-include Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/subdir.mk
-include Middlewares/Third_Party/FreeRTOS/Source/subdir.mk
-include Drivers/STM32H5xx_HAL_Driver/Src/subdir.mk
-include Drivers/IIS3DWB/subdir.mk
-include Drivers/Flash_Storage/subdir.mk
-include Core/Startup/subdir.mk
-include Core/Src/subdir.mk
-include App/Product/Validation/subdir.mk
-include App/Product/RegisterMap/Modbus/subdir.mk
-include App/Middleware/FreeModbus/port/cmsis_rtos2/subdir.mk
-include App/Middleware/FreeModbus/port/subdir.mk
-include App/Middleware/FreeModbus/modbus/rtu/subdir.mk
-include App/Middleware/FreeModbus/modbus/functions/subdir.mk
-include App/Middleware/FreeModbus/modbus/subdir.mk
-include App/Middleware/EEPROM_Emul/Porting/STM32H5/subdir.mk
-include App/Middleware/EEPROM_Emul/Core/subdir.mk
-include App/Libraries/DSP/TransformFunctions/subdir.mk
-include App/Libraries/DSP/StatisticsFunctions/subdir.mk
-include App/Libraries/DSP/ComplexMathFunctions/subdir.mk
-include App/Libraries/DSP/CommonTables/subdir.mk
-include App/Libraries/DSP/BasicMathFunctions/subdir.mk
-include App/BSP/Storage/subdir.mk
-include App/BSP/Sensors/IIS3DWB/subdir.mk
-include App/Application/Tasks/subdir.mk
-include App/Application/Services/Vibration/subdir.mk
-include App/Application/Ports/subdir.mk
-include App/Application/Modules/Vibration/subdir.mk
-include App/Application/Modules/Modbus/subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

OPTIONAL_TOOL_DEPS := \
$(wildcard ../makefile.defs) \
$(wildcard ../makefile.init) \
$(wildcard ../makefile.targets) \


BUILD_ARTIFACT_NAME := Vimot_H563RITX_Professional
BUILD_ARTIFACT_EXTENSION := elf
BUILD_ARTIFACT_PREFIX :=
BUILD_ARTIFACT := $(BUILD_ARTIFACT_PREFIX)$(BUILD_ARTIFACT_NAME)$(if $(BUILD_ARTIFACT_EXTENSION),.$(BUILD_ARTIFACT_EXTENSION),)

# Add inputs and outputs from these tool invocations to the build variables 
EXECUTABLES += \
Vimot_H563RITX_Professional.elf \

MAP_FILES += \
Vimot_H563RITX_Professional.map \

SIZE_OUTPUT += \
default.size.stdout \

OBJDUMP_LIST += \
Vimot_H563RITX_Professional.list \

OBJCOPY_BIN += \
Vimot_H563RITX_Professional.bin \


# All Target
all:
	+@$(MAKE) --no-print-directory main-build && $(MAKE) --no-print-directory post-build

# Main-build Target
main-build: Vimot_H563RITX_Professional.elf secondary-outputs

# Tool invocations
Vimot_H563RITX_Professional.elf Vimot_H563RITX_Professional.map: $(OBJS) $(USER_OBJS) C:\Users\anwar\PiLoT-V\STM32H563xx_FLASH.ld makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-gcc -o "Vimot_H563RITX_Professional.elf" @"objects.list" $(USER_OBJS) $(LIBS) -mcpu=cortex-m33 -T"C:\Users\anwar\PiLoT-V\STM32H563xx_FLASH.ld" --specs=nosys.specs -Wl,-Map="Vimot_H563RITX_Professional.map" -Wl,--gc-sections -static --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -Wl,--start-group -lc -lm -Wl,--end-group
	@echo 'Finished building target: $@'
	@echo ' '

default.size.stdout: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-size  $(EXECUTABLES)
	@echo 'Finished building: $@'
	@echo ' '

Vimot_H563RITX_Professional.list: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objdump -h -S $(EXECUTABLES) > "Vimot_H563RITX_Professional.list"
	@echo 'Finished building: $@'
	@echo ' '

Vimot_H563RITX_Professional.bin: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objcopy  -O binary $(EXECUTABLES) "Vimot_H563RITX_Professional.bin"
	@echo 'Finished building: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) Vimot_H563RITX_Professional.bin Vimot_H563RITX_Professional.elf Vimot_H563RITX_Professional.list Vimot_H563RITX_Professional.map default.size.stdout
	-@echo ' '

post-build:
	arm-none-eabi-objcopy -O ihex Vimot_H563RITX_Professional.elf Vimot_H563RITX_Professional.hex
	-@echo ' '

secondary-outputs: $(SIZE_OUTPUT) $(OBJDUMP_LIST) $(OBJCOPY_BIN)

fail-specified-linker-script-missing:
	@echo 'Error: Cannot find the specified linker script. Check the linker settings in the build configuration.'
	@exit 2

warn-no-linker-script-specified:
	@echo 'Warning: No linker script specified. Check the linker settings in the build configuration.'

.PHONY: all clean dependents main-build fail-specified-linker-script-missing warn-no-linker-script-specified post-build

-include ../makefile.targets
