#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint tiny_ffmpeg.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'tiny_ffmpeg'
  # 与 Dart 分发版本一致，使 CocoaPods 能识别此次原生产物更新。
  s.version          = '0.0.13'
  s.summary          = 'A new Flutter project.'
  s.description      = <<-DESC
A new Flutter project.
                       DESC
  s.homepage         = 'http://example.com'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'Your Company' => 'email@example.com' }
  s.source           = { :path => '.' }
  s.source_files = 'Classes/**/*.{h,m,mm,c,cpp,swift}'
  # 头文件通过 include 根目录定位，避免 CocoaPods 把 libavutil/time.h 映射成系统 time.h。
  s.exclude_files = 'Classes/include/**/*'
  s.libraries = 'z', 'bz2', 'iconv', 'c++'
  s.requires_arc = true

  s.public_header_files = 'Classes/tiny_ffmpeg_swift.h'
  s.preserve_paths = 'Classes/**/*.h', 'Classes/include/**/*'
  s.ios.vendored_libraries = 'Classes/lib/*.a'
  s.ios.frameworks = 'CoreMedia', 'VideoToolBox', "AudioToolBox", "AVFoundation"
  s.dependency 'Flutter'
  s.platform = :ios, '15.0' # 当前静态库和 Xcode SDK 均以 iOS 15 为最低部署版本
  
  s.xcconfig = {
    'HEADER_SEARCH_PATHS' => '$(inherited) "${PODS_TARGET_SRCROOT}/Classes/include"',
    'OTHER_CFLAGS' => '$(inherited) -D_DARWIN_C_SOURCE'
  }

  # Flutter.framework does not contain a i386 slice.
  s.pod_target_xcconfig = { 
    'DEFINES_MODULE' => 'YES', 
    'HEADER_SEARCH_PATHS' => '$(inherited) "${PODS_TARGET_SRCROOT}/Classes/include" "${PODS_ROOT}/tiny_ffmpeg/Classes/include"',
    'OTHER_CFLAGS' => '$(inherited) -D_DARWIN_C_SOURCE',
    'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) _DARWIN_C_SOURCE=1',
    'CLANG_ALLOW_NON_MODULAR_INCLUDES_IN_FRAMEWORK_MODULES' => 'YES',
    'SWIFT_INCLUDE_PATHS' => '$(inherited) "${PODS_TARGET_SRCROOT}/Classes"'
  }
  
  # 当前归档为 arm64 真机产物，模拟器需另行构建对应平台的库。

  s.swift_version = '5.0'
end
