add_library(peripherals_module)
target_link_libraries(peripherals_module PUBLIC opt::pedantic opt::c++26)
target_sources(peripherals_module PUBLIC FILE_SET module TYPE CXX_MODULES FILES
  All.cppm
  Audio.cppm
  Keyboard.cppm
  Memory.cppm
  Video.cppm
)