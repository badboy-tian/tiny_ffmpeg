package com.i7play.tiny_ffmpeg;

public class FFMpegUtils {
    static {
        System.loadLibrary("ffmpeg");
        System.loadLibrary("ffmpeg-invoke");
    }

    public static native long getMediaDuration(String mediaPath);

    // Session 管理接口
    public static native long createFFmpegSession();
    public static native int executeFFmpegCommandWithSession(long sessionId, int argc, String[] argv,
            OnActionListener onActionListener, long totalTime);
    public static native int cancelFFmpegCommandBySession(long sessionId);
    public static native String getSessionErrorMessage(long sessionId);
    public static native void destroyFFmpegSession(long sessionId);

    // 日志控制接口
    public static native void setLogEnabled(boolean enabled);

    public interface OnActionListener {
        void progress(float progress);

        void fail(int code, String msg);

        void success();
    }
}
