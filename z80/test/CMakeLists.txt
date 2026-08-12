ensure_catch2()

add_executable(
        z80_test
        OpcodeTests.cpp)
target_link_libraries(z80_test z80_v1 z80_v2 z80_v3 Catch2::Catch2WithMain)
# Reflection being available is not the same as v4 being built: v4 is excluded
# when modules are on, and gcc 16 has both.
if (TARGET z80_v4)
    target_link_libraries(z80_test z80_v4)
    target_compile_definitions(z80_test PRIVATE SPECBOLT_HAS_V4)
endif ()

add_test(NAME "Z80 v1 vs v2 Unit Tests" COMMAND z80_test)

add_executable(zexdoc_test ZexDocTest.cpp)
target_link_libraries(zexdoc_test PRIVATE z80_v1 z80_v2 z80_v3 lyra)
if (TARGET z80_v4)
    target_link_libraries(zexdoc_test PRIVATE z80_v4)
    target_compile_definitions(zexdoc_test PRIVATE SPECBOLT_HAS_V4)
endif ()

if (NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
    add_test(NAME "Z80 Regression Test (v1 implementation)" COMMAND zexdoc_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME "Z80 Regression Test (v2 implementation)" COMMAND zexdoc_test --impl 2 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME "Z80 Regression Test (v3 implementation)" COMMAND zexdoc_test --impl 3 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    if (TARGET z80_v4)
        add_test(NAME "Z80 Regression Test (v4 implementation)" COMMAND zexdoc_test --impl 4 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    endif ()
endif ()
