#include <aki/jsbind.h>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "hilog/log.h"

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x3200     // 全局domain宏，标识业务领域
#define LOG_TAG "tiny_ffmpeg" // 全局tag宏，标识模块日志tag

extern "C" {
#include "ffmpeg/ffmpeg.h"
#include "libavfilter/avfilter.h"
#include "libavformat/avformat.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
// int exe_ffmpeg_cmd(int argc, char **argv,
//                    int64_t handle, void (*progressCallBack)(int64_t, int,
//                    float), int64_t totalTime);

// 声明 C 函数（在 ffmpeg.c 中定义）
int64_t get_ffmpeg_current_session_id(void);
void set_ffmpeg_current_session_id(int64_t sessionId);
void cancel_ffmpeg_cmd(void);
int exe_ffmpeg_cmd_with_session(int64_t sessionId, int argc, char **argv);
}

char **vector_to_argv(const std::vector<std::string> &args) {
  // Calculate the number of arguments (argc)
  int argc = args.size();

  // Allocate pointer array (size argc + 1, because the last element is NULL)
  char **argv = new char *[argc + 1];

  // Fill the pointer array
  for (int i = 0; i < argc; ++i) {
    // Allocate memory for each string and copy its content
    argv[i] = new char[args[i].length() + 1];
    strcpy(argv[i], args[i].c_str());
  }

  // Set the last element to NULL
  argv[argc] = NULL;

  return argv;
}

typedef struct CallBackInfo {
  const aki::JSFunction *onFFmpegProgress;
  const aki::JSFunction *onFFmpegLog;
  const aki::JSFunction *onFFmpegProgressMessage;
  bool hasLogCallback;
  bool hasProgressCallback;
  int64_t sessionId;
} CallBackInfo;

#include <fstream>
#include <sstream>

// 全局变量控制是否输出日志到控制台（默认开启）
static bool g_logEnabled = true;
static std::mutex g_logEnabledMutex;  // 保护 g_logEnabled 的访问

// Session 管理（完整版本，与其他平台同步）
#define MAX_SESSIONS 64
#define MAX_ERROR_BUFFER_SIZE 8192

typedef struct {
  int64_t sessionId;
  volatile int cancelled;
  char errorBuffer[MAX_ERROR_BUFFER_SIZE];
  int errorBufferLen;
  std::mutex errorMutex;
  int inUse;
  // 存储回调信息，用于日志回调
  CallBackInfo callbackInfo;
  std::mutex callbackMutex;  // 保护 callbackInfo 的访问
} SessionContext;

static SessionContext sessions[MAX_SESSIONS];
static std::mutex sessionsMutex;
static int64_t nextSessionId = 1;
static int64_t currentExecutingSessionId = 0;

int64_t createFFmpegSession() {
  std::lock_guard<std::mutex> lock(sessionsMutex);
  int64_t sessionId = nextSessionId++;

  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].inUse) {
      sessions[i].sessionId = sessionId;
      sessions[i].cancelled = 0;
      sessions[i].errorBuffer[0] = '\0';
      sessions[i].errorBufferLen = 0;
      // 初始化 callbackInfo
      sessions[i].callbackInfo.onFFmpegProgress = nullptr;
      sessions[i].callbackInfo.onFFmpegLog = nullptr;
      sessions[i].callbackInfo.onFFmpegProgressMessage = nullptr;
      sessions[i].callbackInfo.hasLogCallback = false;
      sessions[i].callbackInfo.hasProgressCallback = false;
      sessions[i].callbackInfo.sessionId = sessionId;
      sessions[i].inUse = 1;
      return sessionId;
    }
  }
  return -1;
}

// 需要被 C 代码调用的函数，使用 extern "C"
extern "C" {
int isSessionCancelled(int64_t sessionId) {
  std::lock_guard<std::mutex> lock(sessionsMutex);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
      return sessions[i].cancelled;
    }
  }
  return 0;
}

void appendSessionErrorMessage(int64_t sessionId, const char *message) {
  if (message == nullptr || strlen(message) == 0)
    return;

  std::lock_guard<std::mutex> lock(sessionsMutex);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    // 即使 session 被销毁，也要允许追加错误信息（通过 sessionId 匹配）
    if (sessions[i].sessionId == sessionId) {
      std::lock_guard<std::mutex> errorLock(sessions[i].errorMutex);

      int remaining = MAX_ERROR_BUFFER_SIZE - sessions[i].errorBufferLen - 1;
      if (remaining > 0) {
        int msgLen = (int)strlen(message);
        int toCopy = remaining < msgLen ? remaining : msgLen;
        strncat(sessions[i].errorBuffer, message, toCopy);
        sessions[i].errorBufferLen += toCopy;
        sessions[i].errorBuffer[sessions[i].errorBufferLen] = '\0';
      }
      break;
    }
  }
}
} // extern "C"

int cancelFFmpegCommandBySession(int64_t sessionId) {
  std::lock_guard<std::mutex> lock(sessionsMutex);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
      sessions[i].cancelled = 1;
      // 如果这是当前正在执行的 session，也触发全局取消
      if (get_ffmpeg_current_session_id() == sessionId) {
        cancel_ffmpeg_cmd();
      }
      return 0;
    }
  }
  return -1;
}

// 注意：JSBind 需要返回 std::string，aki 会自动转换
std::string getSessionErrorMessage(int64_t sessionId) {
  std::lock_guard<std::mutex> lock(sessionsMutex);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    // 即使 session 被销毁，也要尝试返回错误信息（通过 sessionId 匹配）
    if (sessions[i].sessionId == sessionId) {
      std::lock_guard<std::mutex> errorLock(sessions[i].errorMutex);
      if (sessions[i].errorBufferLen > 0 &&
          strlen(sessions[i].errorBuffer) > 0) {
        return std::string(sessions[i].errorBuffer);
      }
    }
  }
  return "";
}

void destroyFFmpegSession(int64_t sessionId) {
  std::lock_guard<std::mutex> lock(sessionsMutex);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].sessionId == sessionId) {
      // 只清除 inUse 标志，保留错误缓冲区以便后续查询
      sessions[i].inUse = 0;
      break;
    }
  }
}

int64_t getCurrentExecutingSessionId(void) {
  // 从 ffmpeg.c 获取当前执行的 sessionId
  return get_ffmpeg_current_session_id();
}

void setCurrentExecutingSessionId(int64_t sessionId) {
  currentExecutingSessionId = sessionId;
  // 同时设置 ffmpeg.c 中的 sessionId
  set_ffmpeg_current_session_id(sessionId);
}

void log_call_back_with_callback(void *ptr, int level, const char *fmt,
                                 va_list vl) {
  // 使用 av_log_format_line 格式化日志用于输出和回调
  static int print_prefix = 1;
  char line[8192];
  va_list vl_copy;
  va_copy(vl_copy, vl);
  av_log_format_line(ptr, level, fmt, vl_copy, line, sizeof(line),
                     &print_prefix);
  va_end(vl_copy);

  // 通过当前 sessionId 查找对应的 CallBackInfo（线程安全）
  int64_t currentSessionId = get_ffmpeg_current_session_id();
  CallBackInfo *callbackInfo = nullptr;
  
  if (currentSessionId != 0) {
    std::lock_guard<std::mutex> lock(sessionsMutex);
    for (int i = 0; i < MAX_SESSIONS; i++) {
      if (sessions[i].sessionId == currentSessionId && sessions[i].inUse) {
        std::lock_guard<std::mutex> callbackLock(sessions[i].callbackMutex);
        callbackInfo = &sessions[i].callbackInfo;
        break;
      }
    }
  }

  // 收集 ERROR 和 FATAL 级别的日志到Session错误缓冲区
  // 同时也收集 WARNING 级别的日志，因为很多错误信息是以 WARNING 级别输出的
  // 另外收集包含错误关键词的 INFO 级别日志（如 "Invalid argument", "not a
  // suitable" 等）
  bool shouldCollect = (level == AV_LOG_FATAL || level == AV_LOG_ERROR ||
                        level == AV_LOG_WARNING);

  // 对于 INFO 级别及以下的日志，检查是否包含错误关键词
  if (!shouldCollect && level <= AV_LOG_INFO) {
    const char *errorKeywords[] = {
        "Invalid argument",
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
        "Input #",
        "muxer",
        "muxing",
        "stream error",
    };
    for (size_t i = 0; i < sizeof(errorKeywords) / sizeof(errorKeywords[0]);
         i++) {
      if (strstr(line, errorKeywords[i]) != nullptr) {
        shouldCollect = true;
        break;
      }
    }
  }

  if (shouldCollect && currentSessionId != 0) {
    // 添加换行符，确保错误信息格式正确
    char errorLine[8194];
    snprintf(errorLine, sizeof(errorLine), "%s\n", line);
    appendSessionErrorMessage(currentSessionId, errorLine);
  }

  // 如果设置了 log callback，调用它（使用格式化后的 line）
  if (callbackInfo != nullptr && callbackInfo->hasLogCallback &&
      callbackInfo->onFFmpegLog != nullptr) {
    callbackInfo->onFFmpegLog->Invoke<void>(level, std::string(line));
  }

  // 检查是否是进度信息（FFmpeg 的进度输出格式：size=... time=... bitrate=...
  // speed=...）
  if (callbackInfo != nullptr && callbackInfo->hasProgressCallback &&
      callbackInfo->onFFmpegProgressMessage != nullptr) {
    std::string logLine(line);
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
        callbackInfo->onFFmpegProgressMessage->Invoke<void>(progressMsg);
      }
    }
  }

  // 仍然输出到日志（根据 g_logEnabled 开关控制，线程安全）
  {
    std::lock_guard<std::mutex> lock(g_logEnabledMutex);
    if (g_logEnabled) {
      OH_LOG_ERROR(LOG_APP, "========> %{public}s", line);
    }
  }
}

// 新增：通过 sessionId 执行命令
int executeFFmpegCommandWithSession(int64_t sessionId, std::string uuid,
                                    int cmdLen, std::vector<std::string> argv) {
  char **argv1 = vector_to_argv(argv);

  setCurrentExecutingSessionId(sessionId);

  // 清空该session的错误缓冲区和取消标志
  {
    std::lock_guard<std::mutex> lock(sessionsMutex);
    for (int i = 0; i < MAX_SESSIONS; i++) {
      if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
        std::lock_guard<std::mutex> errorLock(sessions[i].errorMutex);
        sessions[i].errorBuffer[0] = '\0';
        sessions[i].errorBufferLen = 0;
        sessions[i].cancelled = 0;
        break;
      }
    }
  }

  // 将回调信息存储到 SessionContext 中（线程安全）
  {
    std::lock_guard<std::mutex> lock(sessionsMutex);
    for (int i = 0; i < MAX_SESSIONS; i++) {
      if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
        std::lock_guard<std::mutex> callbackLock(sessions[i].callbackMutex);
        sessions[i].callbackInfo.onFFmpegProgress =
            aki::JSBind::GetJSFunction(uuid + "_onFFmpegProgress");
        sessions[i].callbackInfo.hasLogCallback = false;
        sessions[i].callbackInfo.hasProgressCallback = false;
        sessions[i].callbackInfo.sessionId = sessionId;
        sessions[i].callbackInfo.onFFmpegLog = nullptr;
        sessions[i].callbackInfo.onFFmpegProgressMessage = nullptr;
        break;
      }
    }
  }

  // 设置全局日志回调（FFmpeg 的日志回调是全局的，但我们在回调中通过 sessionId 查找对应的 CallBackInfo）
  av_log_set_callback(log_call_back_with_callback);

  int ret = exe_ffmpeg_cmd_with_session(sessionId, cmdLen, argv1);

  setCurrentExecutingSessionId(0);

  // 如果返回码非零但错误缓冲区为空，尝试添加一些有用的错误信息
  if (ret != 0) {
    std::lock_guard<std::mutex> lock(sessionsMutex);
    for (int i = 0; i < MAX_SESSIONS; i++) {
      if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
        std::lock_guard<std::mutex> errorLock(sessions[i].errorMutex);
        if (sessions[i].errorBufferLen == 0 ||
            strlen(sessions[i].errorBuffer) == 0) {
          // 错误缓冲区为空，添加返回码的错误描述
          char errMsg[512] = {0};
          av_strerror(ret, errMsg, sizeof(errMsg));
          char defaultError[1024] = {0};
          snprintf(defaultError, sizeof(defaultError),
                   "FFmpeg execution failed with return code: %d\n"
                   "Error description: %s\n"
                   "Note: No detailed error log was captured.\n",
                   ret, errMsg);
          appendSessionErrorMessage(sessionId, defaultError);
        }
        break;
      }
    }
  }

  // 清理回调信息（线程安全）
  {
    std::lock_guard<std::mutex> lock(sessionsMutex);
    for (int i = 0; i < MAX_SESSIONS; i++) {
      if (sessions[i].sessionId == sessionId) {
        std::lock_guard<std::mutex> callbackLock(sessions[i].callbackMutex);
        sessions[i].callbackInfo.onFFmpegProgress = nullptr;
        sessions[i].callbackInfo.onFFmpegLog = nullptr;
        sessions[i].callbackInfo.onFFmpegProgressMessage = nullptr;
        sessions[i].callbackInfo.hasLogCallback = false;
        sessions[i].callbackInfo.hasProgressCallback = false;
        break;
      }
    }
  }

  // 释放内存：使用 delete[] 因为是用 new 分配的
  for (int i = 0; i < cmdLen; ++i) {
    delete[] argv1[i];
  }
  delete[] argv1;  // 释放指针数组本身
  return ret;
}

// 设置日志输出开关（线程安全）
void setLogEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(g_logEnabledMutex);
  g_logEnabled = enabled;
}

JSBIND_ADDON(ffmpegutils)

JSBIND_GLOBAL() {
  JSBIND_PFUNCTION(executeFFmpegCommandWithSession);
  JSBIND_FUNCTION(createFFmpegSession);
  JSBIND_FUNCTION(cancelFFmpegCommandBySession);
  JSBIND_FUNCTION(getSessionErrorMessage);
  JSBIND_FUNCTION(destroyFFmpegSession);
  JSBIND_FUNCTION(setLogEnabled);
}