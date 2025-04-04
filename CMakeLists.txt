add_executable(picow_speedybee
        picow_speedybee_control.c
        )

target_include_directories(picow_speedybee PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/../wifi # for our common lwipopts
        )

target_link_libraries(picow_speedybee
        pico_cyw43_arch_lwip_threadsafe_background
        pico_stdlib
        pico_multicore
        )

pico_add_extra_outputs(picow_speedybee)