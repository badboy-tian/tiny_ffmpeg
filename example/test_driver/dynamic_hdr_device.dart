import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg.dart';

// 独立设备入口验证动态元数据和默认无损导出，避免将自动测试逻辑加入示例界面。
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final status = ValueNotifier<String>('Testing dynamic HDR');
  runApp(MaterialApp(
      home: Scaffold(
          body: Center(
              child: ValueListenableBuilder<String>(
    valueListenable: status,
    builder: (_, value, __) => Text(value),
  )))));
  final directory = await getApplicationDocumentsDirectory();
  final report = File('${directory.path}/dynamic-hdr-results.json');
  final rows = <Map<String, dynamic>>[];
  var progressEvents = 0;
  final progress = TinyFfmpeg.listenProgress.listen((dynamic event) {
    if (event is Map && event['type'] == 'progress') progressEvents++;
  });
  Future<void> record(String name, Map<String, dynamic> values) async {
    rows.add({'name': name, ...values});
    await report.writeAsString(jsonEncode(rows), flush: true);
    status.value = name;
  }

  try {
    await TinyFfmpeg.showLog(false);
    // 8.4 使用真实手机素材，5 和 8.1 使用独立 RPU 的可重复测试素材。
    // HDR10+ 同时覆盖版本 0 和 1，防止序列化默认值悄悄改变源格式。
    for (final kind in ['dovi', 'dovi5', 'dovi81', 'hdr10plus', 'hdr10plus0']) {
      final input = '${directory.path}/$kind.mp4';
      final source = await TinyFfmpeg.getMediaInfo(input);
      final expectedProfile = kind == 'dovi5' ? 5 : 8;
      final expectedCompatibility = {'dovi': 4, 'dovi5': 0, 'dovi81': 1}[kind];
      if (source.bitDepth != 10 ||
          (kind.startsWith('dovi') &&
              (!source.dolbyVision ||
                  source.dolbyVisionProfile != expectedProfile ||
                  source.dolbyVisionCompatibilityId !=
                      expectedCompatibility)) ||
          (kind.startsWith('hdr10plus') && !source.hdr10Plus)) {
        throw StateError('Incorrect $kind source metadata');
      }
      await record('${kind}_probe', {
        'profile': source.dolbyVisionProfile,
        'compatibility': source.dolbyVisionCompatibilityId,
        'rotation': source.rotation,
        'width': source.displayWidth,
        'height': source.displayHeight,
      });
      for (final operation in ['trim', 'crop', 'lossy_crop']) {
        final name = '${kind}_$operation';
        status.value = name;
        final output = '${directory.path}/dynamic_$name.mp4';
        final width = source.displayWidth - (operation == 'trim' ? 0 : 32);
        final height = source.displayHeight - (operation == 'trim' ? 0 : 32);
        final command = await TinyFfmpeg.createExportCommand(
          input: input,
          output: output,
          start: const Duration(seconds: 1),
          duration: const Duration(milliseconds: 200),
          crop: operation == 'trim'
              ? null
              : TinyFfmpegCrop(width: width, height: height, x: 16, y: 16),
          lossless: operation != 'lossy_crop',
        );
        final started = DateTime.now();
        final session = await TinyFfmpeg.executeAsync(command);
        final result =
            await session.getResult().timeout(const Duration(minutes: 5));
        if (result == null || !result.isSuccess) {
          throw StateError('$name failed ${await session.getErrorMessage()}');
        }
        final exported = await TinyFfmpeg.getMediaInfo(output);
        if (exported.width != width ||
            exported.height != height ||
            exported.bitDepth != source.bitDepth ||
            exported.colorTransfer != source.colorTransfer ||
            exported.dolbyVision != source.dolbyVision ||
            exported.hdr10Plus != source.hdr10Plus ||
            exported.dolbyVisionProfile != source.dolbyVisionProfile ||
            exported.dolbyVisionCompatibilityId !=
                source.dolbyVisionCompatibilityId) {
          throw StateError('$name changed HDR type or dimensions');
        }
        await record(name, {
          'width': exported.width,
          'height': exported.height,
          'bitDepth': exported.bitDepth,
          'transfer': exported.colorTransfer,
          'dolbyVision': exported.dolbyVision,
          'hdr10Plus': exported.hdr10Plus,
          'lossless': operation != 'lossy_crop',
          'bytes': await File(output).length(),
          'milliseconds': DateTime.now().difference(started).inMilliseconds,
        });
      }
    }
    if (progressEvents == 0) {
      throw StateError('No progress with console logs disabled');
    }
    await record('PASS', {'progressEvents': progressEvents});
    status.value = 'Dynamic HDR tests passed';
  } catch (error, stack) {
    await record('FAIL', {'error': '$error', 'stack': '$stack'});
    status.value = '$error';
  } finally {
    await progress.cancel();
  }
}
