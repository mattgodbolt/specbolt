find_package(Readline REQUIRED)

add_executable(specbolt_console main.cpp)
target_link_libraries(specbolt_console PRIVATE z80_v1 z80_v2 peripherals spectrum readline lyra)
