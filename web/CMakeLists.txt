add_executable(spectrum.wasm main.cpp)
target_link_libraries(spectrum.wasm PRIVATE spectrum z80_v2)

find_package(Npm REQUIRED)

add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/node_modules
        COMMAND ${NPM_EXECUTABLE} clean-install
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        MAIN_DEPENDENCY package.json
        DEPENDS package-lock.json
        COMMENT "Installing npm dependencies"
)

add_custom_target(node_modules DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/node_modules)

add_dependencies(spectrum.wasm node_modules)

add_custom_command(
        OUTPUT dist
        COMMAND ${CMAKE_COMMAND} -E env "NODE_ENV=production" "VITE_WASM_BUILD_DIR=${CMAKE_BINARY_DIR}" ${NPM_EXECUTABLE} run build
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Building distribution module"
        DEPENDS node_modules spectrum.wasm
)

add_custom_target(spectrum.wasm.web.dist ALL DEPENDS dist)
install(DIRECTORY dist/ DESTINATION www)
