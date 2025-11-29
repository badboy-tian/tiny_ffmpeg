#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdlib.h>
#include "ffmpeg.h"
#include "ffmpeg_cmd.h"

//保证同时只能一个线程执行
static pthread_mutex_t cmdLock;
static int cmdLockHasInit = 0;
bool hasRegistered = false;

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
static pthread_mutex_t sessionsLock = PTHREAD_MUTEX_INITIALIZER;
static int64_t nextSessionId = 1;
static int64_t currentExecutingSessionId = 0;

// 声明 exe_ffmpeg_cmd_with_session 函数（在 ffmpeg.c 中实现）
extern int exe_ffmpeg_cmd_with_session(int64_t sessionId, int argc, char **argv,
                                       int64_t handle,
                                       void (*progressCallBack)(int64_t, int, float),
                                       int64_t totalTime);

int64_t createFFmpegSession() {
    pthread_mutex_lock(&sessionsLock);
    int64_t sessionId = nextSessionId++;
    
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!sessions[i].inUse) {
            sessions[i].sessionId = sessionId;
            sessions[i].cancelled = 0;
            sessions[i].errorBuffer[0] = '\0';
            sessions[i].errorBufferLen = 0;
            sessions[i].inUse = 1;
            pthread_mutex_init(&sessions[i].errorMutex, NULL);
            pthread_mutex_unlock(&sessionsLock);
            return sessionId;
        }
    }
    pthread_mutex_unlock(&sessionsLock);
    return -1;
}

int cancelFFmpegCommandBySession(int64_t sessionId) {
    pthread_mutex_lock(&sessionsLock);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
            sessions[i].cancelled = 1;
            pthread_mutex_unlock(&sessionsLock);
            return 0;
        }
    }
    pthread_mutex_unlock(&sessionsLock);
    return -1;
}

int isSessionCancelled(int64_t sessionId) {
    pthread_mutex_lock(&sessionsLock);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
            int cancelled = sessions[i].cancelled;
            pthread_mutex_unlock(&sessionsLock);
            return cancelled;
        }
    }
    pthread_mutex_unlock(&sessionsLock);
    return 0;
}

void appendSessionErrorMessage(int64_t sessionId, const char* message) {
    if (message == NULL || strlen(message) == 0) return;
    
    pthread_mutex_lock(&sessionsLock);
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
    pthread_mutex_unlock(&sessionsLock);
}

const char* getSessionErrorMessage(int64_t sessionId) {
    static char empty[] = "";
    pthread_mutex_lock(&sessionsLock);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
            const char* msg = sessions[i].errorBuffer;
            pthread_mutex_unlock(&sessionsLock);
            return msg;
        }
    }
    pthread_mutex_unlock(&sessionsLock);
    return empty;
}

void destroyFFmpegSession(int64_t sessionId) {
    pthread_mutex_lock(&sessionsLock);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
            pthread_mutex_destroy(&sessions[i].errorMutex);
            sessions[i].inUse = 0;
            break;
        }
    }
    pthread_mutex_unlock(&sessionsLock);
}

int64_t getCurrentExecutingSessionId(void) {
    return currentExecutingSessionId;
}

// 供 ffmpeg.c 使用
int64_t get_ffmpeg_current_session_id(void) {
    return getCurrentExecutingSessionId();
}

void setCurrentExecutingSessionId(int64_t sessionId) {
    currentExecutingSessionId = sessionId;
}

int executeFFmpegCommandWithSession(int64_t sessionId, int argc, char **argv,
                                     int64_t handle,
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
        pthread_mutex_init(&cmdLock, NULL);
        cmdLockHasInit = 1;
    }
    
    pthread_mutex_lock(&cmdLock);

    setCurrentExecutingSessionId(sessionId);
    
    // 清空该session的错误缓冲区
    pthread_mutex_lock(&sessionsLock);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].inUse && sessions[i].sessionId == sessionId) {
            pthread_mutex_lock(&sessions[i].errorMutex);
            sessions[i].errorBuffer[0] = '\0';
            sessions[i].errorBufferLen = 0;
            pthread_mutex_unlock(&sessions[i].errorMutex);
            break;
        }
    }
    pthread_mutex_unlock(&sessionsLock);
    
    int ret = exe_ffmpeg_cmd_with_session(sessionId, argc, argv, handle, progressCallBack, totalTime);
    
    setCurrentExecutingSessionId(0);
    
    for (int i = 0; i < argc; ++i) {
        free(argv[i]);
    }
    pthread_mutex_unlock(&cmdLock);
    return ret;
}

int executeFFmpegCommand4TotalTime_New(int64_t handle, int argc, char **argv,
                                   void (*progressCallBack)(int64_t, int, float),
                                   int64_t totalTime) {
    return executeFFmpegCommandWithSession(0, argc, argv, handle, progressCallBack, totalTime);
}

int executeFFmpegCommand_New(int64_t handle, int argc, char **argv,
                         void (*progressCallBack)(int64_t, int, float)) {
    return executeFFmpegCommand4TotalTime_New(handle, argc, argv, progressCallBack, -1);
}

int cancelExecuteFFmpegCommand() {
    int64_t currentSessionId = getCurrentExecutingSessionId();
    if (currentSessionId != 0) {
        return cancelFFmpegCommandBySession(currentSessionId);
    }
    return cancel_exe_ffmpeg_cmd();
}

