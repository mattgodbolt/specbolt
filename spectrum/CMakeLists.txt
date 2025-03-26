add_library(spectrum)
target_link_libraries(spectrum PUBLIC z80_common opt::pedantic opt::c++26 peripherals)
target_sources(spectrum
        PUBLIC
        FILE_SET modules
        TYPE CXX_MODULES
        FILES
        module.cppm
        Snapshot.cppm
        Spectrum.cppm
)
