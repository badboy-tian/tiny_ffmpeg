class TinyFFmpegCMD{
  List<String> cmds = [];

  TinyFFmpegCMD(){
    cmds.add("ffmpeg");
    cmds.add("-y");
  }

  void add(String cmd){
    cmds.add(cmd);
  }

  List<String> build(){
    return cmds;
  }
}