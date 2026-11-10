import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg_cmd.dart';

// 独立入口通过真实插件通道回归，避免把设备测试流程混入日常示例。
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final status = ValueNotifier<String>('Running HDR tests');
  runApp(MaterialApp(
    home: Scaffold(
        body: Center(
            child: ValueListenableBuilder<String>(
      valueListenable: status,
      builder: (_, value, __) => Text(value),
    ))),
  ));
  final directory = await getApplicationDocumentsDirectory();
  final results = <Map<String, dynamic>>[];
  final report = File('${directory.path}/hdr-results.json');
  try {
    await TinyFfmpeg.showLog(false);
    for (final source in ['vivid', 'hlg']) {
      for (final operation in ['trim', 'crop', 'lossless']) {
        final name = '${source}_$operation';
        status.value = name;
        final output = '${directory.path}/$name.mp4';
        final args = <String>[
          '-threads', '2', '-ss', '1',
          '-i', '${directory.path}/$source.mp4',
          '-t', operation == 'lossless' ? '0.07' : '1',
          '-map', '0:v:0', '-map', '0:a?',
          if (operation != 'trim') ...[
            '-vf',
            'crop=iw-160:ih-160:80:80',
          ],
          '-c:v', 'libx265', '-preset', 'ultrafast',
          '-pix_fmt', 'yuv420p10le', '-fps_mode', 'passthrough',
          '-color_primaries', 'bt2020', '-color_trc', 'arib-std-b67',
          '-colorspace', 'bt2020nc', '-color_range', 'tv',
          '-c:a', 'copy', '-crf', '18',
          '-x265-params',
          // 无损回归只编码少量帧，确保真机资源足够且能够逐像素检查。
          '${operation == 'lossless' ? 'lossless=1:' : ''}pools=2:frame-threads=2',
          output,
        ];
        final command = TinyFFmpegCMD();
        args.forEach(command.add);
        final started = DateTime.now();
        final session = await TinyFfmpeg.executeAsync(command);
        final result =
            await session.getResult().timeout(const Duration(minutes: 5));
        results.add({
          'name': name,
          'code': result?.code,
          'bytes':
              await File(output).exists() ? await File(output).length() : 0,
          'milliseconds': DateTime.now().difference(started).inMilliseconds,
          if (result?.isSuccess != true)
            'error': await session.getErrorMessage(),
        });
        await report.writeAsString(jsonEncode(results), flush: true);
        if (result?.isSuccess != true) {
          throw StateError('$name failed');
        }
      }
    }
    status.value = 'HDR tests passed';
  } catch (error) {
    results.add({'failure': error.toString()});
    await report.writeAsString(jsonEncode(results), flush: true);
    status.value = error.toString();
  }
}
