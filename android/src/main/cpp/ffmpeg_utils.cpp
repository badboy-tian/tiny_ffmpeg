#include <jni.h>
#include <cstdio>
#include <ctime>

extern "C" {
#include "ffmpeg.h"
#include "ffmpeg_cmd.h"
}

#include <android/log.h>
#include <jni.h>

#define LOG_TAG "FFMpegUtils"
#ifdef ANDROID
#define LOGE(format, ...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, format, ##__VA_ARGS__)
#define LOGI(format, ...)  __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, format, ##__VA_ARGS__)
#else
#define LOGE(format, ...)  printf(LOG_TAG format "\n", ##__VA_ARGS__)
#define LOGI(format, ...)  printf(LOG_TAG format "\n", ##__VA_ARGS__)
#endif

extern int hasRegistered;
typedef struct CallBackInfo {
    JNIEnv *env;
    jobject obj;
    jmethodID methodID;
} CallBackInfo;

void log_call_back(void *ptr, int level, const char *fmt, va_list vl) {
    LOGE("%d", level);
    if(level == AV_LOG_FATAL || level == AV_LOG_ERROR){
        __android_log_vprint(ANDROID_LOG_ERROR, LOG_TAG, fmt, vl);
    }else if(level == AV_LOG_WARNING){
        __android_log_vprint(ANDROID_LOG_WARN, LOG_TAG, fmt, vl);
    }else if(level == AV_LOG_INFO){
        __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, fmt, vl);
    }else if(level == AV_LOG_VERBOSE){
        __android_log_vprint(ANDROID_LOG_VERBOSE, LOG_TAG, fmt, vl);
    }else if(level == AV_LOG_DEBUG){
        __android_log_vprint(ANDROID_LOG_DEBUG, LOG_TAG, fmt, vl);
    } else{
        __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, fmt, vl);
    }
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_showLog(JNIEnv *env, jclass clazz, jboolean showLog) {
    if (showLog) {
        av_log_set_callback(log_call_back);
    } else {
        av_log_set_callback(NULL);
    }
    return 0;
}

void progressCallBack(int64_t handle, int what, float progress) {
    if (handle != 0) {
        struct CallBackInfo *onActionListener = (struct CallBackInfo *) (handle);
        JNIEnv *env = onActionListener->env;
        env->CallVoidMethod(onActionListener->obj, onActionListener->methodID, progress);
    }
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_executeFFmpegCommand(JNIEnv *env, jclass clazz, jint cmdLen, jobjectArray argv, jobject actionCallBack, jlong totalTime) {
    int ret = 0;

    char *argCmd[cmdLen];
    jstring buf[cmdLen];

    for (int i = 0; i < cmdLen; ++i) {
        buf[i] = static_cast<jstring>(env->GetObjectArrayElement(argv, i));
        char *string = const_cast<char *>(env->GetStringUTFChars(buf[i], JNI_FALSE));
        argCmd[i] = string;
        LOGE("argCmd=%s", argCmd[i]);
    }

    //const char *command = reinterpret_cast<const char *>(argCmd);
    //LOGE("argCmd=%s", command);
    if (NULL != actionCallBack) {
        jclass actionClass = env->GetObjectClass(actionCallBack);
        jmethodID progressMID = env->GetMethodID(actionClass, "progress", "(F)V");
        jmethodID failMID = env->GetMethodID(actionClass, "fail", "(ILjava/lang/String;)V");
        jmethodID successMID = env->GetMethodID(actionClass, "success", "()V");

        CallBackInfo onActionListener;
        onActionListener.env = env;
        onActionListener.obj = actionCallBack;
        onActionListener.methodID = progressMID;
        ret = executeFFmpegCommand4TotalTime_New((int64_t) (&onActionListener), cmdLen, argCmd, progressCallBack, totalTime);
        if (ret != 0) {
            char err[1024] = { 0 };
            int nRet = av_strerror(ret, err, 1024);
            jstring msg = env->NewStringUTF(err);
            env->CallVoidMethod(actionCallBack, failMID, ret, msg);
        } else {
            env->CallVoidMethod(actionCallBack, successMID);
        }
        env->DeleteLocalRef(actionClass);
    } else {
        ret = executeFFmpegCommand_New(0, cmdLen, argCmd, NULL);
    }
    //env->ReleaseStringUTFChars(command_, command);
    return ret;
}
extern "C"
JNIEXPORT jlong JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_getMediaDuration(JNIEnv *env, jclass clazz, jstring media_path) {
    if (NULL == media_path) {
        return -1;
    }
    if (!hasRegistered) {
        av_register_all();
        avcodec_register_all();
        avfilter_register_all();
        avformat_network_init();
        hasRegistered = true;
    }
    const char *mediaPath = env->GetStringUTFChars(media_path, 0);
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

    env->ReleaseStringUTFChars(media_path, mediaPath);
    return videoDuration;
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_cancelExecuteFFmpegCommand(JNIEnv *env, jclass clazz) {
    return cancelExecuteFFmpegCommand();
}