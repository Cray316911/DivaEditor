cmake_minimum_required(VERSION 3.25)
project(DivaStudio VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "DivaStudio currently targets Windows only (macOS port is planned).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_compile_options(/utf-8 /permissive- /Zc:__cplusplus /W3)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE DIVA_VERSION="${PROJECT_VERSION}")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(FFmpeg REQUIRED)
find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Network Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.8)

# Copies the FFmpeg DLLs next to an executable so it runs straight from the build tree.
function(diva_copy_ffmpeg_dlls target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_RUNTIME_DLLS} $<TARGET_FILE_DIR:${target}>
        COMMAND_EXPAND_LISTS)
endfunction()

add_subdirectory(src/core)
add_subdirectory(src/capture)
add_subdirectory(src/edit)
add_subdirectory(src/app)
add_subdirectory(tools/divabench)
add_subdirectory(tools/divarec)
add_subdirectory(tools/divamcp)
add_subdirectory(tools/gpustress)
