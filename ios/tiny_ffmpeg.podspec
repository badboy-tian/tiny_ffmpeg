#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint tiny_ffmpeg.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'tiny_ffmpeg'
  s.version          = '0.0.1'
  s.summary          = 'A new Flutter project.'
  s.description      = <<-DESC
A new Flutter project.
                       DESC
  s.homepage         = 'http://example.com'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'Your Company' => 'email@example.com' }
  s.source           = { :path => '.' }
  s.source_files = 'Classes/**/*'
  s.libraries = 'z', 'bz2', 'iconv', 'c++'
  s.requires_arc = true

  # s.static_framework = true
  s.ios.private_header_files = 'Classes/include/**/*'
  s.ios.header_mappings_dir = 'Classes/include/'
  s.ios.vendored_libraries = 'Classes/lib/*.a'
  s.ios.frameworks = 'CoreMedia', 'VideoToolBox', "AudioToolBox", "AVFoundation"
  s.dependency 'Flutter'
  s.platform = :ios, '10.1'

  # Flutter.framework does not contain a i386 slice.
  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES', 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386' }
  s.swift_version = '5.0'
end
