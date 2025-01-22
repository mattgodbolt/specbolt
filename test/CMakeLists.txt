find_package(Catch2 REQUIRED)

add_executable(
        specbolt_test
        RegisterFileTest.cpp
        DisassemblerTest.cpp)
target_link_libraries(specbolt_test specbolt_lib Catch2::Catch2WithMain)

add_test(NAME test COMMAND specbolt_test)
