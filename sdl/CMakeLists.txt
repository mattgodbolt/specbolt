add_executable(specbolt_sdl main.cpp sdl_wrapper.hpp)
target_link_libraries(specbolt_sdl PRIVATE z80 peripherals spectrum SDL2::SDL2 lyra)
