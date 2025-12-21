#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint tiny_ffmpeg.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'tiny_ffmpeg'
  s.version          = '0.0.5'
  s.summary          = 'A new Flutter project.'
  s.description      = <<-DESC
A new Flutter project.
                       DESC
  s.homepage         = 'http://example.com'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'Your Company' => 'email@example.com' }
  s.source           = { :path => '.' }
  s.source_files = 'Classes/**/*.{h,m,mm,c,cpp,swift}'
  s.exclude_files = 'Classes/include/compat/**/*'
  s.libraries = 'z', 'bz2', 'iconv', 'c++'
  s.requires_arc = true

  s.public_header_files = 'Classes/tiny_ffmpeg_swift.h'
  s.preserve_paths = 'Classes/**/*.h', 'Classes/include/**/*'
  s.ios.vendored_libraries = 'Classes/lib/*.a'
  s.ios.frameworks = 'CoreMedia', 'VideoToolBox', "AudioToolBox", "AVFoundation"
  s.dependency 'Flutter'
  s.platform = :ios, '10.1'
  
  s.xcconfig = {
    'HEADER_SEARCH_PATHS' => '$(inherited) "${PODS_TARGET_SRCROOT}/Classes/include"',
    'OTHER_CFLAGS' => '$(inherited) -D_DARWIN_C_SOURCE'
  }

  # Flutter.framework does not contain a i386 slice.
  s.pod_target_xcconfig = { 
    'DEFINES_MODULE' => 'YES', 
    'VALID_ARCHS[sdk=iphonesimulator*]' => 'i386',
    'HEADER_SEARCH_PATHS' => '$(inherited) "${PODS_TARGET_SRCROOT}/Classes/include" "${PODS_ROOT}/tiny_ffmpeg/Classes/include"',
    'OTHER_CFLAGS' => '$(inherited) -D_DARWIN_C_SOURCE',
    'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) _DARWIN_C_SOURCE=1',
    'CLANG_ALLOW_NON_MODULAR_INCLUDES_IN_FRAMEWORK_MODULES' => 'YES',
    'SWIFT_INCLUDE_PATHS' => '$(inherited) "${PODS_TARGET_SRCROOT}/Classes"'
  }
  
  valid_archs = ['arm64', 'i386']

  s.swift_version = '5.0'
end
