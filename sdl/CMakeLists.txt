add_executable(specbolt_sdl main.cpp sdl_wrapper.hpp)
target_link_libraries(specbolt_sdl PRIVATE z80_module peripherals_module spectrum_module SDL2::SDL2 lyra)
