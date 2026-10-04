# refract, the library that turns a `.cpu` description into an interpreter. It is
# part of v4, whose Z80 is the machine it was built for, but it knows nothing of
# the Z80, and is a target of its own so that the build says so rather than a
# review. Header-only: everything it does happens at compile time, in whatever
# translation unit includes it.

# The repository root is on every target's include path (the top-level
# CMakeLists.txt). Taken off here, before the tests are added, so that what
# builds against refract reaches only what it links: a refract header that
# included anything of the Z80's would then fail to build its own tests.
set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES "")

if (SPECBOLT_TESTS)
    add_subdirectory(test)
endif ()

add_library(refract INTERFACE)
target_link_libraries(refract INTERFACE opt::c++26)

target_sources(refract INTERFACE FILE_SET HEADERS BASE_DIRS include FILES
        include/refract/Compiled.hpp
        include/refract/Checks.hpp
        include/refract/Continue.hpp
        include/refract/Decode.hpp
        include/refract/Disassemble.hpp
        include/refract/Execute.hpp
        include/refract/Lexical.hpp
        include/refract/Machine.hpp
        include/refract/Model.hpp
        include/refract/Parse.hpp
        include/refract/Parser.hpp
        include/refract/Pattern.hpp
        include/refract/TableError.hpp
        include/refract/ToArray.hpp
        include/refract/Vector.hpp
        include/refract/Visit.hpp
        include/refract/Workarounds.hpp
)
