add_executable(divabench main.cpp)
target_link_libraries(divabench PRIVATE divacore)
diva_copy_ffmpeg_dlls(divabench)
