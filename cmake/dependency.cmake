macro(specbolt_dependency NAME CPM_PACKAGE)
  set(SPECBOLT_HAS_${NAME} OFF)

  if (SPECBOLT_PREFER_SYSTEM_DEPS)
    find_package(${NAME} QUIET)
    if (${NAME}_FOUND)
      set(SPECBOLT_HAS_${NAME} ON)
    endif()
  endif()

  if (NOT SPECBOLT_HAS_${NAME})
    CPMAddPackage(${CPM_PACKAGE})
  endif()
endmacro()
