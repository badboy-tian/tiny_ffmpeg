import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg_cmd.dart';

// 独立设备入口覆盖取消、队列、失败恢复和自动 HDR 参数，避免污染示例界面。
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final status = ValueNotifier<String>('Running regression tests');
  runApp(MaterialApp(
      home: Scaffold(
          body: Center(
              child: ValueListenableBuilder<String>(
    valueListenable: status,
    builder: (_, value, __) => Text(value),
  )))));
  final directory = await getApplicationDocumentsDirectory();
  final report = File('${directory.path}/commercial-results.json');
  final rows = <Map<String, dynamic>>[];
  var progressEvents = 0;
  final progress = TinyFfmpeg.listenProgress.listen((dynamic event) {
    if (event is Map &&
        event['type'] == 'progress' &&
        event['sessionId'] != null) {
      progressEvents++;
    }
  });
  Future<void> record(String name, Map<String, dynamic> values) async {
    rows.add({'name': name, ...values});
    await report.writeAsString(jsonEncode(rows), flush: true);
    status.value = name;
  }

  TinyFFmpegCMD command(List<String> args) {
    final cmd = TinyFFmpegCMD();
    args.forEach(cmd.add);
    return cmd;
  }

  Future<TinyFfmpegResult> run(TinyFFmpegCMD cmd) async {
    final session = await TinyFfmpeg.executeAsync(cmd);
    final result =
        await session.getResult().timeout(const Duration(minutes: 5));
    if (result == null || !result.isSuccess) {
      throw StateError(
          'Command failed $result ${await session.getErrorMessage()}');
    }
    return result;
  }

  try {
    await TinyFfmpeg.showLog(false);
    final vivid = '${directory.path}/vivid.mp4';
    final info = await TinyFfmpeg.getMediaInfo(vivid);
    final duration = await TinyFfmpeg.getMediaDuration(vivid);
    if (!info.hdrVivid ||
        info.bitDepth != 10 ||
        duration <= 0 ||
        (duration - info.durationMilliseconds).abs() > 100) {
      throw StateError('Incorrect Vivid probe or duration $duration');
    }
    await record('probe', {
      'durationMilliseconds': duration,
      'hdrVivid': info.hdrVivid,
      'bitDepth': info.bitDepth
    });
    // 四轮累计超过原生槽位上限，确认排队取消不会逐渐耗尽 Session。
    for (var batch = 0; batch < 4; batch++) {
      final blocker = await TinyFfmpeg.executeAsync(command([
        '-re',
        '-stream_loop',
        '-1',
        '-i',
        vivid,
        '-t',
        '60',
        '-map',
        '0:v:0',
        '-an',
        '-vf',
        'scale=320:-2',
        '-c:v',
        'libx265',
        '-preset',
        'ultrafast',
        '-x265-params',
        'pools=1:frame-threads=1',
        '${directory.path}/cancel-$batch.mp4',
      ]));
      final waiting = blocker.getResult();
      await Future<void>.delayed(const Duration(milliseconds: 300));
      for (var count = 0; count < 20; count++) {
        final queued = await TinyFfmpeg.executeAsync(command(['-version']));
        final waiter = queued.getResult();
        await queued.cancel();
        await queued.cancel();
        if ((await waiter.timeout(const Duration(seconds: 5)))?.type !=
            'cancelled') {
          throw StateError('Queued cancellation failed');
        }
      }
      await blocker.cancel();
      if ((await waiting.timeout(const Duration(seconds: 5)))?.type !=
          'cancelled') {
        throw StateError('Running cancellation failed');
      }
      await run(command(['-version']));
      await record(
          'cancel-batch-$batch', {'queuedCancelled': 20, 'recovery': true});
    }
    final bad = await TinyFfmpeg.executeAsync(command(
        ['-i', '${directory.path}/missing.mp4', '${directory.path}/bad.mp4']));
    final badResult =
        await bad.getResult().timeout(const Duration(seconds: 30));
    if (badResult?.isSuccess != false) {
      throw StateError('Missing input was accepted');
    }
    await run(command(['-version']));
    await record('failure-recovery', {'code': badResult?.code});
    final longOutput = '${directory.path}/commercial_long.mp4';
    await run(command([
      '-stream_loop',
      '-1',
      '-i',
      vivid,
      '-t',
      '60',
      '-map',
      '0:v:0',
      '-an',
      '-vf',
      'scale=320:-2',
      '-c:v',
      'libx265',
      '-preset',
      'ultrafast',
      '-x265-params',
      'pools=2:frame-threads=2',
      longOutput,
    ]));
    final longInfo = await TinyFfmpeg.getMediaInfo(longOutput);
    if ((longInfo.durationMilliseconds - 60000).abs() > 100) {
      throw StateError('Looped export duration is incorrect');
    }
    await record(
        'long-export', {'durationMilliseconds': longInfo.durationMilliseconds});
    for (final source in ['vivid', 'hlg']) {
      final input = '${directory.path}/$source.mp4';
      final sourceInfo = await TinyFfmpeg.getMediaInfo(input);
      for (final operation in ['trim', 'crop', 'lossless']) {
        final name = '${source}_$operation';
        status.value = name;
        final output = '${directory.path}/commercial_$name.mp4';
        final cmd = await TinyFfmpeg.createExportCommand(
          input: input,
          output: output,
          start: const Duration(seconds: 1),
          duration: Duration(milliseconds: operation == 'lossless' ? 70 : 1000),
          crop: operation == 'trim'
              ? null
              : TinyFfmpegCrop(
                  width: sourceInfo.displayWidth - 160,
                  height: sourceInfo.displayHeight - 160,
                  x: 80,
                  y: 80),
          lossless: operation == 'lossless',
        );
        final started = DateTime.now();
        await run(cmd);
        final exported = await TinyFfmpeg.getMediaInfo(output);
        if (exported.bitDepth != sourceInfo.bitDepth ||
            exported.colorTransfer != sourceInfo.colorTransfer ||
            exported.hdrVivid != sourceInfo.hdrVivid) {
          throw StateError('HDR properties changed');
        }
        await record(name, {
          'bytes': await File(output).length(),
          'milliseconds': DateTime.now().difference(started).inMilliseconds,
          'hdrVivid': exported.hdrVivid,
          'width': exported.width,
          'height': exported.height
        });
      }
    }
    if (progressEvents == 0) {
      throw StateError('No progress events received with logs disabled');
    }
    await record('PASS', {'progressEvents': progressEvents});
    status.value = 'Regression tests passed';
  } catch (error, stack) {
    await record(
        'FAIL', {'error': error.toString(), 'stack': stack.toString()});
    status.value = error.toString();
  } finally {
    await progress.cancel();
  }
}
