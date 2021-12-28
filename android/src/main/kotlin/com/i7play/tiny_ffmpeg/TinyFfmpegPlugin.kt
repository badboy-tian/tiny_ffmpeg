package com.i7play.tiny_ffmpeg

import android.annotation.SuppressLint
import androidx.annotation.NonNull

import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.MethodChannel.MethodCallHandler
import io.flutter.plugin.common.MethodChannel.Result
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.EventChannel.EventSink
import io.reactivex.BackpressureStrategy
import io.reactivex.Flowable
import io.reactivex.FlowableEmitter
import io.reactivex.FlowableOnSubscribe
import io.reactivex.android.schedulers.AndroidSchedulers
import io.reactivex.disposables.Disposable
import io.reactivex.schedulers.Schedulers


/** TinyFfmpegPlugin */
class TinyFfmpegPlugin : FlutterPlugin, MethodCallHandler, EventChannel.StreamHandler {
    private lateinit var channel: MethodChannel
    private lateinit var eventChannel: EventChannel
    private var eventSink: EventSink? = null
    var dispose: Disposable ?= null

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

                dispose = Flowable.create(FlowableOnSubscribe<HashMap<String, Any>> { emitter ->
                    FFMpegUtils.executeFFmpegCommand(argc, argv.toTypedArray(), object : FFMpegUtils.OnActionListener {
                        override fun progress(progress: Float) {
                            val map = hashMapOf<String, Any>()
                            map["type"] = "progress"
                            map["code"] = 0
                            map["message"] = progress

                            emitter.onNext(map)
                        }

                        override fun fail(code: Int, msg: String?) {
                            val map = hashMapOf<String, Any>()
                            map["type"] = "result"
                            map["code"] = -1
                            map["message"] = msg.toString()
                            emitter.onNext(map)
                        }

                        override fun success() {
                            val map = hashMapOf<String, Any>()
                            map["type"] = "result"
                            map["code"] = 0
                            map["message"] = "success"
                            emitter.onNext(map)
                        }
                    })
                }, BackpressureStrategy.BUFFER).subscribeOn(Schedulers.io()).observeOn(AndroidSchedulers.mainThread()).subscribe ({ map->
                    val type = map["type"].toString()
                    if(type == "progress"){
                        eventSink?.success(map)
                    }else{
                        result.success(map)
                    }
                }, {error->
                    val map = hashMapOf<String, Any>()
                    map["type"] = "result"
                    map["code"] = -1
                    map["message"] = error.message.toString()
                    result.success(map)
                })
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
                dispose?.dispose()
                dispose = null
                FFMpegUtils.cancelExecuteFFmpegCommand();
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
        dispose?.dispose()
        dispose = null
    }

    override fun onListen(arguments: Any?, events: EventSink?) {
        eventSink = events
    }

    override fun onCancel(arguments: Any?) {
        eventSink = null
        dispose?.dispose()
        dispose = null
    }
}
