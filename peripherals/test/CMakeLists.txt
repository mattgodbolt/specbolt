ensure_catch2()

add_executable(
        peripherals_test
        MemoryTest.cpp)
target_link_libraries(peripherals_test peripherals Catch2::Catch2WithMain)

add_test(NAME "peripheral Unit Tests" COMMAND peripherals_test)
