idf_component_register(SRCS "elrs.c" "crsf.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_uart esp_driver_gpio esp_timer)

target_compile_options(${COMPONENT_LIB} PRIVATE
                       -Wconversion -Wsign-conversion -Wshadow -Wformat=2)
