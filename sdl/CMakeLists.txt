add_executable(specbolt_sdl main.cpp)
target_link_libraries(specbolt_sdl PRIVATE z80_module peripherals_module spectrum_module SDL2::SDL2 lyra)

target_sources(specbolt_sdl PUBLIC FILE_SET module TYPE CXX_MODULES FILES
  sdl_wrapper.cppm
)