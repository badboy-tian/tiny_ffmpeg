import 'package:tiny_ffmpeg/tiny_ffmpeg_cmd.dart';

/// 原生使用绝对轨道索引，完整保留音轨列表供容器兼容性判断。
class TinyFfmpegAudioStream {
  final int index;
  final String codec;

  TinyFfmpegAudioStream.fromMap(Map<dynamic, dynamic> map)
      : index = (map['index'] as num?)?.toInt() ?? -1,
        codec = map['codec'] as String? ?? 'unknown';
}

/// Source properties used to preserve bit depth and HDR signalling.
class TinyFfmpegMediaInfo {
  final int durationMilliseconds;
  final bool hasVideo;
  final int videoStreamIndex;
  final bool hasAudio;
  // 老版本原生缺少字段时保持空列表，应用必须采取保守转码策略。
  final List<TinyFfmpegAudioStream> audioStreams;
  final int width;
  final int height;
  final int bitDepth;
  final int rotation;
  final String pixelFormat;
  final String colorPrimaries;
  final String colorTransfer;
  final String colorSpace;
  final String colorRange;
  final bool hdrVivid;
  final bool hdr10Plus;
  final bool dolbyVision;
  // 双层 Dolby Vision 不能当作单层 HEVC 重编码，探测配置后保留明确的能力边界。
  final int dolbyVisionProfile;
  final int dolbyVisionCompatibilityId;
  final bool dolbyVisionEnhancementLayer;

  TinyFfmpegMediaInfo.fromMap(Map<String, dynamic> map)
      : durationMilliseconds =
            (map['durationMilliseconds'] as num?)?.toInt() ?? -1,
        hasVideo = map['hasVideo'] == true,
        // 记录绝对流索引，跳过封面图片后仍裁剪正确的视频流。
        videoStreamIndex = (map['videoStreamIndex'] as num?)?.toInt() ?? -1,
        hasAudio = map['hasAudio'] == true,
        audioStreams = List.unmodifiable(
          ((map['audioStreams'] as List?) ?? const [])
              .map((stream) => TinyFfmpegAudioStream.fromMap(stream as Map)),
        ),
        width = (map['width'] as num?)?.toInt() ?? 0,
        height = (map['height'] as num?)?.toInt() ?? 0,
        bitDepth = (map['bitDepth'] as num?)?.toInt() ?? 0,
        rotation = (map['rotation'] as num?)?.round() ?? 0,
        pixelFormat = map['pixelFormat'] as String? ?? 'unknown',
        colorPrimaries = map['colorPrimaries'] as String? ?? 'unknown',
        colorTransfer = map['colorTransfer'] as String? ?? 'unknown',
        colorSpace = map['colorSpace'] as String? ?? 'unknown',
        colorRange = map['colorRange'] as String? ?? 'unknown',
        hdrVivid = map['hdrVivid'] == true,
        hdr10Plus = map['hdr10Plus'] == true,
        dolbyVision = map['dolbyVision'] == true,
        dolbyVisionProfile = (map['dolbyVisionProfile'] as num?)?.toInt() ?? 0,
        dolbyVisionCompatibilityId =
            (map['dolbyVisionCompatibilityId'] as num?)?.toInt() ?? 0,
        dolbyVisionEnhancementLayer =
            map['dolbyVisionEnhancementLayer'] == true;

  bool get isHdr =>
      hdrVivid ||
      hdr10Plus ||
      dolbyVision ||
      colorTransfer == 'arib-std-b67' ||
      colorTransfer == 'smpte2084';

  // 硬件解码常报告半平面存储；编码器使用等价平面格式，只改变内存布局而不改变位深/色彩。
  String get encodingPixelFormat =>
      const {
        'nv12': 'yuv420p',
        'nv21': 'yuv420p',
        'p010le': 'yuv420p10le',
      }[pixelFormat] ??
      pixelFormat;

  // 只直拷已确认的 AAC 音轨；缺少探测字段或混入其它编码时由调用方转 AAC。
  bool get canCopyAudioToMp4 =>
      hasAudio &&
      audioStreams.isNotEmpty &&
      audioStreams
          .every((stream) => stream.index >= 0 && stream.codec == 'aac');

  int get displayWidth => rotation.abs() % 180 == 90 ? height : width;
  int get displayHeight => rotation.abs() % 180 == 90 ? width : height;

  /// Builds an MP4 export command. Lossless preserves decoded pixels at the
  /// cost of larger files. Dynamic metadata detection inspects the first frame.
  TinyFFmpegCMD exportCommand({
    required String input,
    required String output,
    Duration start = Duration.zero,
    Duration? duration,
    TinyFfmpegCrop? crop,
    bool lossless = true,
    int crf = 18,
    String preset = 'ultrafast',
    int threads = 2,
  }) {
    // 双层内容需要同时处理增强层，单层编码不能静默丢弃增强层画面。
    if (!hasVideo) throw UnsupportedError('The input has no video stream');
    if (dolbyVision &&
        (dolbyVisionEnhancementLayer ||
            (dolbyVisionProfile != 0 &&
                dolbyVisionProfile != 5 &&
                dolbyVisionProfile != 8))) {
      throw UnsupportedError(
          'Dolby Vision profile $dolbyVisionProfile requires an unsupported enhancement-layer or codec path');
    }
    const formats = [
      'yuv420p',
      'yuv422p',
      'yuv444p',
      'yuv420p10le',
      'yuv422p10le',
      'yuv444p10le',
    ];
    if (!formats.contains(encodingPixelFormat) ||
        (bitDepth != 8 && bitDepth != 10) ||
        (isHdr && bitDepth != 10)) {
      throw UnsupportedError(
          'Unsupported source pixel format $pixelFormat/$bitDepth-bit');
    }
    if (input.isEmpty ||
        output.isEmpty ||
        input == output ||
        !output.toLowerCase().endsWith('.mp4')) {
      throw ArgumentError(
          'Use distinct input and output paths and an MP4 output');
    }
    if (start.isNegative ||
        (duration != null && duration <= Duration.zero) ||
        (durationMilliseconds >= 0 &&
            start.inMilliseconds >= durationMilliseconds)) {
      throw ArgumentError('Invalid trim range');
    }
    if (crf < 0 ||
        crf > 51 ||
        threads < 1 ||
        threads > 16 ||
        !const [
          'ultrafast',
          'superfast',
          'veryfast',
          'faster',
          'fast',
          'medium',
          'slow',
          'slower',
          'veryslow'
        ].contains(preset)) {
      throw ArgumentError('Invalid encoder settings');
    }
    crop?.validate(displayWidth, displayHeight);
    final hevc = bitDepth == 10;
    final args = <String>[
      '-threads',
      '$threads',
      '-ss',
      (start.inMicroseconds / 1000000).toStringAsFixed(6),
      '-i',
      input,
      if (duration != null) ...[
        '-t',
        (duration.inMicroseconds / 1000000).toStringAsFixed(6),
      ],
      '-map',
      videoStreamIndex >= 0 ? '0:$videoStreamIndex' : '0:v:0',
      '-map',
      '0:a?',
      if (crop != null) ...[
        '-vf',
        'crop=${crop.width}:${crop.height}:${crop.x}:${crop.y}'
      ],
      '-c:v',
      hevc ? 'libx265' : 'libx264',
      '-preset',
      preset,
      '-pix_fmt',
      encodingPixelFormat,
      '-fps_mode',
      'passthrough',
    ];
    // 仅传递源文件已声明的色彩信息，保留未标记 SDR 的原有语义。
    for (final entry in {
      '-color_primaries': colorPrimaries,
      '-color_trc': colorTransfer,
      '-colorspace': colorSpace,
      '-color_range': colorRange,
    }.entries) {
      if (!const ['unknown', 'unspecified', 'reserved'].contains(entry.value)) {
        args.addAll([entry.key, entry.value]);
      }
    }
    if (hevc) {
      // x265 的 Dolby Vision 初始化要求 VBV，RPU 由 FFmpeg 按帧编码并写入 MP4 配置。
      args.addAll([
        '-tag:v',
        'hvc1',
        if (dolbyVision) ...['-dolbyvision', '1', '-strict', 'unofficial'],
        '-x265-params',
        '${lossless ? 'lossless=1:' : ''}pools=$threads:frame-threads=$threads'
            '${dolbyVision ? ':vbv-maxrate=100000:vbv-bufsize=100000' : ''}',
      ]);
    }
    args.addAll([
      '-crf',
      lossless && !hevc ? '0' : '$crf',
      '-c:a',
      canCopyAudioToMp4 ? 'copy' : 'aac',
      if (!canCopyAudioToMp4) ...['-b:a', '192k'],
      '-movflags',
      '+faststart',
      output,
    ]);
    final command = TinyFFmpegCMD();
    args.forEach(command.add);
    return command;
  }
}

/// Crop coordinates are measured after the input's display rotation.
class TinyFfmpegCrop {
  final int width;
  final int height;
  final int x;
  final int y;

  const TinyFfmpegCrop({
    required this.width,
    required this.height,
    this.x = 0,
    this.y = 0,
  });

  void validate(int sourceWidth, int sourceHeight) {
    // 偶数坐标兼容 4:2:0 色度采样，避免 crop 自动取整改变调用方指定区域。
    if (width <= 0 ||
        height <= 0 ||
        x < 0 ||
        y < 0 ||
        [width, height, x, y].any((value) => value.isOdd) ||
        x + width > sourceWidth ||
        y + height > sourceHeight) {
      throw ArgumentError('Crop must be even and inside the displayed video');
    }
  }
}
