#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>

#include "ffmpeg_utils.h"
#include "ffmpeg.h"
#include "ffmpeg_cmd.h"

#define LOG_TAG "FFMpegUtils"

#define LOGE(format, ...) printf(LOG_TAG format "\n", ##__VA_ARGS__)
#define LOGI(format, ...) printf(LOG_TAG format "\n", ##__VA_ARGS__)

// 声明 Swift 回调函数
void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_log(int32_t logLevel,
                                                 const char *logMessage);
void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progressMessage(
    const char *message);

// CallBackInfo 已在 ffmpeg_utils.h 中定义

// 全局变量存储当前的 CallBackInfo
static CallBackInfo *g_currentCallbackInfo = NULL;

// 全局变量控制是否输出日志到控制台（默认开启）
static bool g_logEnabled = false;

// 声明 Session 管理函数
extern void appendSessionErrorMessage(int64_t sessionId, const char *message);

void log_call_back_with_callback(void *ptr, int level, const char *fmt,
                                 va_list vl) {
  char buffer[4096];
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
      if (strstr(buffer, errorKeywords[i]) != NULL) {
        shouldCollect = true;
        break;
      }
    }
  }

  if (shouldCollect) {
    if (g_currentCallbackInfo != NULL &&
        g_currentCallbackInfo->sessionId != 0) {
      appendSessionErrorMessage(g_currentCallbackInfo->sessionId, buffer);
    }
  }

  if (g_currentCallbackInfo != NULL &&
      g_currentCallbackInfo->hasLogCallback) {
    Java_com_i7play_tiny_ffmpeg_FFMpegUtils_log(level, buffer);
  }

  // 检查是否是进度信息（FFmpeg 的进度输出格式：size=... time=... bitrate=...
  // speed=...）
  if (g_currentCallbackInfo != NULL &&
      g_currentCallbackInfo->hasProgressCallback) {
    // 检查是否包含进度信息的关键字
    if (strstr(buffer, "size=") != NULL &&
        (strstr(buffer, "time=") != NULL ||
         strstr(buffer, "bitrate=") != NULL)) {
      // 提取进度信息并清理换行符和多余空格
      char progressMsg[1024];
      strncpy(progressMsg, buffer, sizeof(progressMsg) - 1);
      progressMsg[sizeof(progressMsg) - 1] = '\0';
      
      // 清理末尾的换行符和空格
      int len = strlen(progressMsg);
      while (len > 0 && (progressMsg[len-1] == '\n' || 
                         progressMsg[len-1] == '\r' || 
                         progressMsg[len-1] == ' ')) {
        progressMsg[--len] = '\0';
      }
      
      if (len > 0) {
        Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progressMessage(progressMsg);
      }
    }
  }

  // 仍然输出到控制台（根据 g_logEnabled 开关控制）
  if (g_logEnabled) {
    if (level == AV_LOG_FATAL || level == AV_LOG_ERROR) {
      vprintf(fmt, vl);
    } else if (level == AV_LOG_WARNING) {
      vprintf(fmt, vl);
    } else if (level == AV_LOG_INFO) {
      vprintf(fmt, vl);
    } else if (level == AV_LOG_VERBOSE) {
      vprintf(fmt, vl);
    } else if (level == AV_LOG_DEBUG) {
      vprintf(fmt, vl);
    } else {
      vprintf(fmt, vl);
    }
  }
}

void log_call_back(void *ptr, int level, const char *fmt, va_list vl) {
  // 根据 g_logEnabled 开关控制是否输出日志
  if (g_logEnabled) {
    if (level == AV_LOG_FATAL || level == AV_LOG_ERROR) {
      vprintf(fmt, vl);
    } else if (level == AV_LOG_WARNING) {
      vprintf(fmt, vl);
    } else if (level == AV_LOG_INFO) {
      vprintf(fmt, vl);
    } else if (level == AV_LOG_VERBOSE) {
      vprintf(fmt, vl);
    } else if (level == AV_LOG_DEBUG) {
      vprintf(fmt, vl);
    } else {
      vprintf(fmt, vl);
    }
  }
}

void progressCallBack(int64_t handle, int what, float progress) {
  Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progress(progress);

  // 如果设置了 progress callback，也发送消息
  if (handle != 0) {
    CallBackInfo *info = (CallBackInfo *)handle;
    if (info != NULL && info->hasProgressCallback) {
      char buffer[256];
      snprintf(buffer, sizeof(buffer), "Progress: %.2f%%", progress * 100.0f);
      Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progressMessage(buffer);
    }
  }
}

// 新增：通过 sessionId 执行命令
int64_t Java_com_i7play_tiny_ffmpeg_FFMpegUtils_executeFFmpegCommandWithSession(
    int64_t sessionId, int cmdLen, char *argv[], long totalTime) {
  int ret = 0;

  for (int i = 0; i < cmdLen; ++i) {
    LOGE("argCmd=%s\n", argv[i]);
  }

  CallBackInfo onActionListener;
  onActionListener.hasLogCallback = false;
  onActionListener.hasProgressCallback = false;
  onActionListener.sessionId = sessionId;

  // 总是设置 g_currentCallbackInfo 以便收集错误信息
  g_currentCallbackInfo = &onActionListener;
  av_log_set_callback(log_call_back_with_callback);

  extern int executeFFmpegCommandWithSession(
      int64_t sessionId, int argc, char **argv, int64_t handle,
      void (*progressCallBack)(int64_t, int, float), int64_t totalTime);
  ret = executeFFmpegCommandWithSession(sessionId, cmdLen, argv,
                                        (int64_t)(&onActionListener),
                                        progressCallBack, totalTime);

  // 清理全局变量（总是清理，因为我们总是设置了 g_currentCallbackInfo）
  g_currentCallbackInfo = NULL;

  char fullErrorMsg[10240] = {0};

  if (ret == 0) {
    // 成功时不需要错误信息
    snprintf(fullErrorMsg, sizeof(fullErrorMsg), "Success");
  } else {
    char err[1024] = {0};
    av_strerror(ret, err, 1024);

    extern const char *getSessionErrorMessage(int64_t sessionId);
    const char *detailedError = getSessionErrorMessage(sessionId);

    if (strlen(detailedError) > 0) {
      snprintf(fullErrorMsg, sizeof(fullErrorMsg),
               "Error code: %d (%s)\n\nDetailed error log:\n%s", ret, err,
               detailedError);
    } else {
      snprintf(fullErrorMsg, sizeof(fullErrorMsg), "Error code: %d (%s)", ret,
               err);
    }
  }

  Java_com_i7play_tiny_ffmpeg_FFMpegUtils_result(sessionId, ret, fullErrorMsg);

  return ret;
}

// 新增：创建 Session
int64_t Java_com_i7play_tiny_ffmpeg_FFMpegUtils_createFFmpegSession() {
  extern int64_t createFFmpegSession(void);
  return createFFmpegSession();
}

// 新增：通过 sessionId 取消
int Java_com_i7play_tiny_ffmpeg_FFMpegUtils_cancelFFmpegCommandBySession(
    int64_t sessionId) {
  extern int cancelFFmpegCommandBySession(int64_t sessionId);
  return cancelFFmpegCommandBySession(sessionId);
}

// 新增：获取 Session 错误信息
const char *Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getSessionErrorMessage(
    int64_t sessionId) {
  extern const char *getSessionErrorMessage(int64_t sessionId);
  return getSessionErrorMessage(sessionId);
}

// 新增：销毁 Session
void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_destroyFFmpegSession(
    int64_t sessionId) {
  extern void destroyFFmpegSession(int64_t sessionId);
  destroyFFmpegSession(sessionId);
}

// 新增：设置日志输出开关
void Java_com_i7play_tiny_ffmpeg_FFMpegUtils_setLogEnabled(int enabled) {
  g_logEnabled = (enabled != 0);
}

long Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getMediaDuration(
    const char *mediaPath) {
  if (NULL == mediaPath) {
    return -1;
  }
  // iOS: 新版 FFmpeg 会自动注册，不需要手动调用
  // if (!hasRegistered) {
  //   av_register_all();
  //   avcodec_register_all();
  //   avfilter_register_all();
  //   avformat_network_init();
  //   hasRegistered = true;
  // }
  // const char *mediaPath = env->GetStringUTFChars(media_path, 0);
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

  // env->ReleaseStringUTFChars(media_path, mediaPath);
  return videoDuration;
}
