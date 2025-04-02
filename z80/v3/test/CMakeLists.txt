ensure_catch2()

add_executable(
        z80_v3_test
        DisassemblerTest.cpp
)
target_link_libraries(z80_v3_test z80_v3 Catch2::Catch2WithMain)

add_test(NAME "Z80 v3 Unit Tests" COMMAND z80_v3_test)
