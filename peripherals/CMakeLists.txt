add_library(peripherals)

target_link_libraries(peripherals PUBLIC opt::pedantic opt::c++26)

target_sources(peripherals PRIVATE
  Memory.cpp
  Video.cpp
)

target_sources(peripherals PUBLIC FILE_SET interface TYPE HEADERS FILES
  Memory.hpp
  Video.hpp
)
