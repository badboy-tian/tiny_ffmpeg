# tiny_ffmpeg

一个轻量级的 Flutter FFmpeg 插件，支持 iOS、Android 和 HarmonyOS 平台。

## 功能特点

- 支持 FFmpeg 基础功能
- 轻量级实现
- 支持 iOS、Android 和 HarmonyOS

## 安装

将以下内容添加到您的 `pubspec.yaml` 文件中：

```yaml
tiny_ffmpeg:
  git:
    url: https://github.com/badboy-tian/tiny_ffmpeg.git
    ref: main # 或指定具体的tag，如 ref: v0.0.3
```

## 平台要求

- iOS 15.0 或更高版本，当前随附 arm64 真机静态库
- Android API 21 或更高版本，当前随附 arm64-v8a 产物
- HarmonyOS 使用匹配的鸿蒙 Flutter SDK，当前随附 arm64-v8a 产物

## iOS 注意事项

如果遇到头文件找不到的问题，请尝试以下步骤：

1. 清理并重新安装 pods：
```
cd ios
pod cache clean --all
rm -rf Pods
rm -rf Podfile.lock
pod install
```

2. 如果仍然无法解决问题，请检查 `tiny_ffmpeg.podspec` 文件中的 `s.public_header_files` 和 `s.preserve_paths` 是否正确。

3. 如果问题依然存在，请检查 `Podfile` 文件中的 `header_mappings_dir` 是否正确。

## 许可证

项目自身的授权状态见 [LICENSE](LICENSE)，第三方组件的原始许可和交付要求见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 问题反馈

如果您发现任何问题或有改进建议，欢迎在 [GitHub Issues](https://github.com/badboy-tian/tiny_ffmpeg/issues) 提出。

## 保留源视频的 HDR 和画质

```dart
import 'package:tiny_ffmpeg/tiny_ffmpeg.dart';

final info = await TinyFfmpeg.getMediaInfo(inputPath);
final command = await TinyFfmpeg.createExportCommand(
  input: inputPath,
  output: outputPath,
  start: const Duration(seconds: 1),
  duration: const Duration(seconds: 3),
  crop: TinyFfmpegCrop(
    width: info.displayWidth - 160,
    height: info.displayHeight - 160,
    x: 80,
    y: 80,
  ),
);
final session = await TinyFfmpeg.executeAsync(command);
final result = await session.getResult();
```

统一入口自动读取源视频的位深、色彩标签和显示旋转。8-bit SDR 使用 x264，10-bit 使用 x265，保留 HLG/PQ 参数。默认 lossless=true，保证已解码画面的无损编码，文件体积和编码耗时会增加。显式设置 lossless=false、crf=18 可采用有损压缩。原命令接口继续接受自定义编码参数。

动态 HDR 类型通过流信息和首个解码帧识别。统一入口支持 HDR Vivid、HDR10+ 和单层 Dolby Vision Profile 5、8。HDR Vivid、HDR10+ 的 T.35 参数随对应帧写回，Dolby Vision 按帧编码 RPU 并生成 MP4 配置记录。已识别的双层增强层以及其他 Dolby Vision profile 会返回 UnsupportedError，防止丢弃增强层画面。

crop 坐标按显示旋转后的画面计算，并要求偶数边界。输出为 MP4，音频使用 copy。动态 HDR 的亮度统计、映射曲线和区域描述沿用源帧，没有针对裁剪区域重新分析。异常或取消后调用方应清理未完成的输出文件。无损编码保留解码像素，Dolby Vision 的流级 level 会由编码器按输出规格重新计算。

进度订阅与 Session 结果共用原生事件通道。progress 事件包含 sessionId、time、size、speed，time 使用毫秒，size 使用字节，speed 为处理速度倍数。关闭日志后进度和错误结果仍可获取。取消任务会完成已等待的 getResult，原生资源在执行线程安全退出后释放。

## example 的平台依赖

标准 Flutter 构建 Android/iOS 时，在 example 目录移除本地 pubspec_overrides.yaml 并重新解析依赖。

```bash
cd example
rm -f pubspec_overrides.yaml
flutter pub get
flutter build apk --debug --target-platform android-arm64
# 或 flutter build ios --release --no-codesign
```

鸿蒙 SDK 提供 TargetPlatform.ohos，需要独立的依赖覆盖文件。

```bash
cd example
cp ohos/pubspec_overrides.yaml pubspec_overrides.yaml
/path/to/ohos-flutter/bin/flutter pub get
/path/to/ohos-flutter/bin/flutter build hap --debug
```

## 回归验证

仓库根目录执行 flutter test，覆盖取消、事件竞态、错误恢复与导出参数。example/test_driver/commercial_device.dart 为独立设备测试入口，通过真实 MethodChannel 覆盖时长与 HDR 探测、80 个排队取消、4 个运行中取消、失败恢复、60 秒循环编码以及两段视频的剪辑、crop、无损导出。测试前将输入命名为 vivid.mp4、hlg.mp4，放入 getApplicationDocumentsDirectory 返回的目录。

```bash
flutter test
cd example
flutter build apk --debug --target-platform android-arm64 -t test_driver/commercial_device.dart
# 鸿蒙依赖配置完成后，可改用鸿蒙 Flutter build hap --debug -t 同一路径。
```

宿主测试设置 NO_PROXY=localhost,127.0.0.1,::1 可避免本地 HTTP 代理影响 Flutter 测试运行器的连接。

example/test_driver/dynamic_hdr_device.dart 覆盖 Dolby Vision 8.4、5、8.1 和 HDR10+ 元数据版本 0、1 的默认无损剪辑、默认无损 crop、有损 crop，核对输出 profile、位深、色彩信息和关闭日志后的进度。输入分别命名为 dovi.mp4、dovi5.mp4、dovi81.mp4、hdr10plus.mp4、hdr10plus0.mp4，放入应用 documents 目录。8.4 使用真实手机素材，5 和 8.1 使用带独立 RPU 的合成测试素材。主机侧另外逐帧核对 RPU 语义、HDR10+ 原始 T.35 数据和无损解码像素，不能只依赖文件上的 HDR 标记。

<!-- 音轨探测兼容旧原生版本，不能从缺省字段推断可以直拷。 -->
`getMediaInfo()` 返回 `audioStreams`（每项含绝对 `index` 与 `codec`）。
`canCopyAudioToMp4` 仅在已知音轨全部为 AAC 时为 true；未知或混合编码使用 AAC 回退。
调用 `session.cancelAndWait()` 可在独立方法通道上确认原生任务停止；失败或超时时，宿主应保留仍可能正在写入的输出文件。
