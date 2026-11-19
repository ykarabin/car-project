idf_component_register(SRCS "remote_uart.c" "remote_input.c" "remote_source.c"
                       INCLUDE_DIRS "include"
                       REQUIRES remote_protocol
                       PRIV_REQUIRES esp_driver_uart esp_driver_gpio)
target_compile_options(${COMPONENT_LIB} PRIVATE
                       -Wconversion -Wsign-conversion -Wshadow -Wformat=2)
