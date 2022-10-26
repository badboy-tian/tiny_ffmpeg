import 'dart:async';
import 'dart:collection';

import 'package:flutter/services.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg_cmd.dart';

class TinyFfmpeg {
  static const MethodChannel _channel = MethodChannel('tiny_ffmpeg');
  static const EventChannel _eventChannel = EventChannel("tiny_ffmpeg_progress_event");
  static Stream listenProgress = _eventChannel.receiveBroadcastStream();

  static Future<String?> get platformVersion async {
    final String? version = await _channel.invokeMethod('getPlatformVersion');
    return version;
  }

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

  static Future<bool> showLog(bool isShowLog) async {
    return await _channel.invokeMethod("showLog", isShowLog);
  }

  static Future<int> getMediaDuration(String path) async {
    return await _channel.invokeMethod("getMediaDuration", path);
  }

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

  static Future<bool> cancelExecuteFFmpegCommand() async {
    return await _channel.invokeMethod("cancelExecuteFFmpegCommand");
  }
}

class TinyFfmpegResult {
  String type;
  int code;
  String message;

  TinyFfmpegResult(this.type, this.code, this.message);

  @override
  String toString() {
    return 'TinyFfmpegResult{type: $type, code: $code, message: $message}';
  }
}
