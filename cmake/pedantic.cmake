macro(add_c_and_cxx_compile_options)
	add_compile_options("$<$<COMPILE_LANGUAGE:C>:${ARGV}>")
	add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${ARGV}>")
endmacro()

macro(add_c_and_cxx_compile_definitions)
	add_compile_definitions("$<$<COMPILE_LANGUAGE:C>:${ARGV}>")
	add_compile_definitions("$<$<COMPILE_LANGUAGE:CXX>:${ARGV}>")
endmacro()

if (MSVC)
	add_c_and_cxx_compile_options("/W4" "/WX")
else()
	add_c_and_cxx_compile_options("-Wall" "-Wextra" "-pedantic" "-Wshadow" "-Wconversion" "-Werror")

	if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
		add_c_and_cxx_compile_definitions("_LIBCPP_ENABLE_NODISCARD")
	endif()
endif()
