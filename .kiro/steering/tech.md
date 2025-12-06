# Technology Stack

## Framework & Language

- **Flutter**: 1.20.0+
- **Dart**: SDK >=2.15.0 <4.0.0
- **Swift**: 5.0 (iOS)
- **Kotlin**: 1.6.10 (Android)
- **C/C++**: Native FFmpeg integration

## Native Build Systems

### Android
- **Gradle**: 4.1.3
- **CMake**: 3.10.2
- **NDK**: Supports armeabi-v7a, arm64-v8a architectures
- **Compile SDK**: 30
- **Min SDK**: 16

### iOS
- **CocoaPods**: Dependency management
- **Platform**: iOS 10.1+
- **Architectures**: arm64, i386 (simulator)
- **Frameworks**: CoreMedia, VideoToolBox, AudioToolBox, AVFoundation
- **Static Libraries**: libz, libbz2, libiconv, libc++

### OpenHarmony (OHOS)
- **Hvigor**: Build system
- **AKI**: Native interop framework

## FFmpeg Libraries

Pre-compiled static libraries included:
- libavcodec.a
- libavdevice.a
- libavfilter.a
- libavformat.a
- libavutil.a
- libswresample.a
- libswscale.a
- libfdk-aac.a (iOS)
- libmp3lame.a
- libx264.a

## Plugin Architecture

- **Method Channel**: `tiny_ffmpeg` - For command execution and control
- **Event Channel**: `tiny_ffmpeg_progress_event` - For progress updates and results
- **Session-based**: Async execution with cancellation support

## Common Commands

### Development
```bash
# Run example app
cd example
flutter run

# Clean and reinstall dependencies
flutter clean
flutter pub get
```

### iOS Specific
```bash
# Clean CocoaPods cache
cd ios
pod cache clean --all
rm -rf Pods Podfile.lock
pod install

# Validate podspec
pod lib lint tiny_ffmpeg.podspec
```

### Android Specific
```bash
# Build Android library
cd android
./gradlew build

# Clean build
./gradlew clean
```

### Testing
```bash
# Run Dart tests
flutter test

# Test FFmpeg headers (iOS)
./test_ffmpeg_headers.sh

# Test build
./test_build.sh
```

## Dependencies

### Runtime
- flutter (SDK)

### Development
- flutter_test (SDK)
- flutter_lints: ^1.0.0

### Example App
- audioplayers: Audio playback testing
- path_provider: File system access
