add_executable(specbolt_sdl main.cpp sdl_wrapper.hpp)
target_link_libraries(specbolt_sdl PRIVATE z80_v1 z80_v2 peripherals spectrum SDL2::SDL2 lyra)
