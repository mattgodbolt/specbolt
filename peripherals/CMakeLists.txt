add_library(peripherals)

target_link_libraries(peripherals PUBLIC opt::pedantic opt::c++26)
target_include_directories(peripherals PUBLIC include)

target_sources(peripherals PRIVATE
        Audio.cpp
        Memory.cpp
        Video.cpp
        Keyboard.cpp
)

target_sources(peripherals PUBLIC FILE_SET interface TYPE HEADERS FILES
        include/peripherals/Audio.hpp
        include/peripherals/Memory.hpp
        include/peripherals/Video.hpp
        include/peripherals/Keyboard.hpp
)
