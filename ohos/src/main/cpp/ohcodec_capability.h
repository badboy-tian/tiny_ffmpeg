#include <algorithm>
#include <cmath>
#include <sstream>
#include <multimedia/player_framework/native_avcapability.h>

// 只查询本机硬件与输入的实际帧率；不能把测试机的100Mbps/4K60当成所有手机的能力。
static std::string getOhosVideoEncoder(std::string path, int index,
                                      int width, int height, double speed,
                                      int64_t bitrate) {
    if (width <= 0 || height <= 0 || width % 2 || height % 2 ||
        !std::isfinite(speed) || speed <= 0 || bitrate <= 0)
        return "{}";
    auto cap = OH_AVCodec_GetCapabilityByCategory("video/avc", true, HARDWARE);
    if (!cap || !avcodec_find_encoder_by_name("h264_ohcodec")) return "{}";
    OH_AVRange range{};
    if (OH_AVCapability_GetEncoderBitrateRange(cap, &range) != AV_ERR_OK ||
        bitrate < range.minVal || range.maxVal <= 0 ||
        !OH_AVCapability_IsEncoderBitrateModeSupported(cap, BITRATE_MODE_VBR)) return "{}";
    // 源预算高于设备上限时取设备上限，不能因大码率原片完全失去硬编机会。
    bitrate = std::min(bitrate, static_cast<int64_t>(range.maxVal));
    const int32_t *formats = nullptr;
    uint32_t count = 0;
    bool nv12 = false;
    if (OH_AVCapability_GetVideoSupportedPixelFormats(cap, &formats, &count) != AV_ERR_OK)
        return "{}";
    for (uint32_t i = 0; formats && i < count; ++i)
        nv12 = nv12 || formats[i] == AV_PIXEL_FORMAT_NV12;
    if (!nv12) return "{}";
    AVFormatContext *input = nullptr;
    if (avformat_open_input(&input, path.c_str(), nullptr, nullptr) < 0) return "{}";
    double fps = 0;
    const char *decoder = "";
    if (avformat_find_stream_info(input, nullptr) >= 0 && index >= 0 &&
        index < static_cast<int>(input->nb_streams) &&
        input->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        auto stream = input->streams[index];
        // VFR保守取标称/平均帧率的较高者；快放提高送帧速度，慢放不降低能力要求。
        double guessed = av_q2d(av_guess_frame_rate(input, stream, nullptr));
        double average = av_q2d(stream->avg_frame_rate);
        fps = std::max(std::isfinite(guessed) ? guessed : 0.0,
                       std::isfinite(average) ? average : 0.0);
        // 只有系统确实有对应硬解时才建议硬解，实际失败仍整条命令回退软件。
        if (stream->codecpar->codec_id == AV_CODEC_ID_H264 &&
            OH_AVCodec_GetCapabilityByCategory("video/avc", false, HARDWARE) &&
            avcodec_find_decoder_by_name("h264_ohcodec")) decoder = "h264_ohcodec";
        if (stream->codecpar->codec_id == AV_CODEC_ID_HEVC &&
            OH_AVCodec_GetCapabilityByCategory("video/hevc", false, HARDWARE) &&
            avcodec_find_decoder_by_name("hevc_ohcodec")) decoder = "hevc_ohcodec";
    }
    avformat_close_input(&input);
    fps *= std::max(1.0, speed);
    if (!std::isfinite(fps) || fps <= 0 ||
        !OH_AVCapability_AreVideoSizeAndFrameRateSupported(cap, width, height, fps))
        return "{}";
    // 源平均码率可能极高，不能让硬编盲目填满设备上限；以高质量像素预算约束目标。
    // 这是码率策略而非画质等价保证，仍需同素材的画面与体积回归。
    bitrate = std::min(bitrate, static_cast<int64_t>(std::max(10000.0, width * 1.0 * height * fps * 0.5)));
    if (bitrate < range.minVal) return "{}";
    std::ostringstream result;
    // 返回固定协议字段，不把设备名称或输入路径拼进JSON，避免转义与隐私问题。
    result << "{\"encoder\":\"h264_ohcodec\",\"bitrate\":" << bitrate
           << ",\"frameRate\":" << fps << ",\"decoder\":\"" << decoder << "\"}";
    return result.str();
}
