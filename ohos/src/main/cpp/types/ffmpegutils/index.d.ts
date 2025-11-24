export const executeFFmpegCommandAPP: (pageName: string, cmdLen: number, argv: Array<string>) => Promise<number>;

export const showLog: (show: boolean) => void;

export const cancelFFmpegCommand: () => void;


export class JSBind {
  static bindFunction: (name: string, func: Function) => number;
}