cmake_minimum_required(VERSION 3.22.1)

project("testffmpeg")

set(LIB_DIR ${CMAKE_SOURCE_DIR}/${ANDROID_ABI})

# FFmpeg 9 前端使用 NDK Binder 兼容层，确保 MediaCodec 可初始化。
set(FFTOOLS_DIR ${CMAKE_SOURCE_DIR}/fftools)
file(GLOB FFTOOLS_SOURCES
    ${FFTOOLS_DIR}/*.c
    ${FFTOOLS_DIR}/textformat/*.c
)

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${FFTOOLS_DIR}
)

# 添加 FFmpeg 动态库
add_library(ffmpeg SHARED IMPORTED)
set_target_properties(ffmpeg PROPERTIES IMPORTED_LOCATION ${LIB_DIR}/libffmpeg.so)

# 创建主库
add_library(ffmpeg-invoke SHARED
    ffmpeg_cmd.c
    include/compat/android/binder.c
    include/compat/strtod.c
    ${FFTOOLS_SOURCES}
)

find_library(log-lib log)

# FFmpeg 9 前端使用 C11 原子类型；JNI 继续链接独立的 FFmpeg 动态库。
target_compile_features(ffmpeg-invoke PRIVATE c_std_17)
# JNI 桥接库也必须使用 16 KB 段对齐，才能在大内存页设备上随 FFmpeg 一起加载。
target_link_options(ffmpeg-invoke PRIVATE "-Wl,-z,max-page-size=16384")
target_link_libraries(ffmpeg-invoke ffmpeg ${log-lib} dl)
