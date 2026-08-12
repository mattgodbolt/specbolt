add_subdirectory(common)
add_subdirectory(v1)
add_subdirectory(v2)
if (NOT SPECBOLT_WASM)
    # need to "natively" compile the v3 generator-generator. Can't be arsed to get that working right now.
    add_subdirectory(v3)
    if (SPECBOLT_HAS_REFLECTION AND SPECBOLT_MODULES)
        # v4's table is a header included into more than one module partition,
        # which duplicates its definitions. The reflection presets keep the two
        # axes separable on purpose; see z80/v4/NOTES.md.
        message(STATUS "specbolt: v4 needs reflection without modules, so it is not being built")
    elseif (SPECBOLT_HAS_REFLECTION)
        add_subdirectory(v4)
    endif ()
endif ()

# After the implementations, because the shared opcode tests run every one of
# them that this build has.
if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()
