# Development tool: a GPU-bound stand-in for a game, used to measure recording overhead.
add_executable(gpustress main.cpp)
target_link_libraries(gpustress PRIVATE d3d11 dxgi d3dcompiler)
