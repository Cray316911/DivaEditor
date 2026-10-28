# Finds an FFmpeg development package laid out as include/, lib/ and bin/
# (for example the BtbN "gpl-shared" builds). Point FFMPEG_ROOT at it.
#
# Defines FFmpeg::avcodec, ::avformat, ::avutil, ::swscale, ::swresample and FFMPEG_RUNTIME_DLLS.

set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}" CACHE PATH "FFmpeg development package root")

find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h HINTS "${FFMPEG_ROOT}/include")

foreach(_lib avcodec avformat avutil swscale swresample)
    find_library(FFMPEG_${_lib}_LIBRARY NAMES ${_lib} HINTS "${FFMPEG_ROOT}/lib")
    if(FFMPEG_${_lib}_LIBRARY AND NOT TARGET FFmpeg::${_lib})
        add_library(FFmpeg::${_lib} UNKNOWN IMPORTED)
        set_target_properties(FFmpeg::${_lib} PROPERTIES
            IMPORTED_LOCATION "${FFMPEG_${_lib}_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}")
    endif()
endforeach()

file(GLOB FFMPEG_RUNTIME_DLLS "${FFMPEG_ROOT}/bin/*.dll")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg REQUIRED_VARS
    FFMPEG_INCLUDE_DIR FFMPEG_avcodec_LIBRARY FFMPEG_avformat_LIBRARY FFMPEG_avutil_LIBRARY
    FFMPEG_swscale_LIBRARY FFMPEG_swresample_LIBRARY)
