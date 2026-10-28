# divacore: UI-independent engine shared by the editor, the render worker and the tools.
add_library(divacore STATIC
    util/Errors.h
    gpu/GpuDevice.cpp gpu/GpuDevice.h
    gpu/SurfacePool.cpp gpu/SurfacePool.h
    media/HwDecoder.cpp media/HwDecoder.h
    media/HwEncoder.cpp media/HwEncoder.h
    media/Muxer.cpp media/Muxer.h
    compositor/Nv12Renderer.cpp compositor/Nv12Renderer.h
    compositor/RgbToNv12.cpp compositor/RgbToNv12.h
)
target_include_directories(divacore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(divacore PUBLIC
    FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample
    d3d11 dxgi d3dcompiler)
