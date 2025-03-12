add_library(peripherals)

target_link_libraries(peripherals PUBLIC opt::pedantic opt::c++26)
target_include_directories(peripherals PUBLIC include)
target_compile_definitions(peripherals PUBLIC SPECBOLT_EXPORT=)

target_sources(peripherals PRIVATE
        Audio.cpp
        Keyboard.cpp
        Memory.cpp
        Video.cpp
)

target_sources(peripherals PUBLIC FILE_SET interface TYPE HEADERS FILES
        include/peripherals/Audio.hpp
        include/peripherals/Keyboard.hpp
        include/peripherals/Memory.hpp
        include/peripherals/Video.hpp
)

add_library(peripherals_modules)
target_link_libraries(peripherals_modules PUBLIC opt::pedantic opt::c++26)
target_include_directories(peripherals_modules PRIVATE include)
target_sources(peripherals_modules PUBLIC FILE_SET modules TYPE CXX_MODULES FILES
        Audio.cppm
        Keyboard.cppm
        Memory.cppm
        Video.cppm
)
