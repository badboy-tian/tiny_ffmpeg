cmake_minimum_required(VERSION 3.10.2)

project("testffmpeg")

set(LIB_DIR ${CMAKE_SOURCE_DIR}/${ANDROID_ABI})

file(GLOB FFMPEG_DIR ${CMAKE_SOURCE_DIR}/*.c)

include_directories(ffmpeg/include ${CMAKE_SOURCE_DIR})
#添加库, 动态导入
add_library(ffmpeg SHARED IMPORTED)
# 设置库路径, ANDROID_ABI 自动赋予值
set_target_properties(ffmpeg PROPERTIES IMPORTED_LOCATION ${LIB_DIR}/libffmpeg.so)

add_library(ffmpeg-invoke SHARED ffmpeg_utils.cpp ${FFMPEG_DIR})

find_library(log-lib log)

target_link_libraries(ffmpeg-invoke ffmpeg ${log-lib})
