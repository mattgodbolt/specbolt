macro(ensure_catch2)
    add_compile_definitions(CATCH_CONFIG_ENABLE_ALL_STRINGMAKERS)
    specbolt_dependency(Catch2 "gh:catchorg/Catch2@3.8.0")
endmacro()
