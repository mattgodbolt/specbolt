if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_subdirectory(common)
add_subdirectory(v1)
add_subdirectory(v2)
if (NOT SPECBOLT_WASM)
    # need to "natively" compile the v3 generator-generator. Can't be arsed to get that working right now.
    add_subdirectory(v3)
endif ()
