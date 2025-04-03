add_executable(specbolt_sdl
    main.cpp
    sdl_wrapper.cpp
    sdl_wrapper.hpp
    heatmap/memory_heatmap.cpp
    heatmap/memory_heatmap.hpp
    heatmap/heatmap_renderer.cpp
    heatmap/heatmap_renderer.hpp
    heatmap/heatmap_memory_listener.hpp
)
target_link_libraries(specbolt_sdl PRIVATE z80_v1 z80_v2 z80_v3 peripherals spectrum SDL2::SDL2 lyra)
