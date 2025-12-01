import 'dart:io';

import 'package:audioplayers/audioplayers.dart';
import 'package:flutter/material.dart';
import 'dart:async';

import 'package:path_provider/path_provider.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg_cmd.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({Key? key}) : super(key: key);

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  StreamSubscription? subscription;
  TinyFfmpegSession? _currentSession;

  @override
  void initState() {
    super.initState();
    //initPlatformState();
  }

  // Platform messages are asynchronous, so we initialize in an async method.
  Future<void> initPlatformState() async {
    Directory? tempDir = await getApplicationDocumentsDirectory();
    var tempPath = "${tempDir.path}/11.mp3";
    var maskPath = "${tempDir.path}/mask.mp3";
    var bgPath = "${tempDir.path}/bg.mp3";
    var icPath = "${tempDir.path}/ic.png";

    var outPath = "${tempDir.path}/try_real.mp3";

    await checkCopy(tempPath, "11.mp3");
    await checkCopy(maskPath, "mask.mp3");
    await checkCopy(bgPath, "bg.mp3");
    await checkCopy(icPath, "ic.png");


    //打印ffmpeg支持的编码器, 解码器, 硬件加速信息
    printEncoderAndDecoderInfo();

    var args = TinyFFmpegCMD();
    args.add("-i");
    args.add(tempPath);
    args.add("-i");
    args.add(bgPath);
    args.add("-i");
    args.add(maskPath);
    args.add("-filter_complex");
    //args.add("[0]volume=3[a0];[1]aloop=loop=-1:size=2e+09,volume=1[a1];[a0][a1]amix=inputs=2:duration=first");
    args.add(
        "[0]adelay=2000|2000,volume=3[a1];[1]volume=0.8[abg];[2]aloop=loop=-1:size=2e+09[a3];[a1][abg][a3]amix=inputs=3:duration=first");
    //args.add("-c:a");
    //args.add("libmp3lame");
    args.add("-ac");
    args.add("1");
    args.add(outPath);

    // 使用新的 Session API
    try {
      debugPrint("executeAsync");
      _currentSession = await TinyFfmpeg.executeAsync(args);
      debugPrint("sessionId: ${_currentSession?.sessionId}");
      // 如果需要取消，可以调用：
      // await _currentSession?.cancel();

      // 等待执行完成并获取结果
      TinyFfmpegResult? result = await _currentSession?.getResult();
      debugPrint("result: $result");
      if (result != null) {
        debugPrint("Execution result: $result");

        if (!result.isSuccess) {
          // 获取详细错误信息
          String errorMsg = await _currentSession!.getErrorMessage();
          debugPrint("Detailed error: $errorMsg");
        } else {
          // 播放生成的音频文件
          AudioPlayer _player = AudioPlayer();
          _player.play(DeviceFileSource(outPath));
        }
      }
    } catch (e) {
      debugPrint("Error: $e");
    }
  }
  

  Future<void> printEncoderAndDecoderInfo() async {
    var args = TinyFFmpegCMD();
    args.add("-encoders");
    var result = await TinyFfmpeg.executeAsync(args);
    if (result != null) {
      debugPrint("result: $result");
    } else {
      debugPrint("result is null");
    }

    args = TinyFFmpegCMD();
    args.add("-decoders");
    result = await TinyFfmpeg.executeAsync(args);
    if (result != null) {
      debugPrint("result: $result");
    } else {
      debugPrint("result is null");
    }

    args = TinyFFmpegCMD();
    args.add("-hwaccels");
    result = await TinyFfmpeg.executeAsync(args);
    if (result != null) {
      debugPrint("result: $result");
    } else {
      debugPrint("result is null");
    }
  }

  Future<void> checkCopy(String path, String name) async {
    File file = File(path);
    if (!file.existsSync()) {
      var data = await DefaultAssetBundle.of(context).load("assets/$name");
      file.writeAsBytesSync(List.from(data.buffer.asUint8List()));
    }
  }

  @override
  void dispose() {
    subscription?.cancel();
    // 取消正在执行的 Session（如果有）
    _currentSession?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(
          title: const Text('Plugin example app'),
        ),
        body: Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              ElevatedButton(
                onPressed: () async {
                  initPlatformState();
                },
                child: const Text('Create Session'),
              ),
              if (_currentSession != null)
                ElevatedButton(
                  onPressed: () async {
                    await _currentSession?.cancel();
                    setState(() {
                      _currentSession = null;
                    });
                    debugPrint("Session cancelled");
                  },
                  child: const Text('Cancel FFmpeg'),
                ),
            ],
          ),
        ),
      ),
    );
  }
}
