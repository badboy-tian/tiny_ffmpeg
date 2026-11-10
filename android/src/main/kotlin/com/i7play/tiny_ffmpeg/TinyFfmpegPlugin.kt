package com.i7play.tiny_ffmpeg

import android.os.Handler
import android.os.Looper
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.EventChannel.EventSink
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.MethodChannel.Result
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.Future

// started/finished 在同一把锁内转换，保证排队取消与线程启动不会重复释放原生槽位。
private class SessionInfo(val sessionId: Long) {
    var future: Future<*>? = null
    var started = false
    var finished = false
    var cancelled = false
}

class TinyFfmpegPlugin : FlutterPlugin, MethodChannel.MethodCallHandler, EventChannel.StreamHandler {
    private lateinit var channel: MethodChannel
    private lateinit var eventChannel: EventChannel
    private var executor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val sessions = ConcurrentHashMap<Long, SessionInfo>()
    private val pendingResults = linkedMapOf<Long, Map<String, Any>>()
    private var eventSink: EventSink? = null
    @Volatile private var attached = false

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        if (executor.isShutdown) executor = Executors.newSingleThreadExecutor()
        attached = true
        channel = MethodChannel(binding.binaryMessenger, "tiny_ffmpeg")
        channel.setMethodCallHandler(this)
        eventChannel = EventChannel(binding.binaryMessenger, "tiny_ffmpeg_progress_event")
        eventChannel.setStreamHandler(this)
    }

    override fun onMethodCall(call: MethodCall, result: Result) {
        when (call.method) {
            "executeFFmpegCommand" -> {
                val argv = call.argument<List<String>>("argv")
                val argc = call.argument<Int>("argc")
                if (argv.isNullOrEmpty() || argc != argv.size) {
                    result.error("ARGUMENT_ERROR", "argc must match a nonempty argv", null)
                    return
                }
                val id = FFMpegUtils.createFFmpegSession()
                if (id < 0) {
                    result.error("SESSION_ERROR", "Too many sessions", null)
                    return
                }
                val session = SessionInfo(id)
                sessions[id] = session
                FFMpegUtils.setProgressListener(id) { time, size, speed ->
                    mainHandler.post {
                        if (attached && sessions.containsKey(id)) eventSink?.success(mapOf(
                            "type" to "progress", "sessionId" to id,
                            "time" to time, "size" to size, "speed" to speed
                        ))
                    }
                }
                result.success(mapOf("sessionId" to id))
                session.future = executor.submit {
                    synchronized(session) {
                        if (session.finished) return@submit
                        session.started = true
                    }
                    var code = -1
                    var message = "FFmpeg execution failed"
                    var errorLog = ""
                    try {
                        code = FFMpegUtils.executeFFmpegCommandWithSession(
                            id, argv.size, argv.toTypedArray(), null, -1
                        )
                        errorLog = FFMpegUtils.getSessionErrorMessage(id).orEmpty()
                        message = if (code == 0) "success" else errorLog.ifEmpty { "FFmpeg failed with code $code" }
                    } catch (error: Exception) {
                        message = error.message ?: message
                    } finally {
                        finishSession(session, code, message, errorLog)
                    }
                }
            }
            "cancelSession" -> {
                val id = call.argument<Number>("sessionId")?.toLong() ?: 0
                val session = sessions[id]
                if (session != null) cancelSession(session)
                result.success(session != null)
            }
            "getSessionState" -> {
                val session = sessions[call.argument<Number>("sessionId")?.toLong()]
                val state = if (session == null) "not_found" else synchronized(session) {
                    if (session.cancelled) "cancelled" else "running"
                }
                result.success(mapOf("state" to state))
            }
            "getSessionErrorMessage" -> result.success(FFMpegUtils.getSessionErrorMessage(
                call.argument<Number>("sessionId")?.toLong() ?: 0
            ))
            "getMediaDuration", "getMediaInfo" -> {
                val path = call.arguments as? String
                if (path.isNullOrEmpty()) {
                    result.error("ARGUMENT_ERROR", "A media path is required", null)
                    return
                }
                // 媒体探测包含文件读取与流分析，放入工作队列避免阻塞 Flutter 主线程。
                executor.submit {
                    val value: Any = if (call.method == "getMediaInfo") {
                        FFMpegUtils.getMediaInfo(path)
                    } else FFMpegUtils.getMediaDuration(path)
                    mainHandler.post { if (attached) result.success(value) }
                }
            }
            "showLog" -> {
                FFMpegUtils.setLogEnabled(call.argument<Boolean>("show") ?: true)
                result.success(null)
            }
            else -> result.notImplemented()
        }
    }

    private fun cancelSession(session: SessionInfo) {
        synchronized(session) {
            if (session.finished) return
            session.cancelled = true
            if (session.started) {
                // 原生编码协作取消，等退出后再释放，不能靠 Java 中断提前销毁 Session。
                FFMpegUtils.cancelFFmpegCommandBySession(session.sessionId)
            } else {
                session.future?.cancel(false)
                finishSession(session, -1, "Command was cancelled", "")
            }
        }
    }

    private fun finishSession(session: SessionInfo, code: Int, message: String, errorLog: String) {
        val cancelled: Boolean
        synchronized(session) {
            if (session.finished) return
            session.finished = true
            cancelled = session.cancelled
            sessions.remove(session.sessionId)
            FFMpegUtils.removeProgressListener(session.sessionId)
            FFMpegUtils.destroyFFmpegSession(session.sessionId)
        }
        val event = mapOf<String, Any>(
            "type" to "result", "sessionId" to session.sessionId,
            "code" to if (cancelled) -1 else code,
            "message" to if (cancelled) "Command was cancelled" else message,
            "errorLog" to errorLog
        )
        mainHandler.post {
            if (!attached) return@post
            val sink = eventSink
            if (sink != null) sink.success(event)
            else if (pendingResults.size < 64) pendingResults[session.sessionId] = event
        }
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        attached = false
        channel.setMethodCallHandler(null)
        eventChannel.setStreamHandler(null)
        eventSink = null
        pendingResults.clear()
        sessions.values.toList().forEach { cancelSession(it) }
        executor.shutdown()
    }

    override fun onListen(arguments: Any?, events: EventSink?) {
        eventSink = events
        if (events != null) {
            pendingResults.values.forEach { events.success(it) }
            pendingResults.clear()
        }
    }

    override fun onCancel(arguments: Any?) {
        eventSink = null
    }
}
