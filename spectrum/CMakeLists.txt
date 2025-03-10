add_library(spectrum)

target_link_libraries(spectrum PUBLIC opt::pedantic opt::c++26 peripherals z80 opt::module)

target_sources(spectrum PRIVATE
        Spectrum.cpp
        Snapshot.cpp
)

target_sources(spectrum PUBLIC FILE_SET interface TYPE HEADERS FILES
        Spectrum.hpp
        Snapshot.hpp
)

add_library(spectrum_module)
target_link_libraries(spectrum_module PUBLIC peripherals_module z80_module opt::pedantic opt::c++26 opt::module)
target_sources(spectrum_module PUBLIC FILE_SET module TYPE CXX_MODULES FILES
  module.cppm
)