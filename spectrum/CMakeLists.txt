add_library(spectrum)

if (SPECBOLT_MODULES)
    target_link_libraries(spectrum PUBLIC z80_common opt::pedantic opt::c++26 peripherals)
    target_include_directories(spectrum PRIVATE include)
    target_sources(spectrum
            PUBLIC
            FILE_SET modules
            TYPE CXX_MODULES
            FILES
            module.cppm
            Assets.cppm
            Snapshot.cppm
            Spectrum.cppm
    )
else ()
    target_link_libraries(spectrum PUBLIC z80_common opt::pedantic opt::c++26 peripherals)
    target_include_directories(spectrum PUBLIC include)

    target_sources(spectrum PRIVATE
            Assets.cpp
            Snapshot.cpp
    )

    target_sources(spectrum
            PUBLIC
            FILE_SET interface
            TYPE HEADERS
            FILES
            include/spectrum/Assets.hpp
            include/spectrum/Spectrum.hpp
            include/spectrum/Snapshot.hpp
    )
endif ()

# TODO this won't play well with installation.
target_compile_definitions(spectrum PRIVATE ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
