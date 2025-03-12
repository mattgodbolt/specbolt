add_library(peripherals)

target_link_libraries(peripherals PUBLIC opt::pedantic opt::c++26)
if (SPECBOLT_MODULES)
    target_include_directories(peripherals PRIVATE include)
    target_sources(peripherals
            PUBLIC
            FILE_SET modules
            TYPE CXX_MODULES
            FILES
            module.cppm
            Audio.cppm
            Keyboard.cppm
            Memory.cppm
            Video.cppm
    )
else ()
    target_include_directories(peripherals PUBLIC include)
    target_compile_definitions(peripherals PUBLIC SPECBOLT_EXPORT=)

    target_sources(peripherals PRIVATE
            Audio.cpp
            Keyboard.cpp
            Memory.cpp
            Video.cpp
    )

    target_sources(peripherals
            PUBLIC
            FILE_SET interface
            TYPE HEADERS
            FILES
            include/peripherals/Audio.hpp
            include/peripherals/Keyboard.hpp
            include/peripherals/Memory.hpp
            include/peripherals/Video.hpp
    )
endif ()
