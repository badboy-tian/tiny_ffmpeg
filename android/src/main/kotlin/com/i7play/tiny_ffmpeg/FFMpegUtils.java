package com.i7play.tiny_ffmpeg;

public class FFMpegUtils {
    static {
        System.loadLibrary("ffmpeg");
        System.loadLibrary("ffmpeg-invoke");
    }

    public static int executeFFmpegCommand(int argc, String[] argv, OnActionListener onActionListener) {
        return executeFFmpegCommand(argc, argv, onActionListener, -1);
    }

    public static native int showLog(boolean showLog);
    private static native int executeFFmpegCommand(int argc, String[] argv, OnActionListener onActionListener, long totalTime);
    public static native long getMediaDuration(String mediaPath);
    public static native int cancelExecuteFFmpegCommand();

    public interface OnActionListener {
        void progress(float progress);
        void fail(int code, String msg);
        void success();
    }
}
