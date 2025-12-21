cmake_minimum_required(VERSION 3.22.1)

project("testffmpeg")

set(LIB_DIR ${CMAKE_SOURCE_DIR}/${ANDROID_ABI})

# FFmpeg 8.0 fftools 源文件
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
    ${FFTOOLS_SOURCES}
)

find_library(log-lib log)

target_link_libraries(ffmpeg-invoke ffmpeg ${log-lib})
