#include <jni.h>
#include <cstdio>
#include <ctime>

extern "C" {
#include "ffmpeg.h"
#include "ffmpeg_cmd.h"
#include "ffmpeg_utils.h"
}

#define LOG_TAG "FFMpegUtils"

#define LOGE(format, ...)  printf(LOG_TAG format "\n", ##__VA_ARGS__)
#define LOGI(format, ...)  printf(LOG_TAG format "\n", ##__VA_ARGS__)

void log_call_back(void *ptr, int level, const char *fmt, va_list vl) {
    //vprintf(fmt, vl);
    if(level == AV_LOG_FATAL || level == AV_LOG_ERROR){
        vprintf(fmt, vl);
    }else if(level == AV_LOG_WARNING){
        vprintf(fmt, vl);
    }else if(level == AV_LOG_INFO){
        vprintf(fmt, vl);
    }else if(level == AV_LOG_VERBOSE){
        vprintf(fmt, vl);
    }else if(level == AV_LOG_DEBUG){
        vprintf(fmt, vl);
    } else{
        vprintf(fmt, vl);
    }
}

int Java_com_i7play_tiny_ffmpeg_FFMpegUtils_showLog(int showLog) {
    if (showLog) {
        av_log_set_callback(log_call_back);
    } else {
        av_log_set_callback(NULL);
    }
    return 0;
}

void progressCallBack(int64_t handle, int what, float progress) {
    Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progress(progress);
}

int Java_com_i7play_tiny_ffmpeg_FFMpegUtils_executeFFmpegCommand(int cmdLen, char* argv[], long totalTime) {
    int ret = 0;

    for (int i = 0; i < cmdLen; ++i) {
        LOGE("argCmd=%s\n", argv[i]);
    }
    //ret = executeFFmpegCommand_New(0, cmdLen, argv, NULL);
    //        ret = executeFFmpegCommand4TotalTime_New((int64_t) (&onActionListener), cmdLen, argCmd, progressCallBack, totalTime);
    CallBackInfo onActionListener;
          
    ret = executeFFmpegCommand4TotalTime_New((int64_t) (&onActionListener), cmdLen, argv, progressCallBack, -1);
    char err[1024] = { 0 };
    int nRet = av_strerror(ret, err, 1024);
    
    //printf("===%s====\n", err);
    
    bool ss;

    Java_com_i7play_tiny_ffmpeg_FFMpegUtils_result(ret, err);
    
    return ret;
}

long Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getMediaDuration(const char* mediaPath) {
    if (NULL == mediaPath) {
        return -1;
    }
    if (!hasRegistered) {
        av_register_all();
        avcodec_register_all();
        avfilter_register_all();
        avformat_network_init();
        hasRegistered = true;
    }
    //const char *mediaPath = env->GetStringUTFChars(media_path, 0);
    AVFormatContext *in_fmt_ctx = NULL;
    int ret = 0;
    if ((ret = avformat_open_input(&in_fmt_ctx, mediaPath, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file mediaPath=%s", mediaPath);
        return ret;
    }
    if ((ret = avformat_find_stream_info(in_fmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }
    int64_t videoDuration = 0;
    for (int i = 0; i < in_fmt_ctx->nb_streams; i++) {
        AVStream *stream;
        stream = in_fmt_ctx->streams[i];
        int64_t temp = stream->duration * 1000 * stream->time_base.num /
                       stream->time_base.den;
        if (temp > videoDuration)
            videoDuration = temp;
    }
    if (NULL != in_fmt_ctx)
        avformat_close_input(&in_fmt_ctx);

    //env->ReleaseStringUTFChars(media_path, mediaPath);
    return videoDuration;
}
int Java_com_i7play_tiny_ffmpeg_FFMpegUtils_cancelExecuteFFmpegCommand() {
    return cancelExecuteFFmpegCommand();
}
