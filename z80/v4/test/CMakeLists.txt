ensure_catch2()

add_executable(z80_v4_test Z80Test.cpp)
target_link_libraries(z80_v4_test PRIVATE z80_v4 z80_common peripherals Catch2::Catch2WithMain opt::pedantic opt::c++26)
target_compile_options(z80_v4_test PRIVATE -freflection-latest -stdlib=libc++)
target_link_options(z80_v4_test PRIVATE -stdlib=libc++)

add_test(NAME "Z80 v4 Unit Tests" COMMAND z80_v4_test)
