export const executeFFmpegCommandWithSession: (sessionId: number, pageName: string, cmdLen: number, argv: Array<string>) => Promise<number>;

export const createFFmpegSession: () => number;

export const cancelFFmpegCommandBySession: (sessionId: number) => number;

export const getSessionErrorMessage: (sessionId: number) => string;

export const destroyFFmpegSession: (sessionId: number) => void;

export const setLogEnabled: (enabled: boolean) => void;

export class JSBind {
  static bindFunction: (name: string, func: Function) => number;
}