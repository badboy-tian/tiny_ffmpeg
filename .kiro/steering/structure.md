# Project Structure

## Root Layout

```
tiny_ffmpeg/
├── lib/                    # Dart plugin API
├── android/                # Android native implementation
├── ios/                    # iOS native implementation
├── ohos/                   # OpenHarmony native implementation
├── example/                # Example Flutter app
└── test/                   # Dart unit tests
```

## Dart Library (`lib/`)

- **tiny_ffmpeg.dart**: Main plugin class with async execution, session management, and event handling
- **tiny_ffmpeg_cmd.dart**: Command builder helper for constructing FFmpeg arguments

## Android Implementation (`android/`)

```
android/
├── build.gradle                    # Gradle build configuration
├── src/main/
│   ├── AndroidManifest.xml
│   ├── kotlin/com/...              # Kotlin plugin implementation
│   └── cpp/                        # Native C/C++ FFmpeg integration
│       ├── CMakeLists.txt          # CMake build configuration
│       ├── ffmpeg.c/h              # FFmpeg command execution
│       ├── cmdutils.c/h            # Command utilities
│       └── [arm64-v8a, armeabi-v7a]/  # Pre-compiled FFmpeg libraries
```

## iOS Implementation (`ios/`)

```
ios/
├── tiny_ffmpeg.podspec             # CocoaPods specification
├── Classes/
│   ├── SwiftTinyFfmpegPlugin.swift # Swift plugin bridge
│   ├── ffmpeg.c/h                  # FFmpeg command execution
│   ├── cmdutils.c/h                # Command utilities
│   ├── ffmpeg_utils.cpp/h          # Utility functions (public header)
│   ├── include/                    # FFmpeg header files
│   │   ├── libavcodec/
│   │   ├── libavformat/
│   │   ├── libavutil/
│   │   └── ...
│   └── lib/                        # Pre-compiled static libraries (.a files)
```

## OHOS Implementation (`ohos/`)

```
ohos/
├── build-profile.json5             # Build configuration
├── index.ets                       # Plugin entry point
├── src/main/
│   ├── cpp/                        # Native C++ implementation
│   └── ets/                        # ArkTS/ETS plugin code
└── oh_modules/                     # OHOS dependencies
```

## Example App (`example/`)

Standard Flutter app structure demonstrating plugin usage:
- **lib/main.dart**: Example implementation showing audio mixing, filtering, and session management
- **assets/**: Sample media files (mp3, png) for testing
- **android/**, **ios/**, **ohos/**: Platform-specific app configurations

## Key Files

- **pubspec.yaml**: Plugin metadata, version, platform configurations
- **analysis_options.yaml**: Dart linting rules
- **.gitignore**: Excludes build artifacts, IDE files, generated code
- **CHANGELOG.md**: Version history
- **LICENSE**: Open source license
- **README.md**: User documentation (Chinese)

## Build Artifacts (Ignored)

- `.dart_tool/`: Dart tooling cache
- `build/`: Compiled output
- `.flutter-plugins*`: Generated plugin registry
- `Pods/`, `Podfile.lock`: iOS CocoaPods
- `.gradle/`, `*.iml`: Android/IDE files
- `.hvigor/`: OHOS build cache

## Native Code Organization

All platforms follow similar patterns:
1. **Plugin bridge**: Platform-specific code (Swift/Kotlin/ETS) handling Flutter method/event channels
2. **FFmpeg wrapper**: C/C++ code wrapping FFmpeg CLI tools
3. **Pre-compiled libraries**: Static FFmpeg libraries for each architecture
4. **Headers**: FFmpeg public API headers for compilation

## Testing Structure

- **test/tiny_ffmpeg_test.dart**: Dart unit tests
- **test_ffmpeg_headers.sh**: iOS header validation script
- **test_build.sh**: Build verification script

用户是中国人, 新手, 请用中文交流
跑命令用fvm flutter

ffmpeg编译的源码目录在/Users/tian/Downloads/ffmpeg-build-scripts/build/forksource/ffmpeg必要的时候可以复制里面的源码