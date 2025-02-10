macro(specbold_dependency NAME CPM_PACKAGE)
  set(SPECBOLD_HAS_${NAME} OFF)

  if (SPECBOLD_PREFER_SYSTEM_DEPS)
    find_package(${NAME} QUIET)
    if (${NAME}_FOUND)
      set(SPECBOLD_HAS_${NAME} ON)
    endif()
  endif()

  if (NOT SPECBOLD_HAS_${NAME})
    CPMAddPackage(${CPM_PACKAGE})
  endif()
endmacro()
