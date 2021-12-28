#import "TinyFfmpegPlugin.h"
#if __has_include(<tiny_ffmpeg/tiny_ffmpeg-Swift.h>)
#import <tiny_ffmpeg/tiny_ffmpeg-Swift.h>
#else
// Support project import fallback if the generated compatibility header
// is not copied when this plugin is created as a library.
// https://forums.swift.org/t/swift-static-libraries-dont-copy-generated-objective-c-header/19816
#import "tiny_ffmpeg-Swift.h"
#endif

@implementation TinyFfmpegPlugin
+ (void)registerWithRegistrar:(NSObject<FlutterPluginRegistrar>*)registrar {
  [SwiftTinyFfmpegPlugin registerWithRegistrar:registrar];
}
@end
