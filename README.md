# tiny_ffmpeg

一个轻量级的 Flutter FFmpeg 插件，支持 iOS 和 Android 平台。

## 功能特点

- 支持 FFmpeg 基础功能
- 轻量级实现
- 支持 iOS 和 Android 双平台

## 安装

将以下内容添加到您的 `pubspec.yaml` 文件中：

```yaml
tiny_ffmpeg:
  git:
    url: https://github.com/badboy-tian/tiny_ffmpeg.git
    ref: main # 或指定具体的tag，如 ref: v0.0.3
```

## 平台要求

- iOS: 10.1 或更高版本
- Android: minSdkVersion 16 或更高版本
- Flutter: 1.20.0 或更高版本

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

本项目遵循 [LICENSE](LICENSE) 开源协议。

## 问题反馈

如果您发现任何问题或有改进建议，欢迎在 [GitHub Issues](https://github.com/badboy-tian/tiny_ffmpeg/issues) 提出。
