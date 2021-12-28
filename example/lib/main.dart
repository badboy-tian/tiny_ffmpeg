import 'dart:io';

import 'package:audioplayers/audioplayers.dart';
import 'package:flutter/material.dart';
import 'dart:async';

import 'package:flutter/services.dart';
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
  String _platformVersion = 'Unknown';
  StreamSubscription? subscription;

  @override
  void initState() {
    super.initState();
    initPlatformState();
  }

  // Platform messages are asynchronous, so we initialize in an async method.
  Future<void> initPlatformState() async {
    String platformVersion;
    try {
      platformVersion = await TinyFfmpeg.platformVersion ?? 'Unknown platform version';
    } on PlatformException {
      platformVersion = 'Failed to get platform version.';
    }

    if (!mounted) return;

    setState(() {
      _platformVersion = platformVersion;
    });

    subscription = TinyFfmpeg.listenProgress.listen((event) {
      print(event.toString());
    });

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

    var args = TinyFFmpegCMD();
    args.add("-i");
    args.add(tempPath);
    args.add("-i");
    args.add(bgPath);
    args.add("-i");
    args.add(maskPath);
    args.add("-filter_complex");
    //args.add("[0]volume=3[a0];[1]aloop=loop=-1:size=2e+09,volume=1[a1];[a0][a1]amix=inputs=2:duration=first");
    args.add("[0]adelay=2000|2000,volume=3[a1];[1]volume=0.8[abg];[2]aloop=loop=-1:size=2e+09[a3];[a1][abg][a3]amix=inputs=3:duration=first");
    args.add("-ac");
    args.add("1");
    args.add(outPath);

    TinyFfmpegResult result = await TinyFfmpeg.executeFFmpegCommand(args);
    print(result);

    AudioPlayer _player = AudioPlayer();
    _player.play(outPath, isLocal: true);
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
          child: Text('Running on: $_platformVersion\n'),
        ),
      ),
    );
  }
}
