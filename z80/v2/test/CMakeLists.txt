ensure_catch2()

add_executable(
        z80_v2_test
        DisassemblerTest.cpp
        IndirectTest.cpp)
target_link_libraries(z80_v2_test z80_v2 Catch2::Catch2WithMain)

add_test(NAME "Z80 v2 Unit Tests" COMMAND z80_v2_test)
