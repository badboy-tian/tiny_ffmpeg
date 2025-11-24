import 'dart:async';
import 'dart:collection';

import 'package:flutter/services.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg_cmd.dart';

/// A Flutter plugin for executing FFmpeg commands.
class TinyFfmpeg {
  static const MethodChannel _channel = MethodChannel('tiny_ffmpeg');
  static const EventChannel _eventChannel = EventChannel("tiny_ffmpeg_progress_event");
  static Stream listenProgress = _eventChannel.receiveBroadcastStream();

  /// Gets the platform version.
  static Future<String?> get platformVersion async {
    final String? version = await _channel.invokeMethod('getPlatformVersion');
    return version;
  }

  /// Executes an FFmpeg command.
  ///
  /// [cmd] The command builder containing the arguments.
  /// Returns a [TinyFfmpegResult] containing the execution result.
  static Future<TinyFfmpegResult> executeFFmpegCommand(TinyFFmpegCMD cmd) async {
    var map = HashMap();
    map["argc"] = cmd.build().length;
    map["argv"] = cmd.build();

    Map<String, dynamic>? result = await _channel.invokeMapMethod("executeFFmpegCommand", map);
    var type = result?["type"].toString();
    var code = result?["code"] as int;
    var message = result?["message"].toString();

    var tinyResult = TinyFfmpegResult(type!, code, message!);
    return tinyResult;
  }

  /// Enables or disables FFmpeg logging.
  static Future<bool> showLog(bool isShowLog) async {
    return await _channel.invokeMethod("showLog", isShowLog);
  }

  /// Gets the duration of a media file.
  static Future<int> getMediaDuration(String path) async {
    return await _channel.invokeMethod("getMediaDuration", path);
  }

  /// Extracts a thumbnail image from a video.
  ///
  /// [path] The path to the video file.
  /// [outPath] The path where the thumbnail should be saved.
  static Future<TinyFfmpegResult> getThumbnailImage(String path, String outPath) {
    TinyFFmpegCMD cmd = TinyFFmpegCMD();
    cmd.add("-i");
    cmd.add(path);
    cmd.add("-ss");
    cmd.add("00:00:01.000");
    cmd.add("-vframes");
    cmd.add("1");
    cmd.add(outPath);

    return executeFFmpegCommand(cmd);
  }

  /// Cancels the currently running FFmpeg command.
  static Future<bool> cancelExecuteFFmpegCommand() async {
    return await _channel.invokeMethod("cancelExecuteFFmpegCommand");
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
