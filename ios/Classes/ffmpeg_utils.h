//
//  ffmpeg_utils.h
//  tiny_ffmpeg
//
//  Created by tian on 2021/12/28.
//

#ifndef ffmpeg_utils_h
#define ffmpeg_utils_h

#include <string.h>
#include <stdio.h>
#include <sys/time.h>

extern int hasRegistered;
typedef struct CallBackInfo {
    
} CallBackInfo;


int Java_com_i7play_tiny_ffmpeg_FFMpegUtils_executeFFmpegCommand(int cmdLen, char* argv[], long totalTime);
long Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getMediaDuration(const char* mediaPath);
int Java_com_i7play_tiny_ffmpeg_FFMpegUtils_cancelExecuteFFmpegCommand();
int Java_com_i7play_tiny_ffmpeg_FFMpegUtils_showLog(int showLog);

extern void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progress(float progress);
extern void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_result(int code, char* msg);

#endif /* ffmpeg_utils_h */
