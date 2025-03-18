add_executable(web.wasm main.cpp)
target_link_libraries(web.wasm PRIVATE spectrum z80_v2)

find_package(Npm REQUIRED)

add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/node_modules
        COMMAND ${NPM_EXECUTABLE} clean-install
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        MAIN_DEPENDENCY package.json
        DEPENDS package-lock.json
        COMMENT "Installing npm dependencies"
)

add_library(web.wasm.web INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/node_modules)

add_dependencies(web.wasm web.wasm.web)
