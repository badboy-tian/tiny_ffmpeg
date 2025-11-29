//
// Created by luoye on 17/5/25.
//

#ifndef FFMPEGCMD_FFMPEG_CMD_H
#define FFMPEGCMD_FFMPEG_CMD_H
//#include <cstdint>
//小于0失败,>0成功
#ifdef __cplusplus

extern "C" {
#endif

//小于0失败,>0成功
int
executeFFmpegCommand(int64_t handle, const char * command, void (*progressCallBack)(int64_t, int, float));


int executeFFmpegCommand4TotalTime_New(int64_t handle, int argc, char **argv,
                                       void (*progressCallBack)(int64_t, int, float),
                                       int64_t totalTime);
int executeFFmpegCommand_New(int64_t handle, int argc, char **argv,
                         void (*progressCallBack)(int64_t, int, float));

int cancelExecuteFFmpegCommand();

// Session 管理接口
int64_t createFFmpegSession();
int cancelFFmpegCommandBySession(int64_t sessionId);
int isSessionCancelled(int64_t sessionId);
void destroyFFmpegSession(int64_t sessionId);
const char* getSessionErrorMessage(int64_t sessionId);
void appendSessionErrorMessage(int64_t sessionId, const char* message);
int executeFFmpegCommandWithSession(int64_t sessionId, int argc, char **argv,
                                     int64_t handle,
                                     void (*progressCallBack)(int64_t, int, float),
                                     int64_t totalTime);
int64_t getCurrentExecutingSessionId(void);
void setCurrentExecutingSessionId(int64_t sessionId);

#ifdef __cplusplus
}
#endif

#endif //FFMPEGCMD_FFMPEG_CMD_H
