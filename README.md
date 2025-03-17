# specbolt [![specbolt CI](https://github.com/mattgodbolt/specbolt/actions/workflows/ci.yml/badge.svg)](https://github.com/mattgodbolt/specbolt/actions/workflows/ci.yml)

Yet another ZX Spectrum Emulator. Part of a project for an upcoming C++ talk.

### Building

- Pre-requisites: CMake, Ninja and a clang compiler newer than 20.
    - On Ubuntu: `wget https://apt.llvm.org/llvm.sh; sudo bash llvm.sh 20 all` will get you clang and the things it
      needs.
- LibSDL's development headers and dependencies: `sudo apt-get install libsdl2-dev` on Ubuntu.
- for the console app `libreadline-dev` too.
- Build with cmake in your favourite way.


### Thanks to

Huge thanks to:

- [Hana Dusíková](https://github.com/hanickadot) for all the clever C++ stuff, and help with modules.
- [blargg (aka Shay Green)](http://www.slack.net/~ant/) for the band-limiting code used in the audio.
