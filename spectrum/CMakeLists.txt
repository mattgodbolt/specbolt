add_library(spectrum)

target_link_libraries(spectrum PUBLIC opt::pedantic opt::c++26 peripherals z80)

target_sources(spectrum PRIVATE
  Spectrum.cpp
)

target_sources(spectrum PUBLIC FILE_SET interface TYPE HEADERS FILES
  Spectrum.hpp
)
