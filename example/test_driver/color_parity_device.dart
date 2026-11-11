import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg_cmd.dart';

// 三端运行同一入口，验证实际链接的库，避免只检查编译开关就判定能力一致。
// 先向 Documents/parity-input 传入 hlg/vivid/dovi/hdr10plus.mp4，再启动此入口。
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final status = ValueNotifier<String>('Testing native color exports');
  runApp(MaterialApp(
    home: Scaffold(
      body: Center(
        child: ValueListenableBuilder<String>(
          valueListenable: status,
          builder: (_, value, __) => Text(value),
        ),
      ),
    ),
  ));
  final documents = await getApplicationDocumentsDirectory();
  final directory = Directory('${documents.path}/parity-output');
  await directory.create(recursive: true);
  final report = File('${directory.path}/results.json');
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

  Map<String, dynamic> describe(TinyFfmpegMediaInfo info) => {
        'width': info.displayWidth,
        'height': info.displayHeight,
        'bitDepth': info.bitDepth,
        'primaries': info.colorPrimaries,
        'transfer': info.colorTransfer,
        'matrix': info.colorSpace,
        'hdrVivid': info.hdrVivid,
        'hdr10Plus': info.hdr10Plus,
        'dolbyVision': info.dolbyVision,
        'dolbyVisionProfile': info.dolbyVisionProfile,
        'dolbyVisionCompatibilityId': info.dolbyVisionCompatibilityId,
        'durationMilliseconds': info.durationMilliseconds,
      };

  try {
    await TinyFfmpeg.showLog(false);
    for (final kind in ['hlg', 'vivid', 'dovi', 'hdr10plus']) {
      // 兼容已有设备验收素材，鸿蒙沙箱限制写入时可复用文档目录中的原文件。
      final candidate = File('${documents.path}/parity-input/$kind.mp4');
      final input = await candidate.exists()
          ? candidate.path
          : '${documents.path}/$kind.mp4';
      final source = await TinyFfmpeg.getMediaInfo(input);
      if (!source.isHdr ||
          source.bitDepth != 10 ||
          (kind == 'vivid' && !source.hdrVivid) ||
          (kind == 'dovi' && !source.dolbyVision) ||
          (kind == 'hdr10plus' && !source.hdr10Plus)) {
        throw StateError('$kind source has incorrect HDR metadata');
      }
      await record('${kind}_probe', describe(source));
      for (final mode in ['trim', 'crop', 'sdr']) {
        final output = '${directory.path}/${kind}_$mode.mp4';
        status.value = '${kind}_$mode';
        // 小尺寸动态元数据样片同样覆盖 crop，不能用超过源画面的固定裁剪区域。
        final cropWidth = source.displayWidth < 352 ? 128 : 320;
        final cropHeight = source.displayHeight < 212 ? 72 : 180;
        final command = mode == 'sdr'
            ? (TinyFFmpegCMD()
              ..addInput(input)
              ..add('-map')
              ..add('0:${source.videoStreamIndex}')
              ..add('-an')
              ..add('-t')
              ..add('0.3')
              ..add('-vf')
              // 先缩小降低真机回归耗时，仍实际执行线性化、映射、色域转换和帧信息清理。
              ..add('zscale=w=320:h=180:t=linear:npl=203,'
                  'format=gbrpf32le,tonemap=tonemap=mobius:peak=4.9261,'
                  'zscale=p=bt709:t=bt709:m=bt709:r=limited:dither=ordered,'
                  'format=yuv420p,sidedata=mode=delete')
              ..add('-c:v')
              ..add('libx264')
              ..add('-preset')
              ..add('ultrafast')
              ..add('-crf')
              ..add('18')
              ..add('-threads')
              ..add('2')
              ..add('-color_primaries')
              ..add('bt709')
              ..add('-color_trc')
              ..add('bt709')
              ..add('-colorspace')
              ..add('bt709')
              ..add('-color_range')
              ..add('tv')
              ..add('-map_metadata')
              ..add('-1')
              ..addOutput(output))
            : await TinyFfmpeg.createExportCommand(
                input: input,
                output: output,
                start: const Duration(milliseconds: 100),
                duration: const Duration(milliseconds: 300),
                crop: mode == 'crop'
                    ? TinyFfmpegCrop(
                        width: cropWidth, height: cropHeight, x: 16, y: 16)
                    : null,
                lossless: false,
              );
        final watch = Stopwatch()..start();
        final session = await TinyFfmpeg.executeAsync(command);
        final result =
            await session.getResult().timeout(const Duration(minutes: 5));
        if (result == null || !result.isSuccess) {
          throw StateError(
              '$kind/$mode failed: ${await session.getErrorMessage()}');
        }
        final exported = await TinyFfmpeg.getMediaInfo(output);
        if (mode == 'sdr') {
          if (exported.isHdr ||
              exported.bitDepth != 8 ||
              exported.colorPrimaries != 'bt709' ||
              exported.colorTransfer != 'bt709' ||
              exported.colorSpace != 'bt709') {
            throw StateError('$kind SDR output retained HDR signalling');
          }
        } else if (exported.bitDepth != source.bitDepth ||
            exported.colorTransfer != source.colorTransfer ||
            exported.hdrVivid != source.hdrVivid ||
            exported.hdr10Plus != source.hdr10Plus ||
            exported.dolbyVision != source.dolbyVision ||
            // 部分手机仅写入 RPU 而没有容器配置；源 profile=0 表示未知，编码后可补齐配置。
            (source.dolbyVisionProfile != 0 &&
                (exported.dolbyVisionProfile != source.dolbyVisionProfile ||
                    exported.dolbyVisionCompatibilityId !=
                        source.dolbyVisionCompatibilityId))) {
          throw StateError('$kind/$mode changed HDR type');
        }
        final expectedWidth = mode == 'trim'
            ? source.displayWidth
            : (mode == 'crop' ? cropWidth : 320);
        final expectedHeight = mode == 'trim'
            ? source.displayHeight
            : (mode == 'crop' ? cropHeight : 180);
        if (exported.displayWidth != expectedWidth ||
            exported.displayHeight != expectedHeight) {
          throw StateError('$kind/$mode has incorrect dimensions');
        }
        await record('${kind}_$mode', {
          ...describe(exported),
          'bytes': await File(output).length(),
          'milliseconds': watch.elapsedMilliseconds,
        });
      }
    }
    if (progressEvents == 0) throw StateError('No encoding progress events');
    await record('PASS', {'progressEvents': progressEvents});
    status.value = 'Native color exports passed';
  } catch (error, stack) {
    await record('FAIL', {'error': '$error', 'stack': '$stack'});
  } finally {
    await progress.cancel();
  }
}
