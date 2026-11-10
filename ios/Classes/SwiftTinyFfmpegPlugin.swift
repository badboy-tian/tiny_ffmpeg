import Flutter
import UIKit
//import CMDUtil

//extern int Java_com_i7play_tiny_1ffmpeg_FFMpegUtils_executeFFmpegCommand(int cmdLen, char* argv[], long totalTime);
public class SwiftTinyFfmpegPlugin: NSObject, FlutterPlugin, FlutterStreamHandler {
    public func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
        self.events = events
        // 极短命令可能先于 EventChannel 完成，监听建立后补发结果。
        for event in self.pendingResults { events(event) }
        self.pendingResults.removeAll()
        return nil
    }
    
    public func onCancel(withArguments arguments: Any?) -> FlutterError? {
        self.events = nil
        return nil
    }
    
    static var re: FlutterResult? = nil
    var progressChannel: FlutterEventChannel? = nil
    private var events: FlutterEventSink?
    private var pendingResults: [[String: Any]] = []
    // 结果按 Session 路由到所属 Engine，避免多个 Flutter Engine 覆盖静态事件回调。
    private static var owners: [Int64: SwiftTinyFfmpegPlugin] = [:]
    // 前端含全局选项，同一插件的命令按提交顺序执行。
    private let executionQueue = DispatchQueue(label: "tiny_ffmpeg.execution", qos: .userInitiated)
    
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
          // 平台参数必须校验，业务输入错误不能触发 Swift 强制解包崩溃。
          guard let dict = call.arguments as? [String: Any],
                let argc = dict["argc"] as? Int,
                let argv = dict["argv"] as? [String],
                argc > 0, argc == argv.count else {
              result(FlutterError(code: "ARGUMENT_ERROR", message: "argc must match a nonempty argv", details: nil))
              return
          }
          
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
          Self.owners[sessionId] = self
          result(["sessionId": sessionId])
          
          // FFmpeg 解析可选参数时会读取 argv[argc]，额外保留空指针哨兵防止越界。
          let cargs = UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>.allocate(capacity: argc + 1)
          cargs[argc] = nil
          
          // C 层逐项使用 free，字符串必须由 strdup 配套分配。
          for (index, value) in argv.enumerated() {
              cargs[index] = strdup(value)
              if cargs[index] == nil {
                  for previous in 0..<index { free(cargs[previous]) }
                  cargs.deallocate()
                  Java_com_i7play_tiny_ffmpeg_FFMpegUtils_destroyFFmpegSession(sessionId)
                  sessionQueue.sync { sessions.removeValue(forKey: sessionId) }
                  Self.sendEvent(["type": "result", "sessionId": sessionId, "code": -1, "message": "Out of memory"])
                  return
              }
          }

          let task = DispatchWorkItem { [weak self] in
              _ = Java_com_i7play_tiny_ffmpeg_FFMpegUtils_executeFFmpegCommandWithSession(
                  sessionId, Int32(argc), cargs, -1)
              
              cargs.deallocate()
              Java_com_i7play_tiny_ffmpeg_FFMpegUtils_destroyFFmpegSession(sessionId)
              
              self?.sessionQueue.sync {
                  self?.sessions.removeValue(forKey: sessionId)
              }
          }
          
          sessionQueue.sync {
              sessions[sessionId]?.task = task
          }
          
          executionQueue.async(execute: task)
      }else if(call.method == "cancelSession"){
          let sessionId = (call.arguments as? NSDictionary)?["sessionId"] as? Int64 ?? 0
          sessionQueue.sync {
              if var sessionInfo = sessions[sessionId] {
                  sessionInfo.isCancelled = true
                  // sessionInfo.task?.cancel()
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
          guard let path = call.arguments as? String else {
              result(FlutterError(code: "ARGUMENT_ERROR", message: "A media path is required", details: nil))
              return
          }
          // 文件探测在工作队列运行，结果在 Flutter 主线程返回。
          executionQueue.async {
              let duration = Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getMediaDuration(path)
              DispatchQueue.main.async { result(duration) }
          }
      }else if(call.method == "getMediaInfo"){
          guard let path = call.arguments as? String else {
              result(FlutterError(code: "ARGUMENT_ERROR", message: "A media path is required", details: nil))
              return
          }
          executionQueue.async {
              let json = String(cString: Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getMediaInfo(path))
              DispatchQueue.main.async { result(json) }
          }
      }else if(call.method == "showLog"){
          // Dart 传递带 show 键的字典，与 Android/鸿蒙保持同一协议。
          let show = (call.arguments as? [String: Any])?["show"] as? Bool ?? true
          Java_com_i7play_tiny_ffmpeg_FFMpegUtils_setLogEnabled(show ? 1 : 0)
          result(nil)
      }else {
          result(FlutterMethodNotImplemented)
      }
    
  }
    fileprivate static func sendEvent(_ event: [String: Any]) {
        DispatchQueue.main.async {
            guard let id = event["sessionId"] as? Int64, let owner = owners[id] else { return }
            if let sink = owner.events { sink(event) }
            else if event["type"] as? String == "result", owner.pendingResults.count < 64 {
                owner.pendingResults.append(event)
            }
            if event["type"] as? String == "result" { owners.removeValue(forKey: id) }
        }
    }

    public func detachFromEngine(for registrar: FlutterPluginRegistrar) {
        // 先停止事件投递，再请求取消，执行闭包负责最终释放原生资源。
        events = nil
        pendingResults.removeAll()
        sessionQueue.sync {
            for id in sessions.keys {
                Java_com_i7play_tiny_ffmpeg_FFMpegUtils_cancelFFmpegCommandBySession(id)
            }
        }
    }
}

// C 前端没有 Swift 实例，回调必须使用显式 C ABI 的顶层函数，避免隐式 self。
@_cdecl("Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progress")
public func tinyFfmpegNativeLegacyProgress(_ progress: Float) {
    SwiftTinyFfmpegPlugin.sendEvent([
        "type": "progress", "sessionId": get_ffmpeg_current_session_id(),
        "code": 0, "message": progress
    ])
}

@_cdecl("Java_com_i7play_tiny_ffmpeg_FFMpegUtils_log")
public func tinyFfmpegNativeLog(_ level: Int32, _ message: UnsafePointer<CChar>?) {
    guard let message = message else { return }
    SwiftTinyFfmpegPlugin.sendEvent([
        "type": "log", "sessionId": get_ffmpeg_current_session_id(),
        "logLevel": Int(level), "logMessage": String(cString: message)
    ])
}

@_cdecl("Java_com_i7play_tiny_ffmpeg_FFMpegUtils_progressMessage")
public func tinyFfmpegNativeProgressMessage(_ message: UnsafePointer<CChar>?) {
    guard let message = message else { return }
    SwiftTinyFfmpegPlugin.sendEvent([
        "type": "progress", "sessionId": get_ffmpeg_current_session_id(),
        "message": String(cString: message)
    ])
}

@_cdecl("Java_com_i7play_tiny_ffmpeg_FFMpegUtils_result")
public func tinyFfmpegNativeResult(_ sessionId: Int64, _ code: Int32, _ message: UnsafePointer<CChar>?) {
    // 原生返回后会销毁 Session，字符串必须在当前线程立即复制。
    let error = Java_com_i7play_tiny_ffmpeg_FFMpegUtils_getSessionErrorMessage(sessionId)
    SwiftTinyFfmpegPlugin.sendEvent([
        "type": "result", "sessionId": sessionId, "code": Int(code),
        "message": message.map { String(cString: $0) } ?? "",
        "errorLog": error.map { String(cString: $0) } ?? ""
    ])
}

// 原生输出时间使用微秒，Dart 的结构化进度统一使用毫秒。
@_cdecl("tiny_ffmpeg_report_progress")
public func tinyFfmpegNativeProgress(_ sessionId: Int64, _ time: Int64, _ size: Int64, _ speed: Double) {
    SwiftTinyFfmpegPlugin.sendEvent([
        "type": "progress", "sessionId": sessionId,
        "time": time / 1000, "size": size, "speed": speed
    ])
}
