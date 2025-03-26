if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(peripherals)

target_link_libraries(peripherals PUBLIC opt::pedantic opt::c++26)
target_sources(peripherals
        PUBLIC
        FILE_SET modules
        TYPE CXX_MODULES
        FILES
        module.cppm
        Audio.cppm
        Blip_Buffer.cppm
        Keyboard.cppm
        Memory.cppm
        Tape.cppm
        Video.cppm
)
