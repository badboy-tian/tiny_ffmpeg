export const executeFFmpegCommandWithSession: (sessionId: number, pageName: string, cmdLen: number, argv: Array<string>) => Promise<number>;

export const createFFmpegSession: () => number;

export const cancelFFmpegCommandBySession: (sessionId: number) => number;

export const getSessionErrorMessage: (sessionId: number) => string;

export const destroyFFmpegSession: (sessionId: number) => void;

export const setLogEnabled: (enabled: boolean) => void;

export class JSBind {
  static bindFunction: (name: string, func: Function) => number;
}
// 探测在工作线程执行，避免文件解析阻塞 ArkTS 主线程。
export const getMediaInfo: (path: string) => Promise<string>;

// 完成后在 ArkTS 线程释放 AKI 回调引用。
export const releaseFFmpegCallback: (name: string) => void;

// 毫秒时长查询无需解码视频帧。
export const getMediaDuration: (path: string) => Promise<number>;

// 返回经过本机尺寸、帧率、NV12和码率校验的硬编方案；不支持时返回空JSON。
export const getOhosVideoEncoder: (path: string, index: number, width: number, height: number, speed: number, bitrate: number) => Promise<string>;

// 协议版本用于识别支持Session控制的HDR Surface实现。
export const getOhosHdrSurfaceVersion: () => number;

// GPU映射协议独立门控，便于应用兼容尚未升级的原生插件。
export const getOhosSdrGpuVersion: () => number;
