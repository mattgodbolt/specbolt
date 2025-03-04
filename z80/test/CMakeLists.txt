add_compile_definitions(CATCH_CONFIG_ENABLE_ALL_STRINGMAKERS)
specbolt_dependency(Catch2 "gh:catchorg/Catch2@3.8.0")

add_executable(
        z80_test
        AluTest.cpp
        RegisterFileTest.cpp
        DisassemblerTest.cpp
        FlagsTest.cpp
        GeneratedTest.cpp
        OpcodeTests.cpp)
target_link_libraries(z80_test z80 Catch2::Catch2WithMain)

add_test(NAME "Z80 Unit Tests" COMMAND z80_test)

add_executable(zexdoc_test ZexDocTest.cpp)
target_link_libraries(zexdoc_test PRIVATE z80 lyra)

if (NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
    add_test(NAME "Z80 Regression Test" COMMAND zexdoc_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif ()
