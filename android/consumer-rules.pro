# JNI 使用固定类名和方法名，防止 Release 混淆删除进度回调或破坏原生方法绑定。
-keep class com.i7play.tiny_ffmpeg.FFMpegUtils { *; }
