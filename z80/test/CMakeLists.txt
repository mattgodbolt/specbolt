ensure_catch2()

add_executable(
        z80_test
        OpcodeTests.cpp)
target_link_libraries(z80_test z80_v1 z80_v2 z80_v3 Catch2::Catch2WithMain)

add_test(NAME "Z80 v1 vs v2 Unit Tests" COMMAND z80_test)

add_executable(zexdoc_test ZexDocTest.cpp)
target_link_libraries(zexdoc_test PRIVATE z80_v1 z80_v2 z80_v3 lyra)

# One benchmark holding every implementation, and one per implementation.
# Interprocedural optimisation is on for the whole link, so the combined binary
# optimises the implementations against each other: editing one changes the code
# generated for the others, by enough to swamp what is being measured. The
# combined binary is what the emulator ships; the single ones are what a
# comparison between implementations should be read from.
add_executable(z80_bench Bench.cpp)
target_link_libraries(z80_bench PRIVATE z80_v1 z80_v2 z80_v3 spectrum peripherals lyra)
target_compile_definitions(z80_bench PRIVATE BENCH_V1=1 BENCH_V2=1 BENCH_V3=1)

foreach (version IN ITEMS 1 2 3)
    add_executable(z80_bench_v${version} Bench.cpp)
    target_link_libraries(z80_bench_v${version} PRIVATE z80_v${version} spectrum peripherals lyra)
    target_compile_definitions(z80_bench_v${version} PRIVATE BENCH_V${version}=1)
endforeach ()

if (NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
    add_test(NAME "Z80 Regression Test (v1 implementation)" COMMAND zexdoc_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME "Z80 Regression Test (v2 implementation)" COMMAND zexdoc_test --impl 2 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME "Z80 Regression Test (v3 implementation)" COMMAND zexdoc_test --impl 3 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif ()
