specbold_dependency(Catch2 "gh:catchorg/Catch2@3.8.0")

add_executable(
        z80_test
        AluTest.cpp
        RegisterFileTest.cpp
        DisassemblerTest.cpp
        FlagsTest.cpp)
target_link_libraries(z80_test z80 Catch2::Catch2WithMain)

add_test(NAME test COMMAND z80_test)
