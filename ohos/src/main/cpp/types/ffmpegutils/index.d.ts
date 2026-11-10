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
