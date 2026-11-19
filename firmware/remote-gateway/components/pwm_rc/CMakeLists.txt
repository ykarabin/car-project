idf_component_register(SRCS "pwm_rc.c" "pwm_decoder.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_mcpwm esp_driver_gpio esp_timer)
target_compile_options(${COMPONENT_LIB} PRIVATE
                       -Wconversion -Wsign-conversion -Wshadow -Wformat=2)
