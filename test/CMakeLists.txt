find_package(Catch2 REQUIRED)

add_executable(
        specbolt_test
        AluTest.cpp
        RegisterFileTest.cpp
        DisassemblerTest.cpp
        FlagsTest.cpp)
target_link_libraries(specbolt_test specbolt_lib Catch2::Catch2WithMain)

add_test(NAME test COMMAND specbolt_test)
