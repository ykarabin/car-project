cmake_minimum_required(VERSION 3.22)
project(crsf_host_tests C)

enable_testing()
add_executable(crsf_test crsf_test.c ../components/elrs/crsf.c)
target_include_directories(crsf_test PRIVATE ../components/elrs)
target_compile_features(crsf_test PRIVATE c_std_11)
target_compile_options(crsf_test PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
add_test(NAME crsf_channels COMMAND crsf_test)

add_executable(pwm_rc_test pwm_rc_test.c ../components/pwm_rc/pwm_decoder.c
               ../components/remote_uart/remote_input.c)
target_include_directories(pwm_rc_test PRIVATE ../components/pwm_rc ../components/pwm_rc/include
                           ../components/remote_uart/include ../../../shared/remote_protocol)
target_compile_features(pwm_rc_test PRIVATE c_std_11)
target_compile_options(pwm_rc_test PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
add_test(NAME pwm_capture_and_freshness COMMAND pwm_rc_test)

add_executable(remote_source_test remote_source_test.c
               ../components/remote_uart/remote_source.c ../components/remote_uart/remote_input.c)
target_include_directories(remote_source_test PRIVATE ../components/remote_uart/include
                           ../../../shared/remote_protocol)
target_compile_features(remote_source_test PRIVATE c_std_11)
target_compile_options(remote_source_test PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
add_test(NAME source_selection_and_mode_button COMMAND remote_source_test)

add_executable(gateway_runtime_test gateway_runtime_test.c ../main/main.c
               ../components/remote_uart/remote_source.c ../components/remote_uart/remote_input.c
               ../../../shared/remote_protocol/remote_protocol.c)
add_executable(elrs_runtime_test elrs_runtime_test.c ../components/elrs/elrs.c
               ../components/elrs/crsf.c)
foreach(target gateway_runtime_test elrs_runtime_test)
    target_include_directories(${target} PRIVATE fakes ../components/elrs/include
                               ../components/pwm_rc/include ../components/remote_uart/include
                               ../../../shared/remote_protocol)
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -pedantic)
endforeach()
add_test(NAME gateway_runtime_io_recovery COMMAND gateway_runtime_test)
add_test(NAME elrs_runtime_uart_recovery COMMAND elrs_runtime_test)
