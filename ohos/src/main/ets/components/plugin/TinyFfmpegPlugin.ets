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
export default class TinyFfmpegPlugin implements FlutterPlugin, MethodCallHandler, StreamHandler {
  private channel: MethodChannel | null = null;
  private eventChannel: EventChannel | null = null;
  private eventSink: EventSink | null = null;

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
    switch (call.method) {
      case "getPlatformVersion":
        result.success("OpenHarmony");
        break;
      case "executeFFmpegCommand":
        const args = call.argument("argv") as Array<string>;
        const uuid = "ffmpeg_cmd_" + new Date().getTime();
        
        // 绑定回调函数，用于接收 native 层进度的回调
        ffmpegutils.JSBind.bindFunction(uuid + "_onFFmpegProgress", (time: number, size: number, bitrate: number, speed: number) => {
             if (this.eventSink) {
                 const event: Record<string, Object> = {};
                 event["time"] = time;

                 event["size"] = size;
                 event["bitrate"] = bitrate;
                 event["speed"] = speed;
                 this.eventSink.success(event);
             }
        });
        


        // 调用 C++ 端的 executeFFmpegCommandAPP 方法
        // 参数说明：uuid (用于绑定回调), argc (参数个数), argv (参数列表)
        ffmpegutils.executeFFmpegCommandAPP(uuid, args.length, args).then((ret) => {
            const resultMap: Record<string, Object> = {};
            if (ret == 0) {
                resultMap["type"] = "SUCCESS";
                resultMap["code"] = 0;
                resultMap["message"] = "Success";
            } else {
                resultMap["type"] = "ERROR";
                resultMap["code"] = ret;
                resultMap["message"] = "Failed with code " + ret;
            }
            result.success(resultMap);
        });
        break;
      case "showLog":
        const show = call.argument("show") as boolean;
        ffmpegutils.showLog(show);
        result.success(true);
        break;
      case "getMediaDuration":
        // TODO: Implement duration retrieval
        result.success(0);
        break;
      case "cancelExecuteFFmpegCommand":
        ffmpegutils.cancelFFmpegCommand();
        result.success(true);
        break;
      default:
        result.notImplemented();
        break;
    }
  }

  onListen(args: Any, events: EventSink): void {
    this.eventSink = events;
  }

  onCancel(args: Any): void {
    this.eventSink = null;
  }
}