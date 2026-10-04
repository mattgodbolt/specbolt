ensure_catch2()

# The tests that need nothing but the library: its parser and patterns, its
# diagnostics, each driven by a description of its own, and a second machine run
# end to end. Linked against refract alone, so nothing of the Z80 is in reach.
add_executable(
        refract_test
        DiagnosticsTest.cpp
        ParserTest.cpp
        PatternTest.cpp
        SecondMachineTest.cpp
)
target_link_libraries(refract_test refract opt::pedantic Catch2::Catch2WithMain)

add_test(NAME "refract Unit Tests" COMMAND refract_test)
