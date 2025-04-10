# link to this library if you want your code to be compiled in a strict/pedantic mode
add_library(opt_pedantic INTERFACE)
add_library(opt::pedantic ALIAS opt_pedantic)

if (MSVC)
    target_compile_options(opt_pedantic INTERFACE "/W4" "/WX")
else ()
    target_compile_options(opt_pedantic INTERFACE "-Wall" "-Wextra" "-pedantic" "-Wshadow" "-Wconversion" "-Werror")

    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        target_compile_definitions(opt_pedantic INTERFACE "_LIBCPP_ENABLE_NODISCARD")
    endif ()
endif ()

# you want to use cool modern stuff
add_library(opt_c++26 INTERFACE)
add_library(opt::c++26 ALIAS opt_c++26)

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    target_compile_options(opt_c++26 INTERFACE -std=c++26)
    target_compile_options(opt_c++26 INTERFACE -fexperimental-library)
else ()
    target_compile_features(opt_c++26 INTERFACE cxx_std_26)
endif ()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # to allow stop_token and jthread in older version of clang (implemented behind flag since 18)
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20.0)
        target_compile_options(opt_c++26 INTERFACE -fexperimental-library)
    endif ()
endif ()
