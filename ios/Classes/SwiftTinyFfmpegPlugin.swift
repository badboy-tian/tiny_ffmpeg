import Flutter
import UIKit
//import CMDUtil

//extern int Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_executeFFmpegCommand(int cmdLen, char* argv[], long totalTime);
public class SwiftTinyFfmpegPlugin: NSObject, FlutterPlugin {
    static var re: FlutterResult? = nil
    
  public static func register(with registrar: FlutterPluginRegistrar) {
    let channel = FlutterMethodChannel(name: "tiny_ffmpeg", binaryMessenger: registrar.messenger())
    let instance = SwiftTinyFfmpegPlugin()
    registrar.addMethodCallDelegate(instance, channel: channel)
  }
    
  public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
      if(call.method == "getPlatformVersion"){
          result("iOS " + UIDevice.current.systemVersion)
      }else if(call.method == "executeFFmpegCommand"){
          SwiftTinyFfmpegPlugin.re = result
          
          let dict = call.arguments as! NSDictionary
          let argc = dict["argc"] as! Int
          let argv = dict["argv"] as! NSArray
          
          let cargs = UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>.allocate(capacity: argc)
          
          for i in 0..<argv.count {
              let value = argv.object(at: i) as! String
              
              let v = value.cString(using: .utf8)
              let pointer = UnsafeMutablePointer<CChar>.allocate(capacity: v!.count)
              for j in 0..<v!.count {
                  pointer[j] = v![j]
              }
              
              cargs[i] = pointer
          }
        
          DispatchQueue.main.async(execute: {
              _ = Java_com_i7play_tiny_ffmpeg_FFMpegUtils_executeFFmpegCommand(Int32(argc), cargs, -1)
          })
      }else if(call.method == "showLog"){
          let showLog = call.arguments as! Bool
          if(showLog){
              Java_com_i7play_tiny_ffmpeg_FFMpegUtils_showLog(1);
          }else{
              Java_com_i7play_tiny_ffmpeg_FFMpegUtils_showLog(0);
          }
          
          result(showLog)
      }else if(call.method == "getMediaDuration"){
          let mediaPath = call.arguments as! String
          let duration = Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getMediaDuration(mediaPath);
          result(duration)
      }else if(call.method == "cancelExecuteFFmpegCommand"){
          Java_com_i7play_tiny_ffmpeg_FFMpegUtils_cancelExecuteFFmpegCommand();
          result(true)
      }
    
  }
    @_silgen_name("Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progress")
     func Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progress(progress: Float) {
        //print("fetch....\(progress)\n")
        var map = Dictionary<String, Any>()
        map["type"] = "progress"
        map["code"] = 0
        map["message"] = progress
        if (SwiftTinyFfmpegPlugin.re != nil) {
            SwiftTinyFfmpegPlugin.re!(map)
        }
        
    }
    
    @_silgen_name("Java_com_i7play_tiny_ffmpeg_FFMpegUtils_result")
     func Java_com_i7play_tiny_ffmpeg_FFMpegUtils_result(code: Int, msg: UnsafeMutablePointer<CChar>?) {
        let msg = String(cString: msg!, encoding: String.Encoding.utf8)!
        if(code == 0){
            var map = Dictionary<String, Any>()
            map["type"] = "result"
            map["code"] = 0
            map["message"] = "success"
            if (SwiftTinyFfmpegPlugin.re != nil) {
                SwiftTinyFfmpegPlugin.re!(map)
            }
        }else{
            var map = Dictionary<String, Any>()
            map["type"] = -1
            map["code"] = 0
            map["message"] = msg
            if (SwiftTinyFfmpegPlugin.re != nil) {
                SwiftTinyFfmpegPlugin.re!(map)
            }
        }
    }
}
