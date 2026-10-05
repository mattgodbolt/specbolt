# A toolchain for building v4 for the browser: a clang with reflection targeting wasm32-wasip1, wasi-sdk's sysroot and
# compiled libc++, and that clang's own libc++ headers, which are the ones with <meta>. How to put the three together,
# and why each flag is here, is in z80/v4/notes/WASM.md.
#
#   cmake -B build/wasm-v4 -G Ninja --toolchain cmake/wasm-reflection.cmake -DSPECBOLT_WASM=ON -DSPECBOLT_MODULES=OFF \
#       -DSPECBOLT_REFLECTION_CLANG=... -DSPECBOLT_WASI_SYSROOT=... -DSPECBOLT_WASM_LIBCXX_HEADERS=...
set(CMAKE_SYSTEM_NAME WASI)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

set(SPECBOLT_REFLECTION_CLANG "" CACHE PATH "Install prefix of a reflection clang with wasm-ld and llvm-ar beside it")
set(SPECBOLT_WASI_SYSROOT "" CACHE PATH "wasi-sdk's wasi-sysroot")
set(SPECBOLT_WASM_LIBCXX_HEADERS "" CACHE PATH "That clang's include/c++/v1, with wasi-sdk's eh __config_site")
foreach (required IN ITEMS SPECBOLT_REFLECTION_CLANG SPECBOLT_WASI_SYSROOT SPECBOLT_WASM_LIBCXX_HEADERS)
    if (NOT ${required})
        message(FATAL_ERROR "wasm-reflection.cmake needs ${required}; see z80/v4/notes/WASM.md")
    endif ()
endforeach ()
# try_compile projects read this file afresh, so they are handed the same three.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
        SPECBOLT_REFLECTION_CLANG SPECBOLT_WASI_SYSROOT SPECBOLT_WASM_LIBCXX_HEADERS)

set(CMAKE_C_COMPILER ${SPECBOLT_REFLECTION_CLANG}/bin/clang)
set(CMAKE_CXX_COMPILER ${SPECBOLT_REFLECTION_CLANG}/bin/clang++)
set(CMAKE_C_COMPILER_TARGET wasm32-wasip1)
set(CMAKE_CXX_COMPILER_TARGET wasm32-wasip1)
set(CMAKE_SYSROOT ${SPECBOLT_WASI_SYSROOT})

# Exceptions, because the parser throws during constant evaluation and a throw does not compile without them; tail
# calls, because the interpreter's run loop is mandatory tail calls.
set(CMAKE_CXX_FLAGS_INIT "-nostdinc++ -isystem ${SPECBOLT_WASM_LIBCXX_HEADERS} -fwasm-exceptions -mtail-call")
set(CMAKE_C_FLAGS_INIT "-fwasm-exceptions -mtail-call")
# The link does the code generation under LTO, so it is told about exceptions again. `__main_argc_argv` is forced in
# because the start code refers to it only weakly, which does not pull Catch2's `main` out of its archive.
set(CMAKE_EXE_LINKER_FLAGS_INIT
        "-fwasm-exceptions -nostdlib++ -L${SPECBOLT_WASI_SYSROOT}/lib/wasm32-wasip1/eh -lc++ -lc++abi -lunwind \
-Wl,-z,stack-size=8388608 -Wl,-u,__main_argc_argv -Wl,-mllvm,-wasm-enable-eh")

# Catch2's fatal-signal handling needs POSIX signals, which WASI lacks.
set(CATCH_CONFIG_NO_POSIX_SIGNALS ON CACHE BOOL "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
