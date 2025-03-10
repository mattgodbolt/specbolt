add_library(peripherals)

target_link_libraries(peripherals PUBLIC opt::pedantic opt::c++26 opt::module)

target_sources(peripherals PRIVATE
        Audio.cpp
        Memory.cpp
        Video.cpp
        Keyboard.cpp
)

target_sources(peripherals PUBLIC FILE_SET interface TYPE HEADERS FILES
        Audio.hpp
        Memory.hpp
        Video.hpp
        Keyboard.hpp
)

add_library(peripherals_module)
target_link_libraries(peripherals_module PUBLIC opt::pedantic opt::c++26 opt::module)
target_sources(peripherals_module PUBLIC FILE_SET module TYPE CXX_MODULES FILES
  module.cppm
)