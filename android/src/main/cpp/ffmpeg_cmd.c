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
// 与其他平台复用相同的媒体探测规则。
#include "tiny_media_info.h"

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
    
    // 错误缓冲独立保留，关闭日志时不额外输出包含用户路径的收集调试信息。
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
// FFmpeg 前端全局状态必须跨插件实例串行使用。
static pthread_mutex_t executionMutex = PTHREAD_MUTEX_INITIALIZER;
static int64_t nextSessionId = 1;

// 初始化 sessions 数组
// 多个 Flutter Engine 首次同时使用插件时，也只能初始化一次原生锁。
static pthread_once_t sessionsOnce = PTHREAD_ONCE_INIT;
static void initializeSessions(void) {
  for (int i = 0; i < MAX_SESSIONS; i++) {
    pthread_mutex_init(&sessions[i].errorMutex, NULL);
  }
  av_log_set_callback(ffmpeg_log_callback);
  av_log_set_level(AV_LOG_INFO);
}
static void initSessions(void) {
  pthread_once(&sessionsOnce, initializeSessions);
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
  pthread_mutex_lock(&executionMutex);
  // -version 等命令会替换全局日志回调，每次执行都恢复 Session 错误收集。
  av_log_set_callback(ffmpeg_log_callback);
  av_log_set_level(AV_LOG_INFO);
  
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
      // 取消状态由创建 Session 初始化，执行时不能覆盖排队取消。
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

  pthread_mutex_unlock(&executionMutex);
  return ret;
}

// ==================== JNI 接口 ====================

// print_report 在调用 JNI 的执行线程中运行，局部引用仅在本次执行期间有效。
static _Thread_local JNIEnv *progressEnv;
static _Thread_local jclass progressClass;
static _Thread_local jmethodID progressMethod;
void tiny_ffmpeg_report_progress(int64_t id, int64_t time, int64_t size, double speed)
{
  if (progressEnv && progressMethod) {
    (*progressEnv)->CallStaticVoidMethod(progressEnv, progressClass, progressMethod,
        (jlong)id, (jlong)(time / 1000), (jlong)size, (jdouble)speed);
    if ((*progressEnv)->ExceptionCheck(progressEnv)) (*progressEnv)->ExceptionClear(progressEnv);
  }
}

JNIEXPORT jstring JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_getMediaInfo(JNIEnv *env, jclass clazz, jstring mediaPath)
{
  if (!mediaPath) return (*env)->NewStringUTF(env, "{\"error\":-22}");
  const char *path = (*env)->GetStringUTFChars(env, mediaPath, NULL);
  if (!path) return NULL;
  char json[2048];
  tiny_media_info(path, json, sizeof(json));
  (*env)->ReleaseStringUTFChars(env, mediaPath, path);
  return (*env)->NewStringUTF(env, json);
}

JNIEXPORT jlong JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_createFFmpegSession(JNIEnv *env,
                                                             jclass clazz) {
  return createFFmpegSession();
}

JNIEXPORT jint JNICALL
Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_executeFFmpegCommandWithSession(
    JNIEnv *env, jclass clazz, jlong sessionId, jint argc, jobjectArray argv,
    jobject listener, jlong totalTime) {

  // FFmpeg 会读取 argv[argc]，必须提供空指针哨兵，避免 Release 构建越界读取。
  if (!argv || argc <= 0 || argc != (*env)->GetArrayLength(env, argv))
    return AVERROR(EINVAL);
  char **argvC = (char **)calloc((size_t)argc + 1, sizeof(char *));
  if (!argvC) return AVERROR(ENOMEM);
  int ret = AVERROR(ENOMEM);
  for (int i = 0; i < argc; i++) {
    jstring str = (jstring)(*env)->GetObjectArrayElement(env, argv, i);
    if (!str) goto free_arguments;
    const char *cStr = (*env)->GetStringUTFChars(env, str, NULL);
    if (!cStr) {
      (*env)->DeleteLocalRef(env, str);
      goto free_arguments;
    }
    argvC[i] = strdup(cStr);
    (*env)->ReleaseStringUTFChars(env, str, cStr);
    (*env)->DeleteLocalRef(env, str);
    if (!argvC[i]) goto free_arguments;
  }

  progressEnv = env;
  progressClass = clazz;
  progressMethod = (*env)->GetStaticMethodID(env, clazz, "onNativeProgress", "(JJJD)V");
  ret = executeFFmpegCommandWithSession(sessionId, argc, argvC);
  progressEnv = NULL;
  progressClass = NULL;
  progressMethod = NULL;

  // calloc 保证部分分配失败时也能释放所有已创建的字符串。
free_arguments:
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

  // 三端共用轻量探测，文件损坏和未知时长均返回 -1。
  jlong duration = tiny_media_duration(path);
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

  // 关闭控制台输出仍要收集错误，避免失败结果只剩下一个返回码。
  av_log_set_level(AV_LOG_INFO);
}
