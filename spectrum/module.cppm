module;

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>

export module spectrum;

export import peripherals;
export import z80;

#define SPECBOLT_IN_MODULE
#define SPECBOLT_EXPORT export

#include "Snapshot.hpp"
#include "Spectrum.hpp"

#include "Snapshot.cpp"
#include "Spectrum.cpp"
