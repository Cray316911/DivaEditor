# divacapture: screen recording (Windows.Graphics.Capture -> GPU convert -> hardware encoder).
add_library(divacapture STATIC
    AudioCapture.cpp AudioCapture.h
    ScreenRecorder.cpp ScreenRecorder.h
)
target_include_directories(divacapture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(divacapture PUBLIC divacore PRIVATE windowsapp ole32)
