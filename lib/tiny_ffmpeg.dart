import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg_cmd.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg_media_info.dart';
export 'package:tiny_ffmpeg/tiny_ffmpeg_media_info.dart';

/// A Flutter plugin for executing FFmpeg commands.
class TinyFfmpeg {
  static const MethodChannel _channel = MethodChannel('tiny_ffmpeg');
  static const EventChannel _eventChannel =
      EventChannel("tiny_ffmpeg_progress_event");
  // 全部消费者共用一次原生订阅，避免第二次 listen 替换正在接收结果的通道。
  static final StreamController<dynamic> _events =
      StreamController<dynamic>.broadcast(
          onListen: _ensureEventChannelListening);
  static Stream<dynamic> get listenProgress => _events.stream;
  static StreamSubscription<dynamic>? _globalEventSubscription;
  static final Map<String, TinyFfmpegSession> _sessions = {};
  static final Map<String, Map<dynamic, dynamic>> _pendingResults = {};
  static int _creatingSessions = 0;

  static void _ensureEventChannelListening() {
    _globalEventSubscription ??= _eventChannel.receiveBroadcastStream().listen(
          (dynamic event) {
            _events.add(event);
            if (event is! Map || event['type'] != 'result') return;
            final id = event['sessionId']?.toString();
            if (id == null) return;
            final session = _sessions[id];
            if (session != null) {
              session._setResult(
                (event['code'] as num?)?.toInt() ?? -1,
                event['message']?.toString() ?? '',
                errorLog: event['errorLog']?.toString(),
              );
            } else if (_creatingSessions > 0 && _pendingResults.length < 64) {
              // 只暂存创建窗口内的结果，取消任务的迟到事件不能无限堆积。
              _pendingResults[id] = event;
            }
          },
          onError: (Object error) => _failRunningSessions(error.toString()),
          onDone: () {
            _globalEventSubscription = null;
            _failRunningSessions('Native event channel closed');
          },
        );
  }

  static void _failRunningSessions(String message) {
    for (final session in _sessions.values.toList()) {
      session._setResult(-1, message);
    }
    _pendingResults.clear();
  }

  /// Reads source properties on a native worker thread.
  static Future<TinyFfmpegMediaInfo> getMediaInfo(String path) async {
    final json = await _channel.invokeMethod<String>('getMediaInfo', path);
    if (json == null) throw StateError('Native media probe returned no result');
    final info = jsonDecode(json) as Map<String, dynamic>;
    if (info.containsKey('error')) {
      throw PlatformException(
          code: 'MEDIA_ERROR',
          message: 'Media probe failed (${info['error']})');
    }
    return TinyFfmpegMediaInfo.fromMap(info);
  }

  /// Creates a source-preserving MP4 command for executeAsync.
  static Future<TinyFFmpegCMD> createExportCommand({
    required String input,
    required String output,
    Duration start = Duration.zero,
    Duration? duration,
    TinyFfmpegCrop? crop,
    bool lossless = true,
    int crf = 18,
    String preset = 'ultrafast',
    int threads = 2,
  }) async {
    // 即使传入相对路径或符号链接，也不能覆盖正在读取的源视频。
    final inputPath = await File(input).resolveSymbolicLinks();
    final outputFile = File(output);
    final outputPath = await outputFile.exists()
        ? await outputFile.resolveSymbolicLinks()
        : outputFile.absolute.path;
    // 硬链接也可能指向同一源文件，路径字符串比较不足以保证输入安全。
    if (inputPath == outputPath ||
        (await outputFile.exists() &&
            await FileSystemEntity.identical(inputPath, outputPath))) {
      throw ArgumentError('Output must differ from input');
    }
    final info = await getMediaInfo(inputPath);
    return info.exportCommand(
      input: inputPath,
      output: outputPath,
      start: start,
      duration: duration,
      crop: crop,
      lossless: lossless,
      crf: crf,
      preset: preset,
      threads: threads,
    );
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
    _ensureEventChannelListening();
    final arguments = List<String>.from(cmd.build());
    _creatingSessions++;
    try {
      final result = await _channel.invokeMapMethod<String, dynamic>(
        'executeFFmpegCommand',
        {'argc': arguments.length, 'argv': arguments},
      );
      final id = result?['sessionId']?.toString();
      if (id == null) throw StateError('Failed to create session');
      final session = TinyFfmpegSession(id);
      // 先安装清理函数再消费早到结果，避免极短任务留下已完成 Session。
      session._onCancel = () {
        _sessions.remove(id);
        _pendingResults.remove(id);
      };
      _sessions[id] = session;
      final pending = _pendingResults.remove(id);
      if (pending != null) {
        session._setResult(
          (pending['code'] as num?)?.toInt() ?? -1,
          pending['message']?.toString() ?? '',
          errorLog: pending['errorLog']?.toString(),
        );
      }
      return session;
    } finally {
      _creatingSessions--;
      if (_creatingSessions == 0) _pendingResults.clear();
    }
  }
}

enum SessionState { idle, running, cancelled, completed, failed }

class TinyFfmpegSession {
  final String _sessionId;
  SessionState _state = SessionState.running;
  int? _returnCode;
  String? _failStackTrace;
  String? _errorLog; // 存储从原生层返回的完整错误日志
  Future<void>? _cancelFuture;
  Future<void>? _nativeStopFuture;
  final Completer<TinyFfmpegResult?> _resultCompleter =
      Completer<TinyFfmpegResult?>();
  VoidCallback? _onCancel;

  TinyFfmpegSession(this._sessionId);

  String get sessionId => _sessionId;

  SessionState get state => _state;
  int? get returnCode => _returnCode;
  String? get failStackTrace => _failStackTrace;

  /// Cancels execution and resolves every existing result waiter.
  Future<void> cancel() {
    if (_cancelFuture != null) return _cancelFuture!;
    if (_state == SessionState.completed || _state == SessionState.failed) {
      return Future<void>.value();
    }
    // 先完成 Dart 等待者，原生释放仍由执行线程退出后的终态路径负责。
    _state = SessionState.cancelled;
    _returnCode = -1;
    _failStackTrace = 'Command was cancelled';
    _resultCompleter.complete(
      TinyFfmpegResult('cancelled', -1, _failStackTrace!),
    );
    _onCancel?.call();
    _cancelFuture = TinyFfmpeg._channel.invokeMethod<void>(
      'cancelSession',
      {'sessionId': int.parse(sessionId)},
    );
    return _cancelFuture!;
  }

  /// 取消信号与真实停止分开确认，事件通道故障时仍可安全等待原生槽位释放。
  Future<void> cancelAndWait({Duration timeout = const Duration(seconds: 10)}) {
    return _nativeStopFuture ??= _cancelAndWait(timeout).timeout(timeout);
  }

  Future<void> _cancelAndWait(Duration timeout) async {
    if (_state == SessionState.completed) return;
    // 通道错误会提前把 Dart session 标记 failed，但不能据此认为原生已停止。
    final failedBeforeCancel = _state == SessionState.failed;
    await cancel();
    if (failedBeforeCancel) {
      await TinyFfmpeg._channel.invokeMethod<void>('cancelSession', {
        'sessionId': int.parse(sessionId),
      });
    }
    final watch = Stopwatch()..start();
    while (watch.elapsed < timeout) {
      final status = await TinyFfmpeg._channel.invokeMapMethod<String, dynamic>(
        'getSessionState',
        {'sessionId': int.parse(sessionId)},
      );
      // cancelled 仅说明收到了信号；三端在执行线程结束后才移除 Session。
      if (status?['state'] == 'not_found') return;
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }
    throw TimeoutException('Native session stop was not confirmed', timeout);
  }

  /// Returns the same terminal result to current and subsequent waiters.
  Future<TinyFfmpegResult?> getResult() => _resultCompleter.future;

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
    // 取消与原生完成可能交错，终态只能完成一次。
    if (_resultCompleter.isCompleted) return;
    _returnCode = code;
    _failStackTrace = message;
    _errorLog = errorLog; // 保存错误日志
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
