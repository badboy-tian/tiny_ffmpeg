import 'dart:async';
import 'dart:collection';

import 'package:flutter/services.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg_cmd.dart';

/// A Flutter plugin for executing FFmpeg commands.
class TinyFfmpeg {
  static const MethodChannel _channel = MethodChannel('tiny_ffmpeg');
  static const EventChannel _eventChannel =
  EventChannel("tiny_ffmpeg_progress_event");
  static Stream listenProgress = _eventChannel.receiveBroadcastStream();

  // 全局事件监听器，确保 EventChannel 始终在监听
  static StreamSubscription? _globalEventSubscription;
  static final Map<String, Function(Map<dynamic, dynamic>)> _sessionHandlers =
  {};
  // 用于存储尚未注册 sessionId 的事件（防止竞态条件）
  static final Map<String, List<Map<dynamic, dynamic>>> _pendingEvents =
  <String, List<Map<dynamic, dynamic>>>{};

  // 初始化全局事件监听（确保 EventChannel 已经建立连接）
  static void _ensureEventChannelListening() {
    if (_globalEventSubscription == null) {
      _globalEventSubscription =
          _eventChannel.receiveBroadcastStream().listen((event) {
            if (event is Map) {
              final eventSessionId = event["sessionId"]?.toString();
              if (eventSessionId != null && eventSessionId.isNotEmpty) {
                // 优先查找已注册的处理器
                if (_sessionHandlers.containsKey(eventSessionId)) {
                  _sessionHandlers[eventSessionId]!(event);
                } else {
                  // 如果处理器尚未注册，将事件暂存
                  _pendingEvents.putIfAbsent(eventSessionId, () => []).add(event);
                }
              }
            }
          });
    }
  }

  /// Gets the duration of a media file.
  static Future<int> getMediaDuration(String path) async {
    return await _channel.invokeMethod("getMediaDuration", path);
  }

  /// Controls whether to show log messages from the native platform.
  ///
  /// [show] If true, enables log output from the native FFmpeg library.
  ///        If false, disables log output (useful for release builds).
  static Future<void> showLog(bool show) async {
    await _channel.invokeMethod("showLog", {"show": show});
  }

  /// Extracts a thumbnail image from a video.
  ///
  /// [path] The path to the video file.
  /// [outPath] The path where the thumbnail should be saved.
  static Future<TinyFfmpegResult> getThumbnailImage(
      String path, String outPath) async {
    TinyFFmpegCMD cmd = TinyFFmpegCMD();
    cmd.add("-i");
    cmd.add(path);
    cmd.add("-ss");
    cmd.add("00:00:01.000");
    cmd.add("-vframes");
    cmd.add("1");
    cmd.add(outPath);

    final session = await executeAsync(cmd);
    final result = await session.getResult();
    return result ?? TinyFfmpegResult("error", -1, "Unknown error");
  }

  /// Executes an FFmpeg command asynchronously and returns a Session.
  ///
  /// [cmd] The command builder containing the arguments.
  /// Returns a [TinyFfmpegSession] that can be used to cancel or get the result.
  static Future<TinyFfmpegSession> executeAsync(
      TinyFFmpegCMD cmd,
      ) async {
    // 确保 EventChannel 已经在监听（必须在调用 invokeMapMethod 之前）
    _ensureEventChannelListening();

    var map = HashMap();
    map["argc"] = cmd.build().length;
    map["argv"] = cmd.build();

    // 先调用 native 端获取 sessionId
    Map<String, dynamic>? result =
    await _channel.invokeMapMethod("executeFFmpegCommand", map);

    final sessionId = result?["sessionId"];
    if (sessionId == null) {
      throw Exception("Failed to create session");
    }

    final sessionIdString = sessionId.toString();

    // 使用真实的 sessionId 创建 Session
    final session = TinyFfmpegSession(sessionIdString);

    // 定义事件处理器
    void eventHandler(Map<dynamic, dynamic> event) {
      final eventSessionId = event["sessionId"]?.toString();
      // 只处理匹配当前 sessionId 的事件
      if (eventSessionId != null && eventSessionId != sessionIdString) {
        return; // 忽略其他 session 的事件
      }

      // 如果 session 已经完成，忽略后续事件
      if (session.state != SessionState.running) {
        return;
      }

      final type = event["type"]?.toString();
      if (type == "result") {
        final code = event["code"] as int? ?? -1;
        final message = event["message"]?.toString() ?? "";
        final errorLog = event["errorLog"]?.toString();  // 获取错误日志
        session._setResult(code, message, errorLog: errorLog);
      }
    }

    // 注册事件处理器
    _sessionHandlers[sessionIdString] = eventHandler;

    // 处理可能在注册之前到达的待处理事件
    final pendingEvents = _pendingEvents.remove(sessionIdString);
    if (pendingEvents != null) {
      for (final event in pendingEvents) {
        eventHandler(event);
      }
    }

    // 设置取消时的清理
    session._onCancel = () {
      _sessionHandlers.remove(sessionIdString);
      _pendingEvents.remove(sessionIdString); // 清理待处理事件
    };

    return session;
  }
}

enum SessionState { idle, running, cancelled, completed, failed }

class TinyFfmpegSession {
  final String _sessionId;
  SessionState _state = SessionState.running;
  int? _returnCode;
  String? _failStackTrace;
  String? _errorLog;  // 存储从原生层返回的完整错误日志
  StreamSubscription? _subscription;
  final Completer<TinyFfmpegResult?> _resultCompleter =
  Completer<TinyFfmpegResult?>();
  VoidCallback? _onCancel;

  TinyFfmpegSession(this._sessionId);

  String get sessionId => _sessionId;

  SessionState get state => _state;
  int? get returnCode => _returnCode;
  String? get failStackTrace => _failStackTrace;

  /// Cancels the FFmpeg command execution.
  Future<void> cancel() async {
    if (_state == SessionState.completed || _state == SessionState.failed) {
      return;
    }
    _state = SessionState.cancelled;
    await _subscription?.cancel();
    _onCancel?.call(); // 清理事件处理器
    await TinyFfmpeg._channel
        .invokeMethod("cancelSession", {"sessionId": int.parse(sessionId)});
  }

  /// Gets the execution result. Returns null if the command is still running or was cancelled.
  Future<TinyFfmpegResult?> getResult() async {
    if (_state == SessionState.idle || _state == SessionState.running) {
      return await _resultCompleter.future;
    }

    if (_state == SessionState.cancelled) {
      return TinyFfmpegResult("cancelled", -1, "Command was cancelled");
    }

    if (_returnCode != null) {
      return TinyFfmpegResult(
        _state == SessionState.completed ? "success" : "error",
        _returnCode!,
        _failStackTrace ?? "",
      );
    }

    return null;
  }

  /// Gets the detailed error message for this session.
  /// 优先返回从 result 事件中获取的 errorLog（因为 session 可能已被销毁）
  Future<String> getErrorMessage() async {
    // 如果已经有缓存的 errorLog，直接返回（避免 session 已销毁的问题）
    if (_errorLog != null && _errorLog!.isNotEmpty) {
      return _errorLog!;
    }
    // 否则尝试从原生层获取（可能返回空，如果 session 已销毁）
    final errorMsg = await TinyFfmpeg._channel.invokeMethod<String>(
      "getSessionErrorMessage",
      {"sessionId": int.parse(sessionId)},
    );
    return errorMsg ?? "";
  }

  void _setResult(int code, String message, {String? errorLog}) {
    _returnCode = code;
    _failStackTrace = message;
    _errorLog = errorLog;  // 保存错误日志
    if (code == 0) {
      _state = SessionState.completed;
    } else {
      _state = SessionState.failed;
    }
    if (!_resultCompleter.isCompleted) {
      _resultCompleter.complete(TinyFfmpegResult(
        code == 0 ? "success" : "error",
        code,
        message,
      ));
    }
    _subscription?.cancel();
    _onCancel?.call(); // 清理事件处理器
  }
}

class TinyFfmpegResult {
  String type;
  int code;
  String message;

  TinyFfmpegResult(this.type, this.code, this.message);

  /// Returns true if the command executed successfully (exit code 0).
  bool get isSuccess => code == 0;

  @override
  String toString() {
    return 'TinyFfmpegResult{type: $type, code: $code, message: $message}';
  }
}
