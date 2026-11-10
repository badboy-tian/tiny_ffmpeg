package com.i7play.tiny_ffmpeg;

public class FFMpegUtils {
    static {
        System.loadLibrary("ffmpeg");
        System.loadLibrary("ffmpeg-invoke");
    }

    public static native long getMediaDuration(String mediaPath);

    // 按 Session 路由进度，多个插件实例不会覆盖彼此的回调。
    public interface ProgressListener {
        void progress(long time, long size, double speed);
    }
    private static final java.util.concurrent.ConcurrentHashMap<Long, ProgressListener> progressListeners =
            new java.util.concurrent.ConcurrentHashMap<>();
    public static void setProgressListener(long id, ProgressListener listener) {
        progressListeners.put(id, listener);
    }
    public static void removeProgressListener(long id) {
        progressListeners.remove(id);
    }
    public static void onNativeProgress(long id, long time, long size, double speed) {
        ProgressListener listener = progressListeners.get(id);
        if (listener != null) listener.progress(time, size, speed);
    }
    public static native String getMediaInfo(String mediaPath);

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
