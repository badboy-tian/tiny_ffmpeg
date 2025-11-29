import {
  EventChannel,
  EventSink,
  FlutterPlugin,
  FlutterPluginBinding,
  MethodCall,
  MethodCallHandler,
  MethodChannel,
  MethodResult,
  StreamHandler,
  Any,
} from '@ohos/flutter_ohos';
import ffmpegutils from 'libffmpegutils.so';


/** TinyFfmpegPlugin **/
interface SessionInfo {
  sessionId: number;
  result: MethodResult | null;
  eventSink: EventSink | null;
  isCancelled: boolean;
}

export default class TinyFfmpegPlugin implements FlutterPlugin, MethodCallHandler, StreamHandler {
  private channel: MethodChannel | null = null;
  private eventChannel: EventChannel | null = null;
  private eventSink: EventSink | null = null;
  private sessions: Map<number, SessionInfo> = new Map();
  private pendingResults: Map<number, Record<string, Object>> = new Map(); // 缓存待发送的结果

  constructor() {
  }

  getUniqueClassName(): string {
    return "TinyFfmpegPlugin"
  }

  onAttachedToEngine(binding: FlutterPluginBinding): void {
    this.channel = new MethodChannel(binding.getBinaryMessenger(), "tiny_ffmpeg");
    this.channel.setMethodCallHandler(this);

    this.eventChannel = new EventChannel(binding.getBinaryMessenger(), "tiny_ffmpeg_progress_event");
    this.eventChannel.setStreamHandler(this);
  }

  onDetachedFromEngine(binding: FlutterPluginBinding): void {
    if (this.channel != null) {
      this.channel.setMethodCallHandler(null);
      this.channel = null;
    }
    if (this.eventChannel != null) {
      this.eventChannel.setStreamHandler(null);
      this.eventChannel = null;
    }
  }

  onMethodCall(call: MethodCall, result: MethodResult): void {
  //console.info("MethodCall arguments:", call.arguments);
    switch (call.method) {
      case "executeFFmpegCommand":
        const args = call.argument("argv") as Array<string>;
        
        // 创建 Session
        const sessionId = ffmpegutils.createFFmpegSession() as number;
        if (sessionId < 0) {
          result.error("SESSION_ERROR", "Failed to create session", null);
          break;
        }
        
        const sessionInfo: SessionInfo = {
          sessionId: sessionId,
          result: result,
          eventSink: null, // eventSink 会在 onListen 时设置，这里不需要保存
          isCancelled: false
        };
        this.sessions.set(sessionId, sessionInfo);
        
        // 返回 sessionId 给 Dart 层
        result.success({ sessionId: sessionId });
        
        const uuid = "ffmpeg_cmd_" + sessionId + "_" + new Date().getTime();
        
        // 绑定回调函数，用于接收 native 层进度的回调（保留用于兼容，但不发送事件）
        ffmpegutils.JSBind.bindFunction(uuid + "_onFFmpegProgress", (time: number, size: number, bitrate: number, speed: number) => {
             // 不再发送 progress 事件
        });

        // 调用 C++ 端的 executeFFmpegCommandWithSession 方法
        ffmpegutils.executeFFmpegCommandWithSession(sessionId, uuid, args.length, args).then((ret) => {
            const session = this.sessions.get(sessionId);
            if (session && !session.isCancelled) {
                const errorMsg = ffmpegutils.getSessionErrorMessage(sessionId) as string;
            const resultMap: Record<string, Object> = {};
            if (ret == 0) {
                    resultMap["type"] = "result";
                resultMap["code"] = 0;
                    resultMap["message"] = "success";
            } else {
                    resultMap["type"] = "result";
                resultMap["code"] = ret;
                    if (errorMsg && errorMsg.length > 0) {
                        resultMap["message"] = `Error code: ${ret}\n\nDetailed error log:\n${errorMsg}`;
                    } else {
                        resultMap["message"] = `Failed with code ${ret}`;
                    }
                }
                resultMap["sessionId"] = sessionId;
                // 通过 EventChannel 发送结果事件（使用当前的 eventSink，而不是保存的）
                if (this.eventSink) {
                    this.eventSink.success(resultMap);
                } else {
                    // 如果 eventSink 还没有设置，缓存结果，等 onListen 时再发送
                    this.pendingResults.set(sessionId, resultMap);
                }
                if (session.result) {
                    session.result.success(resultMap);
                }
                this.sessions.delete(sessionId);
                ffmpegutils.destroyFFmpegSession(sessionId);
            }
        });
        break;
      case "cancelSession":
        const cancelSessionId = (call.argument("sessionId") as number) ?? 0;
        const cancelSession = this.sessions.get(cancelSessionId);
        if (cancelSession) {
          cancelSession.isCancelled = true;
          ffmpegutils.cancelFFmpegCommandBySession(cancelSessionId);
          result.success(true);
        } else {
          result.success(false);
        }
        break;
      case "getSessionState":
        const stateSessionId = (call.argument("sessionId") as number) ?? 0;
        const stateSession = this.sessions.get(stateSessionId);
        if (stateSession) {
          result.success({ state: stateSession.isCancelled ? "cancelled" : "running" });
        } else {
          result.success({ state: "not_found" });
        }
        break;
      case "getSessionErrorMessage":
        const errorSessionId = (call.argument("sessionId") as number) ?? 0;
        const errorMsg = ffmpegutils.getSessionErrorMessage(errorSessionId) as string;
        result.success(errorMsg || "");
        break;
      case "getMediaDuration":
        result.success(0);
        break;
      case "showLog":
        const show = (call.argument("show") as boolean) ?? true;
        ffmpegutils.setLogEnabled(show);
        result.success(null);
        break;
      default:
        result.notImplemented();
        break;
    }
  }

  onListen(args: Any, events: EventSink): void {
    this.eventSink = events;
    // 发送所有待发送的结果事件
    for (const entry of this.pendingResults.entries()) {
      const sessionId = entry[0];
      const resultMap = entry[1];
      events.success(resultMap);
    }
    this.pendingResults.clear();
  }

  onCancel(args: Any): void {
    this.eventSink = null;
  }
}