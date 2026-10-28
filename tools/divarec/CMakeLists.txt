add_executable(divarec main.cpp)
target_link_libraries(divarec PRIVATE divacapture windowsapp)
diva_copy_ffmpeg_dlls(divarec)
