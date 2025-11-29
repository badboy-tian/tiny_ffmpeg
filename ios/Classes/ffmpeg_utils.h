//
//  ffmpeg_utils.h
//  tiny_ffmpeg
//
//  Created by tian on 2021/12/28.
//

#ifndef ffmpeg_utils_h
#define ffmpeg_utils_h

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

extern int hasRegistered;
typedef struct CallBackInfo {

} CallBackInfo;

int64_t Java_com_i7play_tiny_ffmpeg_FFMpegUtils_executeFFmpegCommandWithSession(
    int64_t sessionId, int cmdLen, char *argv[], long totalTime);
long Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getMediaDuration(
    const char *mediaPath);
int Java_com_i7play_tiny_ffmpeg_FFMpegUtils_cancelFFmpegCommandBySession(int64_t sessionId);
int64_t Java_com_i7play_tiny_ffmpeg_FFMpegUtils_createFFmpegSession();
const char* Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getSessionErrorMessage(int64_t sessionId);
void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_destroyFFmpegSession(int64_t sessionId);

extern void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progress(float progress);
extern void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_result(int code, char *msg);
extern void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_log(int logLevel, const char* logMessage);
extern void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progressMessage(const char* message);

#endif /* ffmpeg_utils_h */
