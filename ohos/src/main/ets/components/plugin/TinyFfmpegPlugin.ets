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
    // Engine 退出先请求取消，Session 仍由原生工作线程返回后释放。
    this.eventSink = null;
    this.pendingResults.clear();
    for (const session of this.sessions.values()) {
      session.isCancelled = true;
      ffmpegutils.cancelFFmpegCommandBySession(session.sessionId);
    }
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
        
        // 参数错误应通过通道返回，避免数组边界错误进入原生代码。
        if (!args || args.length == 0 || call.argument("argc") != args.length) {
          result.error("ARGUMENT_ERROR", "argc must match a nonempty argv", null);
          break;
        }
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
        
        // 使用结构化进度，不依赖 showLog 开关或日志文本格式。
        ffmpegutils.JSBind.bindFunction(uuid + "_onFFmpegProgress", (time: number, size: number, speed: number) => {
          if (this.eventSink && this.sessions.has(sessionId)) {
            this.eventSink.success({ type: "progress", sessionId: sessionId, time: time, size: size, speed: speed });
          }
        });

        // 调用 C++ 端的 executeFFmpegCommandWithSession 方法
        ffmpegutils.executeFFmpegCommandWithSession(sessionId, uuid, args.length, args).then((ret) => {
            const session = this.sessions.get(sessionId);
            if (session && !session.isCancelled) {
                // 在销毁 session 之前获取完整的错误日志
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
                resultMap["errorLog"] = errorMsg || "";  // 添加 errorLog 字段，供 Dart 端缓存
                // 通过 EventChannel 发送结果事件（使用当前的 eventSink，而不是保存的）
                if (this.eventSink) {
                    this.eventSink.success(resultMap);
                } else {
                    // 如果 eventSink 还没有设置，缓存结果，等 onListen 时再发送
                    this.pendingResults.set(sessionId, resultMap);
                }
                // MethodChannel 已返回 sessionId，执行结果通过 EventChannel 完成一次通知。
                this.sessions.delete(sessionId);
                ffmpegutils.destroyFFmpegSession(sessionId);
            } else if (session) {
                // 取消后也要等原生线程退出再释放 Session，避免槽位被提前复用。
                this.sessions.delete(sessionId);
                ffmpegutils.destroyFFmpegSession(sessionId);
            }
        }).catch((error: Error) => {
          // Promise 异常也必须完成等待者，不能留下永久运行的 Session。
          const failure: Record<string, Object> = {};
          failure["type"] = "result";
          failure["sessionId"] = sessionId;
          failure["code"] = -1;
          failure["message"] = error.message;
          if (this.eventSink) this.eventSink.success(failure);
          else this.pendingResults.set(sessionId, failure);
          this.sessions.delete(sessionId);
          ffmpegutils.destroyFFmpegSession(sessionId);
        }).finally(() => {
          // 每次命令使用独立回调名，完成后解绑以免长时间使用累积闭包。
          ffmpegutils.releaseFFmpegCallback(uuid + "_onFFmpegProgress");
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
        // 时长查询走轻量接口，兼容纯音频和没有启用视频解码器的文件。
        ffmpegutils.getMediaDuration(call.args as string).then((duration: number) => result.success(duration))
          .catch((error: Error) => result.error("MEDIA_ERROR", error.message, null));
        break;
      case "getMediaInfo":
        // 与 Android/iOS 一样返回毫秒，未知时长保留 -1。
        ffmpegutils.getMediaInfo(call.args as string).then((json: string) => {
          result.success(json);
        }).catch((error: Error) => result.error("MEDIA_ERROR", error.message, null));
        break;
      // 旧插件不具备该协议时，Dart会保持软件路径，不能把自定义命令送给FFmpeg。
      // SDR GPU能力与HDR保留协议独立，避免新Dart误用旧原生。
      case "getOhosSdrGpuVersion":
        result.success(ffmpegutils.getOhosSdrGpuVersion());
        break;
      case "getOhosHdrSurfaceVersion":
        result.success(ffmpegutils.getOhosHdrSurfaceVersion());
        break;
      case "getOhosVideoEncoder":
        // 查询失败只表示不可用，由Dart继续软件编码，不在原生偷偷回退。
        ffmpegutils.getOhosVideoEncoder(call.argument("input") as string,
          call.argument("streamIndex") as number, call.argument("width") as number,
          call.argument("height") as number, call.argument("speed") as number,
          call.argument("bitrate") as number).then((json: string) => result.success(json))
          .catch((error: Error) => result.error("CAPABILITY_ERROR", error.message, null));
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
