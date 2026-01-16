/**
 * FFmpeg JNI 接口层
 * 提供 Session 管理和 FFmpeg 命令执行功能
 */

#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

// FFmpeg headers for getMediaDuration
#include "libavformat/avformat.h"
#include "libavutil/log.h"

#define LOG_TAG "tiny_ffmpeg"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)

// 全局日志开关
static volatile int g_logEnabled = 1;
static pthread_mutex_t g_logMutex = PTHREAD_MUTEX_INITIALIZER;

// 当前执行的 sessionId（用于日志收集）
static volatile int64_t g_currentSessionId = 0;
static pthread_mutex_t g_sessionIdMutex = PTHREAD_MUTEX_INITIALIZER;

// 前向声明
void appendSessionErrorMessage(int64_t sessionId, const char *message);

/**
 * 自定义 FFmpeg 日志回调函数
 * 将 FFmpeg 的日志重定向到 Android logcat，并收集到 Session 错误缓冲区
 */
static void ffmpeg_log_callback(void *ptr, int level, const char *fmt,
                                va_list vl) {
  // 检查日志级别
  if (level > av_log_get_level()) {
    return;
  }

  // 格式化日志消息
  char line[1024];
  vsnprintf(line, sizeof(line), fmt, vl);

  // 移除末尾的换行符
  size_t len = strlen(line);
  if (len > 0 && line[len - 1] == '\n') {
    line[len - 1] = '\0';
    len--;
  }

  // 跳过空消息
  if (len == 0) {
    return;
  }

  // 收集日志到 Session 错误缓冲区（参考 iOS 实现）
  int shouldCollect = 0;
  
  // 收集 ERROR、FATAL 和 WARNING 级别的日志
  if (level == AV_LOG_FATAL || level == AV_LOG_ERROR || level == AV_LOG_WARNING) {
    shouldCollect = 1;
  }
  
  // 也收集包含关键信息的 INFO 级别日志
  if (!shouldCollect && level <= AV_LOG_INFO) {
    const char *keywords[] = {
      "Video:", "Audio:", "Input #", "Stream #",
      "Duration:", "bitrate:",
      "Invalid argument", "not a suitable", "Conversion failed",
      "failed", "error", "Error", "ERROR",
      "cannot", "Cannot", "Unable", "unable"
    };
    int numKeywords = sizeof(keywords) / sizeof(keywords[0]);
    for (int i = 0; i < numKeywords; i++) {
      if (strstr(line, keywords[i]) != NULL) {
        shouldCollect = 1;
        break;
      }
    }
  }
  
  // 如果需要收集，添加到当前 session 的错误缓冲区
  if (shouldCollect) {
    pthread_mutex_lock(&g_sessionIdMutex);
    int64_t currentSessionId = g_currentSessionId;
    pthread_mutex_unlock(&g_sessionIdMutex);
    
    LOGD("[LogCollect] shouldCollect=1, sessionId=%lld, line=%s", (long long)currentSessionId, line);
    if (currentSessionId > 0) {
      appendSessionErrorMessage(currentSessionId, line);
      appendSessionErrorMessage(currentSessionId, "\n");
    }
  }

  // 检查日志开关
  pthread_mutex_lock(&g_logMutex);
  int enabled = g_logEnabled;
  pthread_mutex_unlock(&g_logMutex);

  if (!enabled) {
    return;
  }

  // 根据 FFmpeg 日志级别映射到 Android 日志级别
  switch (level) {
  case AV_LOG_PANIC:
  case AV_LOG_FATAL:
  case AV_LOG_ERROR:
    LOGE("[FFmpeg] %s", line);
    break;
  case AV_LOG_WARNING:
    LOGW("[FFmpeg] %s", line);
    break;
  case AV_LOG_INFO:
    LOGI("[FFmpeg] %s", line);
    break;
  case AV_LOG_VERBOSE:
    LOGV("[FFmpeg] %s", line);
    break;
  case AV_LOG_DEBUG:
  case AV_LOG_TRACE:
  default:
    LOGD("[FFmpeg] %s", line);
    break;
  }
}

// 声明 ffmpeg.c 中的函数
extern int64_t get_ffmpeg_current_session_id(void);
extern void set_ffmpeg_current_session_id(int64_t sessionId);
extern void cancel_ffmpeg_cmd(void);
extern int exe_ffmpeg_cmd_with_session(int64_t sessionId, int argc,
                                       char **argv);

// Session 管理
#define MAX_SESSIONS 64
#define MAX_ERROR_BUFFER_SIZE 8192

typedef struct {
  int64_t sessionId;
  volatile int cancelled;
  char errorBuffer[MAX_ERROR_BUFFER_SIZE];
  int errorBufferLen;
  pthread_mutex_t errorMutex;
  int inUse;
} SessionContext;

static SessionContext sessions[MAX_SESSIONS];
static pthread_mutex_t sessionsMutex = PTHREAD_MUTEX_INITIALIZER;
static int64_t nextSessionId = 1;

// 初始化 sessions 数组
static int sessionsInitialized = 0;
static void initSessions() {
  if (sessionsInitialized)
    return;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    sessions[i].inUse = 0;
    pthread_mutex_init(&sessions[i].errorMutex, NULL);
  }
  sessionsInitialized = 1;

  // 初始化时设置自定义日志回调，将 FFmpeg 日志重定向到 Android logcat
  av_log_set_callback(ffmpeg_log_callback);
  av_log_set_level(AV_LOG_INFO);
  LOGI("FFmpeg log callback initialized");
}

// 创建 FFmpeg Session
int64_t createFFmpegSession() {
  initSessions();
  pthread_mutex_lock(&sessionsMutex);
  int64_t sessionId = nextSessionId++;

  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].inUse) {
      sessions[i].sessionId = sessionId;
      sessions[i].cancelled = 0;
      sessions[i].errorBuffer[0] = '\0';
      sessions[i].errorBufferLen = 0;
      sessions[i].inUse = 1;
      pthread_mutex_unlock(&sessionsMutex);
      return sessionId;
    }
  }
  pthread_mutex_unlock(&sessionsMutex);
  return -1;
}

// 被 ffmpeg.c 调用的函数
int isSessionCancelled(int64_t sessionId) {
  pthread_mutex_lock(&sessionsMutex);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
      int cancelled = sessions[i].cancelled;
      pthread_mutex_unlock(&sessionsMutex);
      return cancelled;
    }
  }
  pthread_mutex_unlock(&sessionsMutex);
  return 0;
}

void appendSessionErrorMessage(int64_t sessionId, const char *message) {
  if (message == NULL || strlen(message) == 0)
    return;

  pthread_mutex_lock(&sessionsMutex);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
      pthread_mutex_lock(&sessions[i].errorMutex);

      int remaining = MAX_ERROR_BUFFER_SIZE - sessions[i].errorBufferLen - 1;
      if (remaining > 0) {
        int msgLen = (int)strlen(message);
        int toCopy = remaining < msgLen ? remaining : msgLen;
        strncat(sessions[i].errorBuffer, message, toCopy);
        sessions[i].errorBufferLen += toCopy;
        sessions[i].errorBuffer[sessions[i].errorBufferLen] = '\0';
      }

      pthread_mutex_unlock(&sessions[i].errorMutex);
      break;
    }
  }
  pthread_mutex_unlock(&sessionsMutex);
}

int cancelFFmpegCommandBySession(int64_t sessionId) {
  pthread_mutex_lock(&sessionsMutex);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
      sessions[i].cancelled = 1;
      // 如果这是当前正在执行的 session，也触发全局取消
      if (get_ffmpeg_current_session_id() == sessionId) {
        cancel_ffmpeg_cmd();
      }
      pthread_mutex_unlock(&sessionsMutex);
      return 0;
    }
  }
  pthread_mutex_unlock(&sessionsMutex);
  return -1;
}

const char *getSessionErrorMessage(int64_t sessionId) {
  static char empty[] = "";
  pthread_mutex_lock(&sessionsMutex);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
      pthread_mutex_lock(&sessions[i].errorMutex);
      const char *msg = sessions[i].errorBuffer;
      pthread_mutex_unlock(&sessions[i].errorMutex);
      pthread_mutex_unlock(&sessionsMutex);
      return msg;
    }
  }
  pthread_mutex_unlock(&sessionsMutex);
  return empty;
}

void destroyFFmpegSession(int64_t sessionId) {
  pthread_mutex_lock(&sessionsMutex);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].sessionId == sessionId) {
      sessions[i].inUse = 0;
      break;
    }
  }
  pthread_mutex_unlock(&sessionsMutex);
}

// 执行 FFmpeg 命令（带 Session）
int executeFFmpegCommandWithSession(int64_t sessionId, int argc, char **argv) {
  LOGI("[DEBUG] executeFFmpegCommandWithSession called, sessionId=%lld", (long long)sessionId);
  
  // 设置当前 sessionId（用于日志收集）
  pthread_mutex_lock(&g_sessionIdMutex);
  g_currentSessionId = sessionId;
  pthread_mutex_unlock(&g_sessionIdMutex);
  
  LOGI("[DEBUG] g_currentSessionId set to %lld", (long long)g_currentSessionId);

  // 清空该 session 的错误缓冲区
  pthread_mutex_lock(&sessionsMutex);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
      pthread_mutex_lock(&sessions[i].errorMutex);
      sessions[i].errorBuffer[0] = '\0';
      sessions[i].errorBufferLen = 0;
      sessions[i].cancelled = 0;
      pthread_mutex_unlock(&sessions[i].errorMutex);
      break;
    }
  }
  pthread_mutex_unlock(&sessionsMutex);

  int ret = exe_ffmpeg_cmd_with_session(sessionId, argc, argv);

  // 清除当前 sessionId
  pthread_mutex_lock(&g_sessionIdMutex);
  g_currentSessionId = 0;
  pthread_mutex_unlock(&g_sessionIdMutex);

  return ret;
}

// ==================== JNI 接口 ====================

JNIEXPORT jlong JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_createFFmpegSession(JNIEnv *env,
                                                             jclass clazz) {
  return createFFmpegSession();
}

JNIEXPORT jint JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_executeFFmpegCommandWithSession(
    JNIEnv *env, jclass clazz, jlong sessionId, jint argc, jobjectArray argv,
    jobject listener, jlong totalTime) {

  // 转换 Java String 数组到 C char** 数组
  char **argvC = (char **)malloc(argc * sizeof(char *));
  for (int i = 0; i < argc; i++) {
    jstring str = (jstring)(*env)->GetObjectArrayElement(env, argv, i);
    const char *cStr = (*env)->GetStringUTFChars(env, str, NULL);
    argvC[i] = strdup(cStr);
    (*env)->ReleaseStringUTFChars(env, str, cStr);
    (*env)->DeleteLocalRef(env, str);
  }

  int ret = executeFFmpegCommandWithSession(sessionId, argc, argvC);

  // 释放内存
  for (int i = 0; i < argc; i++) {
    free(argvC[i]);
  }
  free(argvC);

  return ret;
}

JNIEXPORT jint JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_cancelFFmpegCommandBySession(
    JNIEnv *env, jclass clazz, jlong sessionId) {
  return cancelFFmpegCommandBySession(sessionId);
}

JNIEXPORT jstring JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_getSessionErrorMessage(
    JNIEnv *env, jclass clazz, jlong sessionId) {
  const char *msg = getSessionErrorMessage(sessionId);
  return (*env)->NewStringUTF(env, msg);
}

JNIEXPORT void JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_destroyFFmpegSession(JNIEnv *env,
                                                              jclass clazz,
                                                              jlong sessionId) {
  destroyFFmpegSession(sessionId);
}

JNIEXPORT jlong JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_getMediaDuration(JNIEnv *env,
                                                          jclass clazz,
                                                          jstring mediaPath) {
  if (mediaPath == NULL) {
    return -1;
  }

  const char *path = (*env)->GetStringUTFChars(env, mediaPath, NULL);
  if (path == NULL) {
    return -1;
  }

  jlong duration = -1;
  AVFormatContext *formatContext = NULL;

  // 打开媒体文件
  int ret = avformat_open_input(&formatContext, path, NULL, NULL);
  if (ret < 0) {
    LOGE("getMediaDuration: avformat_open_input failed for %s, error: %d", path,
         ret);
    (*env)->ReleaseStringUTFChars(env, mediaPath, path);
    return -1;
  }

  // 获取流信息
  ret = avformat_find_stream_info(formatContext, NULL);
  if (ret < 0) {
    LOGE("getMediaDuration: avformat_find_stream_info failed, error: %d", ret);
    avformat_close_input(&formatContext);
    (*env)->ReleaseStringUTFChars(env, mediaPath, path);
    return -1;
  }

  // 获取时长（单位：微秒），转换为毫秒
  if (formatContext->duration != AV_NOPTS_VALUE) {
    duration = formatContext->duration / 1000; // 微秒转毫秒
  }

  // 清理
  avformat_close_input(&formatContext);
  (*env)->ReleaseStringUTFChars(env, mediaPath, path);

  return duration;
}

JNIEXPORT void JNICALL Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_setLogEnabled(
    JNIEnv *env, jclass clazz, jboolean enabled) {
  // 确保日志回调已初始化
  initSessions();

  pthread_mutex_lock(&g_logMutex);
  g_logEnabled = enabled ? 1 : 0;
  pthread_mutex_unlock(&g_logMutex);

  // 设置 FFmpeg 日志级别
  if (enabled) {
    av_log_set_level(AV_LOG_INFO);
  } else {
    av_log_set_level(AV_LOG_QUIET);
  }

  LOGI("setLogEnabled: %s", enabled ? "true" : "false");
}
