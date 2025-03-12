add_library(spectrum)

target_link_libraries(spectrum PUBLIC z80_common opt::pedantic opt::c++26 peripherals)
target_include_directories(spectrum PUBLIC include)

target_sources(spectrum PRIVATE
        Snapshot.cpp
)

target_sources(spectrum PUBLIC FILE_SET interface TYPE HEADERS FILES
        include/spectrum/Spectrum.hpp
        include/spectrum/Snapshot.hpp
)
