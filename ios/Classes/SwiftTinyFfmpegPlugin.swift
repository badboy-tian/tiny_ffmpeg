import Flutter
import UIKit
//import CMDUtil

//extern int Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_executeFFmpegCommand(int cmdLen, char* argv[], long totalTime);
public class SwiftTinyFfmpegPlugin: NSObject, FlutterPlugin, FlutterStreamHandler {
    public func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
        SwiftTinyFfmpegPlugin.events = events
        return nil
    }
    
    public func onCancel(withArguments arguments: Any?) -> FlutterError? {
        SwiftTinyFfmpegPlugin.events = nil
        return nil
    }
    
    static var re: FlutterResult? = nil
    var progressChannel: FlutterEventChannel? = nil
    static var events: FlutterEventSink?
    
    // Session 管理
    struct SessionInfo {
        let sessionId: Int64
        var task: DispatchWorkItem?
        var result: FlutterResult?
        var isCancelled: Bool = false
    }
    private var sessions: [Int64: SessionInfo] = [:]
    private let sessionQueue = DispatchQueue(label: "sessionQueue")
    
    convenience init(messenger: FlutterBinaryMessenger) {
        self.init()
        progressChannel = FlutterEventChannel(name: "tiny_ffmpeg_progress_event", binaryMessenger: messenger)
        progressChannel?.setStreamHandler(self)
    }
    
  public static func register(with registrar: FlutterPluginRegistrar) {
    let channel = FlutterMethodChannel(name: "tiny_ffmpeg", binaryMessenger: registrar.messenger())
    let instance = SwiftTinyFfmpegPlugin(messenger: registrar.messenger())
    registrar.addMethodCallDelegate(instance, channel: channel)
  }
    
  public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
      if(call.method == "executeFFmpegCommand"){
          let dict = call.arguments as! NSDictionary
          let argc = dict["argc"] as! Int
          let argv = dict["argv"] as! NSArray
          
          // 创建 Session
          let sessionId = Java_com_i7play_tiny_ffmpeg_FFMpegUtils_createFFmpegSession()
          if sessionId < 0 {
              result(FlutterError(code: "SESSION_ERROR", message: "Failed to create session", details: nil))
              return
          }
          
          var sessionInfo = SessionInfo(sessionId: sessionId, task: nil, result: result, isCancelled: false)
          sessionQueue.sync {
              sessions[sessionId] = sessionInfo
          }
          
          // 返回 sessionId 给 Dart 层
          result(["sessionId": sessionId])
          
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
        
          let task = DispatchWorkItem {
              _ = Java_com_i7play_tiny_ffmpeg_FFMpegUtils_executeFFmpegCommandWithSession(
                  sessionId, Int32(argc), cargs, -1)
          }
          
          sessionQueue.sync {
              sessions[sessionId]?.task = task
          }
          
          DispatchQueue.global(qos: .userInitiated).async(execute: task)
      }else if(call.method == "cancelSession"){
          let sessionId = (call.arguments as? NSDictionary)?["sessionId"] as? Int64 ?? 0
          sessionQueue.sync {
              if var sessionInfo = sessions[sessionId] {
                  sessionInfo.isCancelled = true
                  sessionInfo.task?.cancel()
                  Java_com_i7play_tiny_ffmpeg_FFMpegUtils_cancelFFmpegCommandBySession(sessionId)
                  sessions[sessionId] = sessionInfo
                  result(true)
              } else {
                  result(false)
              }
          }
      }else if(call.method == "getSessionState"){
          let sessionId = (call.arguments as? NSDictionary)?["sessionId"] as? Int64 ?? 0
          sessionQueue.sync {
              if let sessionInfo = sessions[sessionId] {
                  result(["state": sessionInfo.isCancelled ? "cancelled" : "running"])
              } else {
                  result(["state": "not_found"])
              }
          }
      }else if(call.method == "getSessionErrorMessage"){
          let sessionId = (call.arguments as? NSDictionary)?["sessionId"] as? Int64 ?? 0
          if let errorMsg = Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getSessionErrorMessage(sessionId) {
              result(String(cString: errorMsg))
          } else {
              result("")
          }
      }else if(call.method == "getMediaDuration"){
          let mediaPath = call.arguments as! String
          let duration = Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getMediaDuration(mediaPath);
          result(duration)
      }else if(call.method == "showLog"){
          let show = (call.arguments as? Bool) ?? true
          Java_com_i7play_tiny_ffmpeg_FFMpegUtils_setLogEnabled(show ? 1 : 0)
          result(nil)
      }else {
          result(FlutterMethodNotImplemented)
      }
    
  }
    @_silgen_name("Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progress")
     func Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progress(progress: Float) {
        //print("fetch....\(progress)\n")
        var map = Dictionary<String, Any>()
        map["type"] = "progress"
        map["code"] = 0
        map["message"] = progress
        
         if(SwiftTinyFfmpegPlugin.events != nil){
             DispatchQueue.main.async {
                 SwiftTinyFfmpegPlugin.events!(map)
             }
         }
         
    }
    
    @_silgen_name("Java_com_i7play_tiny_ffmpeg_FFMpegUtils_log")
    func Java_com_i7play_tiny_ffmpeg_FFMpegUtils_log(logLevel: Int32, logMessage: UnsafePointer<CChar>?) {
        if let message = logMessage {
            let msg = String(cString: message)
            var map = Dictionary<String, Any>()
            map["type"] = "log"
            map["logLevel"] = Int(logLevel)
            map["logMessage"] = msg
            
            if(SwiftTinyFfmpegPlugin.events != nil){
                DispatchQueue.main.async {
                    SwiftTinyFfmpegPlugin.events!(map)
                }
            }
        }
    }
    
    @_silgen_name("Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progressMessage")
    func Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progressMessage(message: UnsafePointer<CChar>?) {
        if let msg = message {
            let msgStr = String(cString: msg)
            var map = Dictionary<String, Any>()
            map["type"] = "progress"
            map["message"] = msgStr
            
            if(SwiftTinyFfmpegPlugin.events != nil){
                DispatchQueue.main.async {
                    SwiftTinyFfmpegPlugin.events!(map)
                }
            }
        }
    }
    
    @_silgen_name("Java_com_i7play_tiny_ffmpeg_FFMpegUtils_setLogEnabled")
    func Java_com_i7play_tiny_ffmpeg_FFMpegUtils_setLogEnabled(_ enabled: Int32)
    
    @_silgen_name("Java_com_i7play_tiny_ffmpeg_FFMpegUtils_result")
    func Java_com_i7play_tiny_ffmpeg_FFMpegUtils_result(sessionId: Int64, code: Int32, msg: UnsafePointer<CChar>?) {
        guard let msgPtr = msg else { return }
        let msgStr = String(cString: msgPtr)
        var map = Dictionary<String, Any>()
        map["type"] = "result"
        map["sessionId"] = sessionId
        map["code"] = Int(code)
        map["message"] = msgStr
        
        // 通过 EventChannel 发送结果
        DispatchQueue.main.async {
            SwiftTinyFfmpegPlugin.events?(map)
        }
    }
}
