add_compile_definitions(CATCH_CONFIG_ENABLE_ALL_STRINGMAKERS)
specbolt_dependency(Catch2 "gh:catchorg/Catch2@3.8.0")

add_executable(
        z80_common_test
        AluTest.cpp
        RegisterFileTest.cpp)
target_link_libraries(z80_common_test z80_common Catch2::Catch2WithMain)

add_test(NAME "Z80 Common Unit Tests" COMMAND z80_test)
