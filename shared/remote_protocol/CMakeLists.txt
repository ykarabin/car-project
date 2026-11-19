if(ESP_PLATFORM)
    idf_component_register(SRCS "remote_protocol.c" INCLUDE_DIRS ".")
    return()
endif()

add_library(remote_protocol STATIC remote_protocol.c)
target_include_directories(remote_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(remote_protocol PUBLIC c_std_11)
set_target_properties(remote_protocol PROPERTIES
                      C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
