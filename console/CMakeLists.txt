find_package(Readline REQUIRED)
specbolt_dependency(Lyra "gh:bfgroup/Lyra@1.6.1#ee3c076fa6b9d64c9d249a21f5b9b5a8dae92cd8")

add_executable(specbolt_console main.cpp)
target_link_libraries(specbolt_console PRIVATE z80_v1 peripherals spectrum readline lyra)
