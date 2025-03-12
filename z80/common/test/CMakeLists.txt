ensure_catch2()

add_executable(
        z80_common_test
        AluTest.cpp
        RegisterFileTest.cpp)
target_link_libraries(z80_common_test z80_common Catch2::Catch2WithMain)

add_test(NAME "Z80 Common Unit Tests" COMMAND z80_test)
