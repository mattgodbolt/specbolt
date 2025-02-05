add_executable(specbolt_console main.cpp)
find_package(Readline REQUIRED)
target_link_libraries(specbolt_console z80 peripherals readline)
