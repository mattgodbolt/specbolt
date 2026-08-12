ensure_catch2()

add_executable(
        z80_v4_test
        ParserTest.cpp
        MatchedTest.cpp
        TableTest.cpp
        DiagnosticsTest.cpp
        DisassemblerTest.cpp
        ExecuteTest.cpp
)
target_link_libraries(z80_v4_test z80_v4 Catch2::Catch2WithMain)

add_test(NAME "Z80 v4 Unit Tests" COMMAND z80_v4_test)
