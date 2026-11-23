cmake_minimum_required(VERSION 3.22)
project(ride_on_car_host_tests C)
enable_testing()

add_executable(int32_math_test int32_math_test.c)
target_include_directories(int32_math_test PRIVATE ../firmware/vehicle-ecu/Core/Inc)
add_test(NAME int32_magnitude_bounds COMMAND int32_math_test)

add_subdirectory(../shared/remote_protocol remote-protocol)
add_executable(remote_protocol_test remote_protocol_test.c)
target_link_libraries(remote_protocol_test PRIVATE remote_protocol)
add_test(NAME remote_protocol COMMAND remote_protocol_test)

add_executable(debug_console_test debug_console_test.c
               ../firmware/vehicle-ecu/Core/Src/debug_console.c)
target_include_directories(debug_console_test PRIVATE fakes ../firmware/vehicle-ecu/Core/Inc)
target_include_directories(debug_console_test SYSTEM PRIVATE ../firmware/vehicle-ecu/ThirdParty/SEGGER_RTT)
add_test(NAME compact_rtt_timestamp_and_lines COMMAND debug_console_test)

add_executable(vehicle_log_test vehicle_log_test.c
               ../firmware/vehicle-ecu/Core/Src/vehicle_log.c
               ../firmware/vehicle-ecu/Core/Src/vehicle_log_format.c
               ../firmware/vehicle-ecu/Core/Src/debug_console.c
               ../firmware/vehicle-ecu/Core/Src/steering_calibration.c)
target_include_directories(vehicle_log_test PRIVATE fakes/uart fakes ../firmware/vehicle-ecu/Core/Inc)
target_include_directories(vehicle_log_test SYSTEM PRIVATE ../firmware/vehicle-ecu/ThirdParty/SEGGER_RTT)
add_test(NAME static_logger_queue_snapshot_and_timestamps COMMAND vehicle_log_test)

add_executable(remote_command_test remote_command_test.c
               ../firmware/vehicle-ecu/Core/Src/remote_command_monitor.c
               ../firmware/remote-gateway/components/remote_uart/remote_input.c)
target_include_directories(remote_command_test PRIVATE
                           ../firmware/vehicle-ecu/Core/Inc
                           ../firmware/remote-gateway/components/remote_uart/include)
target_link_libraries(remote_command_test PRIVATE remote_protocol)
add_test(NAME remote_command_flow COMMAND remote_command_test)

add_executable(remote_uart_test remote_uart_test.c
               ../firmware/vehicle-ecu/Core/Src/remote_uart.c
               ../firmware/vehicle-ecu/Core/Src/vehicle_app.c
               ../firmware/vehicle-ecu/Core/Src/vehicle_log_format.c
               ../firmware/vehicle-ecu/Core/Src/steering_calibration.c
               ../firmware/vehicle-ecu/Core/Src/remote_command_monitor.c)
target_include_directories(remote_uart_test PRIVATE fakes/uart fakes ../firmware/vehicle-ecu/Core/Inc)
target_link_libraries(remote_uart_test PRIVATE remote_protocol)
add_test(NAME remote_uart_order_overflow_and_recovery COMMAND remote_uart_test)

add_executable(fault_shutdown_test fault_shutdown_test.c
               ../firmware/vehicle-ecu/Core/Src/fault_shutdown.c
               ../firmware/vehicle-ecu/Core/Src/motor_safety.c
               ../firmware/vehicle-ecu/Core/Src/stm32g4xx_it.c)
target_include_directories(fault_shutdown_test PRIVATE fakes ../firmware/vehicle-ecu/Core/Inc)
target_link_libraries(fault_shutdown_test PRIVATE remote_protocol)
add_test(NAME fatal_fault_motor_shutdown COMMAND fault_shutdown_test)

add_executable(motor_safety_test motor_safety_test.c
               ../firmware/vehicle-ecu/Core/Src/motor_safety.c
               ../firmware/vehicle-ecu/Core/Src/stm32g4xx_it.c)
target_include_directories(motor_safety_test PRIVATE fakes ../firmware/vehicle-ecu/Core/Inc)
target_link_libraries(motor_safety_test PRIVATE remote_protocol)
foreach(scenario normal startup_low short_low pending_dedup pulse_train wake_pulse rolling_window independent restart_low transition_repeat transition_low)
    add_test(NAME motor_safety_${scenario} COMMAND motor_safety_test ${scenario})
endforeach()

add_executable(control_watchdog_test control_watchdog_test.c
               ../firmware/vehicle-ecu/Core/Src/control_watchdog.c)
target_include_directories(control_watchdog_test PRIVATE fakes ../firmware/vehicle-ecu/Core/Inc)
add_test(NAME control_watchdog_deadline_and_reset COMMAND control_watchdog_test)

foreach(variant default left_inverted right_inverted steering_inverted)
    set(target motor_control_${variant}_test)
    add_executable(${target} motor_control_test.c
                   ../firmware/vehicle-ecu/Core/Src/motor_safety.c
                   ../firmware/vehicle-ecu/Core/Src/motor_control.c)
    target_include_directories(${target} PRIVATE fakes ../firmware/vehicle-ecu/Core/Inc)
    if(variant STREQUAL "left_inverted")
        target_compile_definitions(${target} PRIVATE MOTOR_LEFT_DIRECTION_INVERTED=1
                                   MOTOR_RIGHT_DIRECTION_INVERTED=0
                                   MOTOR_STEERING_DIRECTION_INVERTED=0)
    elseif(variant STREQUAL "right_inverted")
        target_compile_definitions(${target} PRIVATE MOTOR_LEFT_DIRECTION_INVERTED=0
                                   MOTOR_RIGHT_DIRECTION_INVERTED=1
                                   MOTOR_STEERING_DIRECTION_INVERTED=0)
    elseif(variant STREQUAL "steering_inverted")
        target_compile_definitions(${target} PRIVATE MOTOR_LEFT_DIRECTION_INVERTED=0
                                   MOTOR_RIGHT_DIRECTION_INVERTED=0
                                   MOTOR_STEERING_DIRECTION_INVERTED=1)
    endif()
    add_test(NAME motor_control_${variant} COMMAND ${target})
    list(APPEND motor_test_targets ${target})
endforeach()

add_executable(mt6701_pwm_test mt6701_pwm_test.c
               ../firmware/vehicle-ecu/Core/Src/mt6701_pwm.c)
target_include_directories(mt6701_pwm_test PRIVATE ../firmware/vehicle-ecu/Core/Inc)
add_test(NAME mt6701_pwm COMMAND mt6701_pwm_test)

add_executable(steering_encoder_test steering_encoder_test.c
               ../firmware/vehicle-ecu/Core/Src/steering_encoder.c
               ../firmware/vehicle-ecu/Core/Src/mt6701_pwm.c)
target_include_directories(steering_encoder_test PRIVATE fakes ../firmware/vehicle-ecu/Core/Inc)
add_test(NAME steering_encoder_capture COMMAND steering_encoder_test)

add_executable(steering_calibration_test steering_calibration_test.c
               ../firmware/vehicle-ecu/Core/Src/steering_calibration.c)
target_include_directories(steering_calibration_test PRIVATE ../firmware/vehicle-ecu/Core/Inc)
add_test(NAME steering_calibration_geometry_and_record COMMAND steering_calibration_test)

add_executable(vehicle_control_test vehicle_control_test.c
               ../firmware/vehicle-ecu/Core/Src/vehicle_control.c
               ../firmware/vehicle-ecu/Core/Src/steering_calibration_service.c
               ../firmware/vehicle-ecu/Core/Src/vehicle_log_format.c
               ../firmware/vehicle-ecu/Core/Src/board_controls.c
               ../firmware/vehicle-ecu/Core/Src/drive_controller.c
               ../firmware/vehicle-ecu/Core/Src/steering_controller.c
               ../firmware/vehicle-ecu/Core/Src/steering_calibration.c)
target_include_directories(vehicle_control_test PRIVATE fakes ../firmware/vehicle-ecu/Core/Inc)
target_link_libraries(vehicle_control_test PRIVATE remote_protocol)
add_test(NAME vehicle_control_calibration_and_modes COMMAND vehicle_control_test)

add_executable(steering_controller_test steering_controller_test.c
               ../firmware/vehicle-ecu/Core/Src/steering_controller.c
               ../firmware/vehicle-ecu/Core/Src/steering_calibration.c)
target_include_directories(steering_controller_test PRIVATE ../firmware/vehicle-ecu/Core/Inc)
add_test(NAME steering_position_controller COMMAND steering_controller_test)

add_executable(drive_controller_test drive_controller_test.c
               ../firmware/vehicle-ecu/Core/Src/drive_controller.c)
target_include_directories(drive_controller_test PRIVATE ../firmware/vehicle-ecu/Core/Inc)
target_link_libraries(drive_controller_test PRIVATE remote_protocol)
add_test(NAME shared_drive_pi_and_modes COMMAND drive_controller_test)

add_executable(wheel_speed_test wheel_speed_test.c ../firmware/vehicle-ecu/Core/Src/wheel_speed.c)
target_include_directories(wheel_speed_test PRIVATE ../firmware/vehicle-ecu/Core/Inc)
add_test(NAME wheel_speed_units_wrap_and_timing COMMAND wheel_speed_test)

add_executable(timebase_us_test timebase_us_test.c
               ../firmware/vehicle-ecu/Core/Src/timebase_us.c)
target_include_directories(timebase_us_test PRIVATE fakes ../firmware/vehicle-ecu/Core/Inc)
add_test(NAME microsecond_timebase COMMAND timebase_us_test)

add_executable(board_controls_test board_controls_test.c
               ../firmware/vehicle-ecu/Core/Src/board_controls.c)
target_include_directories(board_controls_test PRIVATE fakes ../firmware/vehicle-ecu/Core/Inc)
add_test(NAME calibration_button_and_led COMMAND board_controls_test)

foreach(variant default right_inverted)
    set(target rear_encoders_${variant}_test)
    add_executable(${target} rear_encoders_test.c
                   ../firmware/vehicle-ecu/Core/Src/rear_encoders.c
                   ../firmware/vehicle-ecu/Core/Src/wheel_speed.c)
    target_include_directories(${target} PRIVATE fakes ../firmware/vehicle-ecu/Core/Inc)
    if(variant STREQUAL "right_inverted")
        target_compile_definitions(${target} PRIVATE REAR_ENCODER_RIGHT_INVERTED=1)
    endif()
    add_test(NAME rear_encoders_${variant} COMMAND ${target})
    list(APPEND rear_encoder_test_targets ${target})
endforeach()

foreach(target int32_math_test remote_protocol remote_protocol_test debug_console_test vehicle_log_test remote_command_test remote_uart_test ${motor_test_targets}
               fault_shutdown_test motor_safety_test control_watchdog_test
               mt6701_pwm_test steering_encoder_test steering_calibration_test vehicle_control_test
               steering_controller_test drive_controller_test wheel_speed_test timebase_us_test
               board_controls_test
               ${rear_encoder_test_targets})
    set_target_properties(${target} PROPERTIES
                          C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_compile_options(${target} PRIVATE
                           -Wall -Wextra -Werror -Wconversion -Wsign-conversion -pedantic)
endforeach()

add_subdirectory(../firmware/remote-gateway/tests crsf)
