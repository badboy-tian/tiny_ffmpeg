#include <cstdio>
#include <ctime>
#include <jni.h>
#include <string>

extern "C" {
#include "ffmpeg.h"
#include "ffmpeg_cmd.h"
}

#include <android/log.h>
#include <jni.h>

#define LOG_TAG "FFMpegUtils"
#ifdef ANDROID
#define LOGE(format, ...)                                                      \
  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, format, ##__VA_ARGS__)
#define LOGI(format, ...)                                                      \
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, format, ##__VA_ARGS__)
#else
#define LOGE(format, ...) printf(LOG_TAG format "\n", ##__VA_ARGS__)
#define LOGI(format, ...) printf(LOG_TAG format "\n", ##__VA_ARGS__)
#endif

extern int hasRegistered;
typedef struct CallBackInfo {
  JNIEnv *env;
  jobject obj;
  jmethodID methodID;
  jclass utilsClass;
  jmethodID sendLogMethodID;
  jmethodID sendProgressMethodID;
  bool hasLogCallback;
  bool hasProgressCallback;
  int64_t sessionId;
} CallBackInfo;

static JavaVM *g_jvm = nullptr;

JNIEnv *getJNIEnv() {
  JNIEnv *env = nullptr;
  if (g_jvm != nullptr) {
    g_jvm->GetEnv((void **)&env, JNI_VERSION_1_6);
    if (env == nullptr) {
      g_jvm->AttachCurrentThread(&env, nullptr);
    }
  }
  return env;
}

// 全局变量存储当前的 CallBackInfo
static CallBackInfo *g_currentCallbackInfo = nullptr;

// 全局变量控制是否输出日志到控制台（默认关闭）
static bool g_logEnabled = false;

// 声明 Session 管理函数
extern void appendSessionErrorMessage(int64_t sessionId, const char *message);

void log_call_back_with_callback(void *ptr, int level, const char *fmt,
                                 va_list vl) {
  char buffer[8192];
  vsnprintf(buffer, sizeof(buffer), fmt, vl);

  // 收集 ERROR、FATAL 和 WARNING 级别的日志到Session错误缓冲区
  // 同时也收集 WARNING 级别的日志，因为很多错误信息是以 WARNING 级别输出的
  bool shouldCollect = (level == AV_LOG_FATAL || level == AV_LOG_ERROR ||
                        level == AV_LOG_WARNING);

  if (!shouldCollect && level <= AV_LOG_INFO) {
    const char *errorKeywords[] = {"Invalid argument",
                                   "not a suitable",
                                   "Conversion failed",
                                   "pipe::",
                                   "[NULL @",
                                   "failed",
                                   "error",
                                   "Error",
                                   "ERROR",
                                   "cannot",
                                   "Cannot",
                                   "Unable",
                                   "unable",
                                   "Video:",
                                   "Input #"};
    for (size_t i = 0; i < sizeof(errorKeywords) / sizeof(errorKeywords[0]);
         i++) {
      if (strstr(buffer, errorKeywords[i]) != nullptr) {
        shouldCollect = true;
        break;
      }
    }
  }

  if (shouldCollect) {
    if (g_currentCallbackInfo != nullptr &&
        g_currentCallbackInfo->sessionId != 0) {
      appendSessionErrorMessage(g_currentCallbackInfo->sessionId, buffer);
    }
  }

  if (g_currentCallbackInfo != nullptr &&
      g_currentCallbackInfo->hasLogCallback &&
      g_currentCallbackInfo->utilsClass != nullptr &&
      g_currentCallbackInfo->sendLogMethodID != nullptr) {
    JNIEnv *env = getJNIEnv();
    if (env != nullptr) {
      jstring logMessage = env->NewStringUTF(buffer);
      env->CallStaticVoidMethod(g_currentCallbackInfo->utilsClass,
                                g_currentCallbackInfo->sendLogMethodID, level,
                                logMessage);
      env->DeleteLocalRef(logMessage);
    }
  }

  // 检查是否是进度信息（FFmpeg 的进度输出格式：size=... time=... bitrate=...
  // speed=...）
  if (g_currentCallbackInfo != nullptr &&
      g_currentCallbackInfo->hasProgressCallback &&
      g_currentCallbackInfo->utilsClass != nullptr &&
      g_currentCallbackInfo->sendProgressMethodID != nullptr) {
    std::string logLine(buffer);
    // 检查是否包含进度信息的关键字
    if (logLine.find("size=") != std::string::npos &&
        (logLine.find("time=") != std::string::npos ||
         logLine.find("bitrate=") != std::string::npos)) {
      // 提取进度信息
      std::string progressMsg = logLine;
      // 清理换行符和多余空格
      while (!progressMsg.empty() &&
             (progressMsg.back() == '\n' || progressMsg.back() == '\r' ||
              progressMsg.back() == ' ')) {
        progressMsg.pop_back();
      }
      if (!progressMsg.empty()) {
        JNIEnv *env = getJNIEnv();
        if (env != nullptr) {
          jstring message = env->NewStringUTF(progressMsg.c_str());
          env->CallStaticVoidMethod(g_currentCallbackInfo->utilsClass,
                                    g_currentCallbackInfo->sendProgressMethodID,
                                    message);
          env->DeleteLocalRef(message);
        }
      }
    }
  }

  // 仍然输出到 logcat（根据 g_logEnabled 开关控制）
  if (g_logEnabled) {
    if (level == AV_LOG_FATAL || level == AV_LOG_ERROR) {
      __android_log_vprint(ANDROID_LOG_ERROR, LOG_TAG, fmt, vl);
    } else if (level == AV_LOG_WARNING) {
      __android_log_vprint(ANDROID_LOG_WARN, LOG_TAG, fmt, vl);
    } else if (level == AV_LOG_INFO) {
      __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, fmt, vl);
    } else if (level == AV_LOG_VERBOSE) {
      __android_log_vprint(ANDROID_LOG_VERBOSE, LOG_TAG, fmt, vl);
    } else if (level == AV_LOG_DEBUG) {
      __android_log_vprint(ANDROID_LOG_DEBUG, LOG_TAG, fmt, vl);
    } else {
      __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, fmt, vl);
    }
  }
}

void log_call_back(void *ptr, int level, const char *fmt, va_list vl) {
  // 根据 g_logEnabled 开关控制是否输出日志
  if (g_logEnabled) {
    if (level == AV_LOG_FATAL || level == AV_LOG_ERROR) {
      __android_log_vprint(ANDROID_LOG_ERROR, LOG_TAG, fmt, vl);
    } else if (level == AV_LOG_WARNING) {
      __android_log_vprint(ANDROID_LOG_WARN, LOG_TAG, fmt, vl);
    } else if (level == AV_LOG_INFO) {
      __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, fmt, vl);
    } else if (level == AV_LOG_VERBOSE) {
      __android_log_vprint(ANDROID_LOG_VERBOSE, LOG_TAG, fmt, vl);
    } else if (level == AV_LOG_DEBUG) {
      __android_log_vprint(ANDROID_LOG_DEBUG, LOG_TAG, fmt, vl);
    } else {
      __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, fmt, vl);
    }
  }
}

void progressCallBack(int64_t handle, int what, float progress) {
  if (handle != 0) {
    struct CallBackInfo *onActionListener = (struct CallBackInfo *)(handle);
    JNIEnv *env = onActionListener->env;
    env->CallVoidMethod(onActionListener->obj, onActionListener->methodID,
                        progress);

    // 如果设置了 progress callback，也发送消息
    if (onActionListener->hasProgressCallback &&
        onActionListener->utilsClass != nullptr &&
        onActionListener->sendProgressMethodID != nullptr) {
      char buffer[256];
      snprintf(buffer, sizeof(buffer), "Progress: %.2f%%", progress * 100.0f);
      jstring message = env->NewStringUTF(buffer);
      env->CallStaticVoidMethod(onActionListener->utilsClass,
                                onActionListener->sendProgressMethodID,
                                message);
      env->DeleteLocalRef(message);
    }
  }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_getMediaDuration(JNIEnv *env,
                                                          jclass clazz,
                                                          jstring media_path) {
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
    av_log(NULL, AV_LOG_ERROR, "Cannot open input file mediaPath=%s",
           mediaPath);
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
    int64_t temp =
        stream->duration * 1000 * stream->time_base.num / stream->time_base.den;
    if (temp > videoDuration)
      videoDuration = temp;
  }
  if (NULL != in_fmt_ctx)
    avformat_close_input(&in_fmt_ctx);

  env->ReleaseStringUTFChars(media_path, mediaPath);
  return videoDuration;
}
// 新增：创建 Session
extern "C" JNIEXPORT jlong JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_createFFmpegSession(JNIEnv *env,
                                                             jclass clazz) {
  extern int64_t createFFmpegSession(void);
  return createFFmpegSession();
}

// 新增：通过 sessionId 执行命令
extern "C" JNIEXPORT jint JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_executeFFmpegCommandWithSession(
    JNIEnv *env, jclass clazz, jlong sessionId, jint cmdLen, jobjectArray argv,
    jobject actionCallBack, jlong totalTime) {
  int ret = 0;

  if (g_jvm == nullptr) {
    env->GetJavaVM(&g_jvm);
  }

  char *argCmd[cmdLen];
  jstring buf[cmdLen];

  for (int i = 0; i < cmdLen; ++i) {
    buf[i] = static_cast<jstring>(env->GetObjectArrayElement(argv, i));
    char *string =
        const_cast<char *>(env->GetStringUTFChars(buf[i], JNI_FALSE));
    argCmd[i] = string;
    LOGE("argCmd=%s", argCmd[i]);
  }

  if (NULL != actionCallBack) {
    jclass actionClass = env->GetObjectClass(actionCallBack);
    jmethodID progressMID = env->GetMethodID(actionClass, "progress", "(F)V");
    jmethodID failMID =
        env->GetMethodID(actionClass, "fail", "(ILjava/lang/String;)V");
    jmethodID successMID = env->GetMethodID(actionClass, "success", "()V");

    CallBackInfo onActionListener;
    onActionListener.env = env;
    onActionListener.obj = actionCallBack;
    onActionListener.methodID = progressMID;
    onActionListener.utilsClass = nullptr;
    onActionListener.sendLogMethodID = nullptr;
    onActionListener.sendProgressMethodID = nullptr;
    onActionListener.hasLogCallback = false;
    onActionListener.hasProgressCallback = false;
    onActionListener.sessionId = sessionId;

    // 总是设置 g_currentCallbackInfo 以便收集错误信息
    g_currentCallbackInfo = &onActionListener;
    av_log_set_callback(log_call_back_with_callback);

    extern int executeFFmpegCommandWithSession(
        int64_t sessionId, int argc, char **argv, int64_t handle,
        void (*progressCallBack)(int64_t, int, float), int64_t totalTime);
    ret = executeFFmpegCommandWithSession(sessionId, cmdLen, argCmd,
                                          (int64_t)(&onActionListener),
                                          progressCallBack, totalTime);

    // 清理全局变量（总是清理，因为我们总是设置了 g_currentCallbackInfo）
    g_currentCallbackInfo = nullptr;

    if (ret != 0) {
      char err[1024] = {0};
      av_strerror(ret, err, 1024);

      extern const char *getSessionErrorMessage(int64_t sessionId);
      const char *detailedError = getSessionErrorMessage(sessionId);
      char fullErrorMsg[10240] = {0};

      if (strlen(detailedError) > 0) {
        snprintf(fullErrorMsg, sizeof(fullErrorMsg),
                 "Error code: %d (%s)\n\nDetailed error log:\n%s", ret, err,
                 detailedError);
      } else {
        snprintf(fullErrorMsg, sizeof(fullErrorMsg), "Error code: %d (%s)", ret,
                 err);
      }

      jstring msg = env->NewStringUTF(fullErrorMsg);
      env->CallVoidMethod(actionCallBack, failMID, ret, msg);
      env->DeleteLocalRef(msg);
    } else {
      env->CallVoidMethod(actionCallBack, successMID);
    }
    env->DeleteLocalRef(actionClass);
  } else {
    extern int executeFFmpegCommandWithSession(
        int64_t sessionId, int argc, char **argv, int64_t handle,
        void (*progressCallBack)(int64_t, int, float), int64_t totalTime);
    ret = executeFFmpegCommandWithSession(sessionId, cmdLen, argCmd, 0, NULL,
                                          totalTime);
  }

  for (int i = 0; i < cmdLen; ++i) {
    env->ReleaseStringUTFChars(buf[i], argCmd[i]);
    env->DeleteLocalRef(buf[i]);
  }

  return ret;
}

// 新增：通过 sessionId 取消
extern "C" JNIEXPORT jint JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_cancelFFmpegCommandBySession(
    JNIEnv *env, jclass clazz, jlong sessionId) {
  extern int cancelFFmpegCommandBySession(int64_t sessionId);
  return cancelFFmpegCommandBySession(sessionId);
}

// 新增：获取 Session 错误信息
extern "C" JNIEXPORT jstring JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_getSessionErrorMessage(
    JNIEnv *env, jclass clazz, jlong sessionId) {
  extern const char *getSessionErrorMessage(int64_t sessionId);
  const char *errorMsg = getSessionErrorMessage(sessionId);
  return env->NewStringUTF(errorMsg);
}

// 新增：销毁 Session
extern "C" JNIEXPORT void JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_destroyFFmpegSession(JNIEnv *env,
                                                              jclass clazz,
                                                              jlong sessionId) {
  extern void destroyFFmpegSession(int64_t sessionId);
  destroyFFmpegSession(sessionId);
}

// 新增：设置日志输出开关
extern "C" JNIEXPORT void JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_setLogEnabled(JNIEnv *env,
                                                       jclass clazz,
                                                       jboolean enabled) {
  g_logEnabled = (enabled == JNI_TRUE);
}