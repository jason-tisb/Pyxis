cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Vimot_H563RITX_Professional)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Application core
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Application/Tasks/task_vibration.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Application/Tasks/task_modbus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Application/Ports/sensor_bus_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Application/Services/Vibration/vibration_algorithm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Application/Services/Vibration/bearing_freq_calculation.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Application/Modules/Modbus/modbus_module.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Application/Modules/Vibration/vibration_module.c

    # Hardware-specific implementations
    ${CMAKE_CURRENT_SOURCE_DIR}/App/BSP/Storage/storage_kv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/BSP/Storage/sys_storage_partition.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/BSP/Sensors/IIS3DWB/iis3dwb_sensor.c

    # Product-specific mappings
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Product/RegisterMap/Modbus/modbus_app_adapter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Product/RegisterMap/Modbus/vibration_modbus_adapter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Product/Validation/system_validator.c
    # External sensor drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/IIS3DWB/iis3dwb_reg.c
    # ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/main.c
    # FreeModbus middleware
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/mb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/functions/mbfunccoils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/functions/mbfuncdiag.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/functions/mbfuncdisc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/functions/mbfuncholding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/functions/mbfuncinput.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/functions/mbfuncother.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/functions/mbutils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/rtu/mbrtu.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/rtu/mbcrc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/port/user_mb_app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/port/cmsis_rtos2/portcritical.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/port/cmsis_rtos2/portevent.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/FreeModbus/port/cmsis_rtos2/porttimer.c

    # EEPROM emulation middleware
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/EEPROM_Emul/Core/eeprom_emul.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middleware/EEPROM_Emul/Porting/STM32H5/flash_interface.c
)

# Add include paths
target_include_directories(${PROJECT_NAME} PRIVATE
    # Core & HAL
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32H5xx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32H5xx_HAL_Driver/Inc/Legacy
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H5xx/Include
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
    
    # CMSIS DSP & Third Party
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/ARM_CMSIS/PrivateInclude
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/ARM_CMSIS/Include
     ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/ARM_CMSIS/Include/dsp
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/ARM_CMSIS/Source/CommonTables
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/ARM_CMSIS/Source/TransformFunctions
    
    # Custom DSP components
    ${CMAKE_SOURCE_DIR}/App/Libraries/DSP/CommonTables
    ${CMAKE_SOURCE_DIR}/App/Libraries/DSP/ComplexMathFunctions
    ${CMAKE_SOURCE_DIR}/App/Libraries/DSP/TransformFunctions
    ${CMAKE_SOURCE_DIR}/App/Libraries/DSP/StatisticsFunctions
    ${CMAKE_SOURCE_DIR}/App/Libraries/DSP/BasicMathFunctions
    
    # FreeRTOS
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/include
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/CMSIS/RTOS2/Include
    
    # Application code
    ${CMAKE_SOURCE_DIR}/App/Application/Tasks
    ${CMAKE_SOURCE_DIR}/App/Application/Modules/Modbus
    ${CMAKE_SOURCE_DIR}/App/Application/Modules/Vibration
    ${CMAKE_SOURCE_DIR}/App/Application/Ports
    ${CMAKE_SOURCE_DIR}/App/Application/Services/Vibration
    ${CMAKE_SOURCE_DIR}/App/BSP/Sensors/IIS3DWB
    ${CMAKE_SOURCE_DIR}/App/BSP/Storage
    ${CMAKE_SOURCE_DIR}/App/Product/Configurations
    ${CMAKE_SOURCE_DIR}/App/Product/Validation
    ${CMAKE_SOURCE_DIR}/App/Product/RegisterMap/Modbus
   

    # FreeModbus middleware
    ${CMAKE_SOURCE_DIR}/App/Middleware/FreeModbus
    ${CMAKE_SOURCE_DIR}/App/Middleware/FreeModbus/port
    ${CMAKE_SOURCE_DIR}/App/Middleware/FreeModbus/port/cmsis_rtos2
    ${CMAKE_SOURCE_DIR}/App/Middleware/FreeModbus/modbus
    ${CMAKE_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/include
    ${CMAKE_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/functions
    ${CMAKE_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/ascii
    ${CMAKE_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/rtu
    ${CMAKE_SOURCE_DIR}/App/Middleware/FreeModbus/modbus/tcp

    # Drivers
    ${CMAKE_SOURCE_DIR}/Drivers/IIS3DWB
    ${CMAKE_SOURCE_DIR}/Drivers/Flash_Storage
    
    # EEPROM Emulation
    ${CMAKE_SOURCE_DIR}/App/Middleware/EEPROM_Emul/Core
    ${CMAKE_SOURCE_DIR}/App/Middleware/EEPROM_Emul/Porting
    ${CMAKE_SOURCE_DIR}/App/Middleware/EEPROM_Emul/Porting/STM32H5
)
# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
   DEBUG
    EEPROM_MAX_VIRTUAL_ID=1000
    FEATURE_STORAGE_USE_RTOS=1
    FLASH_LINES_128B=1
    USE_HAL_DRIVER
    STM32H563xx
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -mcpu=cortex-m33
    -mthumb
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
    -std=gnu11
    -g3
    -O0
    -Wall
    -ffunction-sections
    -fdata-sections
    -fstack-usage
    -fcyclomatic-complexity
)

# Linker specs and core flags are already provided by STM32Cube/toolchain settings.
# Avoid re-adding nano.specs here, which causes duplicate spec registration on GCC 14.

# Generate additional firmware images next to the .elf (e.g. .bin for flashing tools).
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/$<TARGET_FILE_BASE_NAME:${CMAKE_PROJECT_NAME}>.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/$<TARGET_FILE_BASE_NAME:${CMAKE_PROJECT_NAME}>.bin
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    VERBATIM
)
