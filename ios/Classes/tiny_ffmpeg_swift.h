//
//  tiny_ffmpeg_swift.h
//  tiny_ffmpeg
//
//  Swift 桥接头文件 - 只包含 Swift 需要的函数声明
//

#ifndef tiny_ffmpeg_swift_h
#define tiny_ffmpeg_swift_h

#include <stdint.h>

// 日志和进度按当前原子 Session ID 路由，兼容多个 Flutter Engine。
int64_t get_ffmpeg_current_session_id(void);

// FFmpeg 命令执行函数
int64_t Java_com_i7play_tiny_ffmpeg_FFMpegUtils_executeFFmpegCommandWithSession(
    int64_t sessionId, int cmdLen, char *argv[], long totalTime);

// 媒体信息以 JSON 跨越 C/Swift 边界，避免暴露 FFmpeg 内部结构。
const char *Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getMediaInfo(const char *mediaPath);

long Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getMediaDuration(const char *mediaPath);

int Java_com_i7play_tiny_ffmpeg_FFMpegUtils_cancelFFmpegCommandBySession(int64_t sessionId);

int64_t Java_com_i7play_tiny_ffmpeg_FFMpegUtils_createFFmpegSession(void);

const char* Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getSessionErrorMessage(int64_t sessionId);

void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_destroyFFmpegSession(int64_t sessionId);

void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_setLogEnabled(int enabled);

// Swift 回调函数声明
void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progress(float progress);
void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_result(int64_t sessionId, int32_t code, const char *msg);
void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_log(int32_t logLevel, const char *logMessage);
void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progressMessage(const char *message);

#endif /* tiny_ffmpeg_swift_h */
