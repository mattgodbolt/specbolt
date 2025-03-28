ensure_catch2()

add_executable(
        z80_test
        OpcodeTests.cpp)
target_link_libraries(z80_test z80_v1 z80_v2 z80_v3 Catch2::Catch2WithMain)

add_test(NAME "Z80 v1 vs v2 Unit Tests" COMMAND z80_test)

add_executable(zexdoc_test ZexDocTest.cpp)
target_link_libraries(zexdoc_test PRIVATE z80_v1 z80_v2 z80_v3 lyra)

if (NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
    add_test(NAME "Z80 Regression Test (old implementation)" COMMAND zexdoc_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME "Z80 Regression Test (new implementation)" COMMAND zexdoc_test --new-impl WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif ()
