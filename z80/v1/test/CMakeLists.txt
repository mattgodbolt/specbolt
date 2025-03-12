add_compile_definitions(CATCH_CONFIG_ENABLE_ALL_STRINGMAKERS)
specbolt_dependency(Catch2 "gh:catchorg/Catch2@3.8.0")

add_executable(
        z80_v1_test
        DisassemblerTest.cpp)
target_link_libraries(z80_v1_test z80_v1 Catch2::Catch2WithMain)

add_test(NAME "Z80 v1 Unit Tests" COMMAND z80_v1_test)
