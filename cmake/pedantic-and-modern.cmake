set(PEDANTIC_FLAGS)
set(PEDANTIC_DEFINITIONS)

if (MSVC)
  list(APPEND PEDANTIC_FLAGS "/W4" "/WX")
else()
  list(APPEND PEDANTIC_FLAGS  "-Wall" "-Wextra" "-pedantic" "-Wshadow" "-Wconversion" "-Werror")
	
	if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    list(APPEND PEDANTIC_DEFINITIONS "_LIBCPP_ENABLE_NODISCARD")
	endif()
endif()

# this macro sets c++26 and pedantic flags for specified target
# it doesn't need to be set for everything if visibility is "PUBLIC" it will be inherited
# by any library which links to these targets, but not in other direction
macro(modern_and_pedantic_settings TARGET VISIBILITY)
  if (NOT VISIBILITY)
    set(VISIBILITY PUBLIC)
  endif()
  target_compile_features(${TARGET} ${VISIBILITY} cxx_std_26)
  target_compile_options(${TARGET} ${VISIBILITY} "$<$<COMPILE_LANGUAGE:CXX>:${PEDANTIC_FLAGS}>")
  target_compile_definitions(${TARGET} ${VISIBILITY} "$<$<COMPILE_LANGUAGE:CXX>:${PEDANTIC_DEFINITIONS}>")
endmacro()