package com.i7play.tiny_ffmpeg

import android.os.Handler
import android.os.Looper
import androidx.annotation.NonNull

import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.MethodChannel.MethodCallHandler
import io.flutter.plugin.common.MethodChannel.Result
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.EventChannel.EventSink
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.Future

/** TinyFfmpegPlugin */
class TinyFfmpegPlugin : FlutterPlugin, MethodCallHandler, EventChannel.StreamHandler {
    private lateinit var channel: MethodChannel
    private lateinit var eventChannel: EventChannel
    private var eventSink: EventSink? = null
    private val executor: ExecutorService = Executors.newSingleThreadExecutor()
    private var future: Future<*>? = null
    private val mainHandler = Handler(Looper.getMainLooper())

    override fun onAttachedToEngine(@NonNull flutterPluginBinding: FlutterPlugin.FlutterPluginBinding) {
        channel = MethodChannel(flutterPluginBinding.binaryMessenger, "tiny_ffmpeg")
        channel.setMethodCallHandler(this)

        eventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "tiny_ffmpeg_progress_event")
        eventChannel.setStreamHandler(this)
    }

    override fun onMethodCall(@NonNull call: MethodCall, @NonNull result: Result) {
        when (call.method) {
            "getPlatformVersion" -> {
                result.success("Android ${android.os.Build.VERSION.RELEASE}")
            }
            "executeFFmpegCommand" -> {
                val argc = call.argument<Int>("argc")
                val argv = call.argument<List<String>>("argv")
                if (argc == null || argv == null) {
                    val map = hashMapOf<String, Any>()
                    map["type"] = "result"
                    map["code"] = -1
                    map["message"] = "参数错误"
                    result.success(map)
                    return
                }

                future = executor.submit {
                    FFMpegUtils.executeFFmpegCommand(argc, argv.toTypedArray(), object : FFMpegUtils.OnActionListener {
                        override fun progress(progress: Float) {
                            val map = hashMapOf<String, Any>()
                            map["type"] = "progress"
                            map["code"] = 0
                            map["message"] = progress

                            mainHandler.post {
                                eventSink?.success(map)
                            }
                        }

                        override fun fail(code: Int, msg: String?) {
                            val map = hashMapOf<String, Any>()
                            map["type"] = "result"
                            map["code"] = -1
                            map["message"] = msg.toString()
                            
                            mainHandler.post {
                                result.success(map)
                            }
                        }

                        override fun success() {
                            val map = hashMapOf<String, Any>()
                            map["type"] = "result"
                            map["code"] = 0
                            map["message"] = "success"
                            
                            mainHandler.post {
                                result.success(map)
                            }
                        }
                    })
                }
            }
            "showLog" -> {
                val isShowLog = call.arguments as Boolean
                FFMpegUtils.showLog(isShowLog)
                result.success(isShowLog)
            }
            "getMediaDuration" -> {
                val mediaPath = call.arguments as String
                result.success(FFMpegUtils.getMediaDuration(mediaPath))
            }
            "cancelExecuteFFmpegCommand" -> {
                future?.cancel(true)
                future = null
                FFMpegUtils.cancelExecuteFFmpegCommand()
                result.success(true)
            }
            else -> {
                result.notImplemented()
            }
        }
    }

    override fun onDetachedFromEngine(@NonNull binding: FlutterPlugin.FlutterPluginBinding) {
        channel.setMethodCallHandler(null)
        eventChannel.setStreamHandler(null)
        eventSink = null
        future?.cancel(true)
        future = null
        executor.shutdown()
    }

    override fun onListen(arguments: Any?, events: EventSink?) {
        eventSink = events
    }

    override fun onCancel(arguments: Any?) {
        eventSink = null
        future?.cancel(true)
        future = null
    }
}
