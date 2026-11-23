file(GLOB UI_SRCS "ui/*.c" "assets/*.c")
idf_component_register(
    SRCS "main.c" "board.c" ${UI_SRCS}
    INCLUDE_DIRS "." "ui" "assets"
    REQUIRES esp_lcd esp_driver_i2c esp_driver_gpio nvs_flash esp_timer)

idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl__lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
    target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
endif()
