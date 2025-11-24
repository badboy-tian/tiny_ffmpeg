/// Helper class to build FFmpeg commands.
class TinyFFmpegCMD {
  final List<String> _cmds = [];

  /// Creates a new command builder.
  ///
  /// [overwrite] If true, adds the '-y' flag to overwrite output files without asking. Default is true.
  TinyFFmpegCMD({bool overwrite = true}) {
    _cmds.add("ffmpeg");
    if (overwrite) {
      _cmds.add("-y");
    }
  }

  /// Adds a command argument.
  void add(String cmd) {
    _cmds.add(cmd);
  }

  /// Adds an input file path argument (-i path).
  void addInput(String path) {
    _cmds.add("-i");
    _cmds.add(path);
  }

  /// Adds an output file path argument (path).
  void addOutput(String path) {
    _cmds.add(path);
  }

  /// Builds the list of command arguments.
  List<String> build() {
    return _cmds;
  }

  @override
  String toString() {
    return 'TinyFFmpegCMD{cmds: $_cmds}';
  }
}