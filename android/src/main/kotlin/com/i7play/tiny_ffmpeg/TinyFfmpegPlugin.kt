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
import java.util.concurrent.atomic.AtomicBoolean

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
    
    // 添加执行锁，防止并发执行导致的崩溃
    private val isExecuting = AtomicBoolean(false)

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

                // 检查是否有任务正在执行，如果有则返回忙碌状态
                if (isExecuting.get()) {
                    val map = hashMapOf<String, Any>()
                    map["type"] = "result"
                    map["code"] = -2
                    map["message"] = "FFmpeg 正在执行中，请等待完成后再试"
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
                    // 设置执行标志
                    isExecuting.set(true)
                    
                    try {
                        // 在执行前添加小延迟，确保之前的资源完全释放
                        Thread.sleep(50)
                        
                        // 新版 FFmpeg 8.0 直接返回结果码，不再使用回调
                        val ret = FFMpegUtils.executeFFmpegCommandWithSession(
                            sessionId,
                            argc, 
                            argv.toTypedArray(), 
                            null,  // 不再使用回调
                            -1
                        )
                        
                        val map = hashMapOf<String, Any>()
                        map["type"] = "result"
                        map["sessionId"] = sessionId
                        
                        if (ret == 0) {
                            map["code"] = 0
                            map["message"] = "success"
                        } else {
                            map["code"] = ret
                            // 获取错误信息
                            val errorMsg = FFMpegUtils.getSessionErrorMessage(sessionId)
                            map["message"] = if (errorMsg.isNullOrEmpty()) "FFmpeg execution failed with code: $ret" else errorMsg
                        }
                        
                        mainHandler.post {
                            // 通过 EventChannel 发送结果事件
                            eventSink?.success(map)
                            sessions.remove(sessionId)
                            FFMpegUtils.destroyFFmpegSession(sessionId)
                        }
                    } catch (e: Exception) {
                        val map = hashMapOf<String, Any>()
                        map["type"] = "result"
                        map["code"] = -1
                        map["message"] = e.message ?: "Unknown error"
                        map["sessionId"] = sessionId
                        
                        mainHandler.post {
                            eventSink?.success(map)
                            sessions.remove(sessionId)
                            FFMpegUtils.destroyFFmpegSession(sessionId)
                        }
                    } finally {
                        // 执行完成后添加延迟，确保 FFmpeg 内部状态完全清理
                        Thread.sleep(100)
                        // 清除执行标志
                        isExecuting.set(false)
                    }
                }
            }
            "cancelSession" -> {
                val sessionId = (call.argument<Number>("sessionId") ?: 0).toLong()
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
                val sessionId = (call.argument<Number>("sessionId") ?: 0).toLong()
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
                val sessionId = (call.argument<Number>("sessionId") ?: 0).toLong()
                val errorMsg = FFMpegUtils.getSessionErrorMessage(sessionId)
                result.success(errorMsg)
            }
            "getMediaDuration" -> {
                val mediaPath = call.arguments as String
                result.success(FFMpegUtils.getMediaDuration(mediaPath))
            }
            "showLog" -> {
                val show = call.argument<Boolean>("show") ?: true
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
