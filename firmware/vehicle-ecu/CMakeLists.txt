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
set(CMAKE_PROJECT_NAME vehicle-ecu)

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
add_subdirectory(../../shared/remote_protocol "${CMAKE_CURRENT_BINARY_DIR}/remote-protocol")

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/debug_console.c
    Core/Src/vehicle_log.c
    Core/Src/vehicle_log_format.c
    Core/Src/motor_control.c
    Core/Src/fault_shutdown.c
    Core/Src/motor_safety.c
    Core/Src/control_watchdog.c
    Core/Src/mt6701_pwm.c
    Core/Src/steering_encoder.c
    Core/Src/steering_calibration.c
    Core/Src/steering_calibration_storage.c
    Core/Src/vehicle_control.c
    Core/Src/steering_controller.c
    Core/Src/board_controls.c
    Core/Src/drive_controller.c
    Core/Src/wheel_speed.c
    Core/Src/timebase_us.c
    Core/Src/rear_encoders.c
    Core/Src/remote_command_monitor.c
    Core/Src/vehicle_app.c
    Core/Src/remote_uart.c
    ThirdParty/SEGGER_RTT/SEGGER_RTT.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ThirdParty/SEGGER_RTT
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    remote_protocol

    # Add user defined libraries
)
