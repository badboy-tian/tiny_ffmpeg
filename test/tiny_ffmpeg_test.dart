import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg_cmd.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  const methods = MethodChannel('tiny_ffmpeg');
  const events = MethodChannel('tiny_ffmpeg_progress_event');
  final messenger =
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
  var nextId = 1;
  var nativeListens = 0;
  var cancelCalls = 0;
  var earlyResult = false;
  var failCreation = false;
  var badProbe = false;

  Future<void> emit(Map<String, dynamic> value) async {
    final delivered = Completer<void>();
    messenger.handlePlatformMessage(
        events.name,
        const StandardMethodCodec().encodeSuccessEnvelope(value),
        (_) => delivered.complete());
    await delivered.future;
  }

  setUpAll(() {
    messenger.setMockMethodCallHandler(events, (call) async {
      if (call.method == 'listen') nativeListens++;
      return null;
    });
    messenger.setMockMethodCallHandler(methods, (call) async {
      switch (call.method) {
        case 'executeFFmpegCommand':
          if (failCreation) throw PlatformException(code: 'SESSION_ERROR');
          final id = nextId++;
          if (earlyResult) {
            await emit({
              'type': 'result',
              'sessionId': id,
              'code': 0,
              'message': 'early'
            });
          }
          return {'sessionId': id};
        case 'cancelSession':
          cancelCalls++;
          return true;
        case 'getMediaInfo':
          return badProbe ? '{"error":-22}' : jsonEncode(source());
        case 'showLog':
          expect(call.arguments, {'show': false});
          return null;
        default:
          return '';
      }
    });
  });
  setUp(() {
    earlyResult = false;
    failCreation = false;
    badProbe = false;
  });

  // 回归真实缺陷，取消必须完成已经开始等待的 Future，重复取消不能重复调原生。
  test('cancel resolves all waiters and is idempotent', () async {
    final session = await TinyFfmpeg.executeAsync(TinyFFmpegCMD());
    final first = session.getResult();
    final second = session.getResult();
    final before = cancelCalls;
    await Future.wait([session.cancel(), session.cancel()]);
    expect(
        (await first.timeout(const Duration(seconds: 1)))?.type, 'cancelled');
    expect((await second)?.type, 'cancelled');
    expect((await session.getResult())?.type, 'cancelled');
    expect(cancelCalls, before + 1);
    await emit({
      'type': 'result',
      'sessionId': int.parse(session.sessionId),
      'code': 0
    });
    expect(session.state, SessionState.cancelled);
  });

  test('early results are delivered before session registration', () async {
    earlyResult = true;
    for (var i = 0; i < 100; i++) {
      final session = await TinyFfmpeg.executeAsync(TinyFFmpegCMD());
      expect((await session.getResult())?.isSuccess, true);
      await session.cancel();
      expect(session.state, SessionState.completed);
    }
  });

  test('progress consumers share one native subscription and preserve results',
      () async {
    final seen = <dynamic>[];
    final a = TinyFfmpeg.listenProgress.listen(seen.add);
    final b = TinyFfmpeg.listenProgress.listen((_) {});
    final session = await TinyFfmpeg.executeAsync(TinyFFmpegCMD());
    await emit(
        {'type': 'progress', 'sessionId': session.sessionId, 'time': 500});
    await a.cancel();
    await b.cancel();
    await emit({'type': 'result', 'sessionId': session.sessionId, 'code': 0});
    expect((await session.getResult())?.isSuccess, true);
    expect(seen.any((dynamic event) => event['type'] == 'progress'), true);
    expect(nativeListens, 1);
  });

  test('failure caches native errors and permits next execution', () async {
    final failed = await TinyFfmpeg.executeAsync(TinyFFmpegCMD());
    await emit({
      'type': 'result',
      'sessionId': failed.sessionId,
      'code': -22,
      'message': 'bad input',
      'errorLog': 'Invalid data'
    });
    expect((await failed.getResult())?.isSuccess, false);
    expect(await failed.getErrorMessage(), 'Invalid data');
    earlyResult = true;
    final next = await TinyFfmpeg.executeAsync(TinyFFmpegCMD());
    expect((await next.getResult())?.isSuccess, true);
  });

  test('creation failure and probe failure propagate', () async {
    failCreation = true;
    await expectLater(TinyFfmpeg.executeAsync(TinyFFmpegCMD()),
        throwsA(isA<PlatformException>()));
    badProbe = true;
    await expectLater(
        TinyFfmpeg.getMediaInfo('/bad'), throwsA(isA<PlatformException>()));
    failCreation = false;
    earlyResult = true;
    expect(
        (await (await TinyFfmpeg.executeAsync(TinyFFmpegCMD())).getResult())
            ?.isSuccess,
        true);
  });

  test('log switch uses a consistent map protocol',
      () => TinyFfmpeg.showLog(false));

  // 原生事件错误也应完成所有等待者，之后仍允许继续创建任务。
  test('event channel errors complete pending results', () async {
    final session = await TinyFfmpeg.executeAsync(TinyFFmpegCMD());
    final delivered = Completer<void>();
    messenger.handlePlatformMessage(
        events.name,
        const StandardMethodCodec().encodeErrorEnvelope(
            code: 'ENGINE_ERROR', message: 'engine disconnected'),
        (_) => delivered.complete());
    await delivered.future;
    expect(
        (await session.getResult().timeout(const Duration(seconds: 1)))
            ?.isSuccess,
        false);
    earlyResult = true;
    final recovered = await TinyFfmpeg.executeAsync(TinyFFmpegCMD());
    expect((await recovered.getResult())?.isSuccess, true);
  });

  test('native probe copies and distribution flags stay consistent', () {
    final shared =
        File('android/src/main/cpp/tiny_media_info.h').readAsStringSync();
    expect(File('ios/Classes/tiny_media_info.h').readAsStringSync(), shared);
    expect(
        File('ohos/src/main/cpp/tiny_media_info.h').readAsStringSync(), shared);
    for (final path in [
      'android/src/main/cpp/fftools/config.h',
      'ios/Classes/config.h',
      'ohos/src/main/cpp/ffmpeg/config.h'
    ]) {
      final config = File(path).readAsStringSync();
      expect(config, contains('#define CONFIG_NONFREE 0'));
      expect(config, contains('#define CONFIG_LIBFDK_AAC 0'));
    }
  });

  // -y 不能经由符号链接或硬链接覆盖源文件。
  test('export factory rejects links to the input', () async {
    final directory =
        await Directory.systemTemp.createTemp('tiny_ffmpeg_alias_');
    try {
      final input = File('${directory.path}/input.mp4');
      await input.writeAsBytes([0]);
      final alias = '${directory.path}/alias.mp4';
      await Link(alias).create(input.path);
      await expectLater(
          TinyFfmpeg.createExportCommand(input: input.path, output: alias),
          throwsArgumentError);
      await Link(alias).delete();
      final linked = await Process.run('ln', [input.path, alias]);
      expect(linked.exitCode, 0);
      await expectLater(
          TinyFfmpeg.createExportCommand(input: input.path, output: alias),
          throwsArgumentError);
      expect(await input.readAsBytes(), [0]);
    } finally {
      await directory.delete(recursive: true);
    }
  }, skip: Platform.isWindows);

  test('HDR crop preserves source properties and defaults to lossless', () {
    final info = TinyFfmpegMediaInfo.fromMap(source());
    final args = info
        .exportCommand(
            input: '/in.mp4',
            output: '/out.mp4',
            crop: const TinyFfmpegCrop(width: 920, height: 1760, x: 80, y: 80))
        .build();
    expect(info.displayWidth, 1080);
    expect(args,
        containsAll(['libx265', 'yuv420p10le', 'arib-std-b67', 'bt2020']));
    expect(args, contains('crop=920:1760:80:80'));
    expect(args, contains('0:1'));
    expect(args, contains('lossless=1:pools=2:frame-threads=2'));
    expect(args, containsAll(['-c:a', 'copy']));
  });

  test('SDR keeps 8-bit and uses lossless x264 without invented HDR tags', () {
    final map = source()
      ..addAll({
        'bitDepth': 8,
        'pixelFormat': 'yuv420p',
        'colorTransfer': 'unknown',
        'colorPrimaries': 'unknown',
        'hdrVivid': false
      });
    final args = TinyFfmpegMediaInfo.fromMap(map)
        .exportCommand(input: '/in', output: '/out.mp4')
        .build();
    expect(args, containsAll(['libx264', 'yuv420p']));
    expect(args[args.indexOf('-crf') + 1], '0');
    expect(args, isNot(contains('-color_trc')));
  });

  test('PQ is preserved and lossy quality requires explicit selection', () {
    final map = source()..['colorTransfer'] = 'smpte2084';
    final args = TinyFfmpegMediaInfo.fromMap(map)
        .exportCommand(
            input: '/in', output: '/out.mp4', lossless: false, crf: 20)
        .build();
    expect(args, contains('smpte2084'));
    expect(args[args.indexOf('-crf') + 1], '20');
    expect(args.join(' '), isNot(contains('lossless=1')));
  });

  // HDR10+ 由编码器从帧附加数据写回，命令生成不能再次把它挡在入口外。
  test('HDR10+ exports retain PQ and lossless Main 10', () {
    final info = TinyFfmpegMediaInfo.fromMap(source()
      ..addAll({
        'hdrVivid': false,
        'hdr10Plus': true,
        'colorTransfer': 'smpte2084'
      }));
    final args = info.exportCommand(input: '/in', output: '/out.mp4').build();
    expect(info.isHdr, isTrue);
    expect(args, containsAll(['libx265', 'smpte2084', 'yuv420p10le']));
    expect(args.join(' '), contains('lossless=1'));
  });

  // 必须显式启用 RPU 编码与 MP4 配置写回，避免输出只剩普通 HLG。
  test('single-layer Dolby Vision enables RPU encoding and muxing', () {
    for (final profile in [5, 8]) {
      final info = TinyFfmpegMediaInfo.fromMap(source()
        ..addAll({
          'hdrVivid': false,
          'dolbyVision': true,
          'dolbyVisionProfile': profile,
          'dolbyVisionCompatibilityId': profile == 8 ? 4 : 0
        }));
      final args = info.exportCommand(input: '/in', output: '/out.mp4').build();
      expect(info.dolbyVisionProfile, profile);
      expect(args, containsAll(['-dolbyvision', '1', '-strict', 'unofficial']));
      expect(args.join(' '), contains('vbv-maxrate=100000:vbv-bufsize=100000'));
    }
  });

  // 双层画面需要保留增强层，不能把单层重编码伪装成完整支持。
  test('Dolby Vision enhancement layers fail before discarding picture data',
      () {
    for (final map in [
      {'dolbyVisionProfile': 7},
      {'dolbyVisionProfile': 8, 'dolbyVisionEnhancementLayer': true}
    ]) {
      final info = TinyFfmpegMediaInfo.fromMap(
          source()..addAll({'dolbyVision': true, ...map}));
      expect(() => info.exportCommand(input: '/in', output: '/out.mp4'),
          throwsUnsupportedError);
    }
  });

  // 非法裁剪仍在生成命令前拦截，新增 HDR 类型不能削弱原有输入保护。
  test('invalid regions fail before encoding', () {
    final info = TinyFfmpegMediaInfo.fromMap(source());
    expect(() => info.exportCommand(input: '/same.mp4', output: '/same.mp4'),
        throwsArgumentError);
    expect(
        () => info.exportCommand(
            input: '/in',
            output: '/out.mp4',
            start: const Duration(seconds: -1)),
        throwsArgumentError);
    expect(
        () => info.exportCommand(
            input: '/in',
            output: '/out.mp4',
            crop: const TinyFfmpegCrop(width: 1081, height: 1920)),
        throwsArgumentError);
    expect(
        () => info.exportCommand(
            input: '/in',
            output: '/out.mp4',
            crop: const TinyFfmpegCrop(width: 1080, height: 1920, x: 2)),
        throwsArgumentError);
  });
}

Map<String, dynamic> source() => {
      'durationMilliseconds': 5000,
      'hasVideo': true,
      'hasAudio': true,
      'videoStreamIndex': 1,
      'width': 1920,
      'height': 1080,
      'rotation': -90,
      'bitDepth': 10,
      'pixelFormat': 'yuv420p10le',
      'colorPrimaries': 'bt2020',
      'colorTransfer': 'arib-std-b67',
      'colorSpace': 'bt2020nc',
      'colorRange': 'tv',
      'hdrVivid': true,
      'hdr10Plus': false,
      'dolbyVision': false,
    };
