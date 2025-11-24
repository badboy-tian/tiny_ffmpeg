#include <aki/jsbind.h>
#include <cstdio>
#include <vector>

#include "hilog/log.h"

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x3200 // 全局domain宏，标识业务领域
#define LOG_TAG "xxoo"    // 全局tag宏，标识模块日志tag

extern "C" {
#include "ffmpeg.h"
#include "libavfilter/avfilter.h"
#include "libavformat/avformat.h"
#include "libavutil/log.h"
// int exe_ffmpeg_cmd(int argc, char **argv,
//                    int64_t handle, void (*progressCallBack)(int64_t, int,
//                    float), int64_t totalTime);
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
} CallBackInfo;

#include <fstream>
#include <sstream>
#include <string>

void log_call_back(void *ptr, int level, const char *fmt, va_list vl) {
  static int print_prefix = 1;
  static char prev[1024];
  char line[1024];
  av_log_format_line(ptr, level, fmt, vl, line, sizeof(line), &print_prefix);
  strcpy(prev, line);
  OH_LOG_ERROR(LOG_APP, "========> %{public}s", line);
}

void showLog(bool show) {
  if (show) {
    av_log_set_callback(log_call_back);
  }
}

int executeFFmpegCommandAPP(std::string uuid, int cmdLen,
                            std::vector<std::string> argv) {
  char **argv1 = vector_to_argv(argv);

  CallBackInfo onActionListener;
  onActionListener.onFFmpegProgress =
      aki::JSBind::GetJSFunction(uuid + "_onFFmpegProgress");

  int ret = exe_ffmpeg_cmd(cmdLen, argv1);

  for (int i = 0; i < cmdLen; ++i) {
    free(argv1[i]);
  }
  return ret;
}

JSBIND_ADDON(ffmpegutils)

JSBIND_GLOBAL() {
  JSBIND_PFUNCTION(executeFFmpegCommandAPP);
  JSBIND_FUNCTION(showLog);
  JSBIND_FUNCTION(cancel_ffmpeg_cmd, "cancelFFmpegCommand");
}