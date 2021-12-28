import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tiny_ffmpeg/tiny_ffmpeg.dart';

void main() {
  const MethodChannel channel = MethodChannel('tiny_ffmpeg');

  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    channel.setMockMethodCallHandler((MethodCall methodCall) async {
      return '42';
    });
  });

  tearDown(() {
    channel.setMockMethodCallHandler(null);
  });

  test('getPlatformVersion', () async {
    expect(await TinyFfmpeg.platformVersion, '42');
  });
}
