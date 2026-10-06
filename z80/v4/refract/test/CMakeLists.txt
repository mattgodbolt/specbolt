ensure_catch2()

# The tests that need nothing but the library: its parser and patterns, its
# diagnostics and the interpreter's, each driven by a description of its own,
# a machine that names its accessors as it pleases, and a 6502, its second
# machine, both run end to end. Linked against refract alone, so nothing of the
# Z80 is in reach.
add_executable(
        refract_test
        DiagnosticsTest.cpp
        InterpreterDiagnosticsTest.cpp
        LocationsTest.cpp
        M6502Test.cpp
        ParserTest.cpp
        PatternTest.cpp
)
target_link_libraries(refract_test refract opt::pedantic Catch2::Catch2WithMain)

add_test(NAME "refract Unit Tests" COMMAND refract_test)
