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
import java.util.concurrent.ConcurrentHashMap

/** TinyFfmpegPlugin */
data class SessionInfo(
    val sessionId: Long,
    var future: Future<*>? = null,
    var result: Result? = null,
    var isCancelled: Boolean = false
)

class TinyFfmpegPlugin : FlutterPlugin, MethodCallHandler, EventChannel.StreamHandler {
    private lateinit var channel: MethodChannel
    private lateinit var eventChannel: EventChannel
    private var eventSink: EventSink? = null
    private val executor: ExecutorService = Executors.newSingleThreadExecutor()
    private var future: Future<*>? = null
    private val mainHandler = Handler(Looper.getMainLooper())
    private val sessions = ConcurrentHashMap<Long, SessionInfo>()

    override fun onAttachedToEngine(@NonNull flutterPluginBinding: FlutterPlugin.FlutterPluginBinding) {
        channel = MethodChannel(flutterPluginBinding.binaryMessenger, "tiny_ffmpeg")
        channel.setMethodCallHandler(this)

        eventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "tiny_ffmpeg_progress_event")
        eventChannel.setStreamHandler(this)
    }

    override fun onMethodCall(@NonNull call: MethodCall, @NonNull result: Result) {
        when (call.method) {
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

                // 创建 Session
                val sessionId = FFMpegUtils.createFFmpegSession()
                if (sessionId < 0) {
                    result.error("SESSION_ERROR", "Failed to create session", null)
                    return
                }

                val sessionInfo = SessionInfo(sessionId)
                sessionInfo.result = result
                sessions[sessionId] = sessionInfo

                // 返回 sessionId 给 Dart 层
                result.success(mapOf("sessionId" to sessionId))

                sessionInfo.future = executor.submit {
                    FFMpegUtils.executeFFmpegCommandWithSession(
                        sessionId,
                        argc, 
                        argv.toTypedArray(), 
                        object : FFMpegUtils.OnActionListener {
                            override fun progress(progress: Float) {
                                // 不再发送 progress 事件
                            }

                            override fun fail(code: Int, msg: String?) {
                                val map = hashMapOf<String, Any>()
                                map["type"] = "result"
                                map["code"] = code
                                map["message"] = msg ?: "Unknown error"
                                map["sessionId"] = sessionId
                                
                                mainHandler.post {
                                    // 通过 EventChannel 发送结果事件
                                    eventSink?.success(map)
                                    sessionInfo.result?.success(map)
                                    sessions.remove(sessionId)
                                    FFMpegUtils.destroyFFmpegSession(sessionId)
                                }
                            }

                            override fun success() {
                                val map = hashMapOf<String, Any>()
                                map["type"] = "result"
                                map["code"] = 0
                                map["message"] = "success"
                                map["sessionId"] = sessionId
                                
                                mainHandler.post {
                                    // 通过 EventChannel 发送结果事件
                                    eventSink?.success(map)
                                    sessionInfo.result?.success(map)
                                    sessions.remove(sessionId)
                                    FFMpegUtils.destroyFFmpegSession(sessionId)
                                }
                            }
                        },
                        -1
                    )
                }
            }
            "cancelSession" -> {
                val sessionId = call.argument<Long>("sessionId") ?: 0L
                val sessionInfo = sessions[sessionId]
                if (sessionInfo != null) {
                    sessionInfo.isCancelled = true
                    sessionInfo.future?.cancel(true)
                    FFMpegUtils.cancelFFmpegCommandBySession(sessionId)
                    result.success(true)
                } else {
                    result.success(false)
                }
            }
            "getSessionState" -> {
                val sessionId = call.argument<Long>("sessionId") ?: 0L
                val sessionInfo = sessions[sessionId]
                val map = hashMapOf<String, Any>()
                if (sessionInfo != null) {
                    map["state"] = if (sessionInfo.isCancelled) "cancelled" else "running"
                } else {
                    map["state"] = "not_found"
                }
                result.success(map)
            }
            "getSessionErrorMessage" -> {
                val sessionId = call.argument<Long>("sessionId") ?: 0L
                val errorMsg = FFMpegUtils.getSessionErrorMessage(sessionId)
                result.success(errorMsg)
            }
            "getMediaDuration" -> {
                val mediaPath = call.arguments as String
                result.success(FFMpegUtils.getMediaDuration(mediaPath))
            }
            "showLog" -> {
                val show = call.argument<Boolean>() ?: true
                FFMpegUtils.setLogEnabled(show)
                result.success(null)
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
        
        // 清理所有 Session
        sessions.values.forEach { sessionInfo ->
            sessionInfo.future?.cancel(true)
            FFMpegUtils.destroyFFmpegSession(sessionInfo.sessionId)
        }
        sessions.clear()
        
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
