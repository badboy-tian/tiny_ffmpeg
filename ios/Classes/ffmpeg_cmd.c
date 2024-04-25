#include <string.h>
#include <stdbool.h>
#include "include/ffmpeg.h"

//保证同时只能一个线程执行
static pthread_mutex_t cmdLock;
static int cmdLockHasInit = 0;
bool hasRegistered = false;


int executeFFmpegCommand4TotalTime_New(int64_t handle, int argc, char **argv,
                                   void (*progressCallBack)(int64_t, int, float),
                                   int64_t totalTime) {
    if (!hasRegistered) {
        av_register_all();
        avcodec_register_all();
        avfilter_register_all();
        avformat_network_init();
        hasRegistered = true;
    }
    if (!cmdLockHasInit) {
        pthread_mutex_init(&cmdLock, NULL);//初始化
        cmdLockHasInit = 1;
    }
    pthread_mutex_lock(&cmdLock);


    int ret = exe_ffmpeg_cmd(argc, argv, handle, progressCallBack, totalTime);
    for (int i = 0; i < argc; ++i) {
        free(argv[i]);
    }
    pthread_mutex_unlock(&cmdLock);
    return ret;
}

int executeFFmpegCommand_New(int64_t handle, int argc, char **argv,
                         void (*progressCallBack)(int64_t, int, float)) {
    return executeFFmpegCommand4TotalTime_New(handle, argc, argv, progressCallBack, -1);
}

int cancelExecuteFFmpegCommand() {
    return cancel_exe_ffmpeg_cmd();
}

