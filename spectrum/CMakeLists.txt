add_library(spectrum_module)
target_link_libraries(spectrum_module PUBLIC peripherals_module z80_module opt::pedantic opt::c++26 opt::module)
target_sources(spectrum_module PUBLIC FILE_SET module TYPE CXX_MODULES FILES
  Snapshot.cppm
  Spectrum.cppm
  module.cppm
)