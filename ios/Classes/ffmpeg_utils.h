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
#include <stdint.h>
#include <stdbool.h>

#include "libavutil/common.h"
#include "libavutil/frame.h"
#include "libavutil/rational.h"

#include "libavcodec/packet.h"

typedef struct Timestamp {
    int64_t    ts;
    AVRational tb;
} Timestamp;

/**
 * Merge two return codes - return one of the error codes if at least one of
 * them was negative, 0 otherwise.
 */
static inline int err_merge(int err0, int err1)
{
    // prefer "real" errors over EOF
    if ((err0 >= 0 || err0 == AVERROR_EOF) && err1 < 0)
        return err1;
    return (err0 < 0) ? err0 : FFMIN(err1, 0);
}

/**
 * Wrapper calling av_frame_side_data_clone() in a loop for all source entries.
 * It does not clear dst beforehand. */
static inline int clone_side_data(AVFrameSideData ***dst, int *nb_dst,
                                  AVFrameSideData * const *src, int nb_src,
                                  unsigned int flags)
{
    for (int i = 0; i < nb_src; i++) {
        int ret = av_frame_side_data_clone(dst, nb_dst, src[i], flags);
        if (ret < 0)
            return ret;
    }

    return 0;
}

// extern int hasRegistered;  // iOS: 新版 FFmpeg 不需要手动注册
typedef struct CallBackInfo {
  bool hasLogCallback;
  bool hasProgressCallback;
  int64_t sessionId;
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
extern void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_result(int64_t sessionId, int32_t code, const char *msg);
extern void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_log(int32_t logLevel, const char* logMessage);
extern void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progressMessage(const char* message);

#endif /* ffmpeg_utils_h */
