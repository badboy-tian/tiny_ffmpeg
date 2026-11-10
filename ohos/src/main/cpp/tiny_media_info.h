#ifndef TINY_MEDIA_INFO_H
#define TINY_MEDIA_INFO_H

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/display.h"
#include "libavutil/dovi_meta.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"

// 三端共用同一探测实现，微秒时长统一转毫秒，未知值保持为 -1。
static int tiny_probe_interrupt(void *opaque)
{
    return av_gettime_relative() > *(int64_t *)opaque;
}

// 时长探测独立于视频解码，音频和尚未启用解码器的格式也能查询时长。
static int64_t tiny_format_duration(AVFormatContext *format) {
    int64_t duration = format->duration == AV_NOPTS_VALUE ? -1 : format->duration / 1000;
    for (unsigned i = 0; i < format->nb_streams; i++) {
        AVStream *stream = format->streams[i];
        if (stream->duration != AV_NOPTS_VALUE) {
            int64_t value = av_rescale_q(stream->duration, stream->time_base, (AVRational){1, 1000});
            if (value > duration) duration = value;
        }
    }
    return duration;
}

static int64_t tiny_media_duration(const char *path) {
    if (!path || !*path) return -1;
    AVFormatContext *format = avformat_alloc_context();
    if (!format) return -1;
    int64_t deadline = av_gettime_relative() + 10000000;
    format->interrupt_callback.callback = tiny_probe_interrupt;
    format->interrupt_callback.opaque = &deadline;
    int ret = avformat_open_input(&format, path, NULL, NULL);
    if (ret >= 0) ret = avformat_find_stream_info(format, NULL);
    int64_t duration = ret < 0 ? -1 : tiny_format_duration(format);
    avformat_close_input(&format);
    return duration;
}

static int tiny_media_info(const char *path, char *json, size_t capacity)
{
    AVFormatContext *format = avformat_alloc_context();
    AVCodecContext *decoder = NULL;
    AVFrame *frame = NULL;
    AVPacket *packet = NULL;
    int64_t deadline = av_gettime_relative() + 10000000;
    int ret = AVERROR(EINVAL), video = -1, audio = 0, depth = 0;
    int vivid = 0, hdr_plus = 0, dovi = 0, got_frame = 0;
    // 保留 Dolby Vision 配置，避免统一入口将双层内容误当成单层视频。
    int dovi_profile = 0, dovi_compatibility = 0, dovi_enhancement = 0;
    // 记录全部音轨，不能只看第一条 AAC 就错误直拷其它不兼容音轨。
    char audio_streams[768] = "[";
    size_t audio_length = 1;
    int64_t duration = -1;
    double rotation = 0;
    AVCodecParameters *parameters = NULL;
    const AVPixFmtDescriptor *descriptor = NULL;
    const AVPacketSideData *side = NULL;
    const char *pixel = NULL, *primaries = NULL, *transfer = NULL, *space = NULL, *range = NULL;
    if (!format) { ret = AVERROR(ENOMEM); goto done; }
    format->interrupt_callback.callback = tiny_probe_interrupt;
    format->interrupt_callback.opaque = &deadline;
    if (!path || !*path) goto done;
    ret = avformat_open_input(&format, path, NULL, NULL);
    if (ret < 0) goto done;
    ret = avformat_find_stream_info(format, NULL);
    if (ret < 0) goto done;
    duration = tiny_format_duration(format);
    for (unsigned i = 0; i < format->nb_streams; i++) {
        AVStream *stream = format->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video < 0 &&
            !(stream->disposition & AV_DISPOSITION_ATTACHED_PIC)) video = (int)i;
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            const char *codec = avcodec_get_name(stream->codecpar->codec_id);
            int written = snprintf(audio_streams + audio_length, sizeof(audio_streams) - audio_length,
                "%s{\"index\":%u,\"codec\":\"%s\"}", audio ? "," : "", i, codec);
            // 超多音轨时明确失败，不能截断列表后把未知音轨误判为可直拷。
            if (written < 0 || (size_t)written >= sizeof(audio_streams) - audio_length) {
                ret = AVERROR(ENOSPC); goto done;
            }
            audio_length += (size_t)written;
            audio = 1;
        }
    }
    if (audio_length + 2 > sizeof(audio_streams)) { ret = AVERROR(ENOSPC); goto done; }
    audio_streams[audio_length++] = ']';
    audio_streams[audio_length] = '\0';
    if (video >= 0) {
        parameters = format->streams[video]->codecpar;
        side = av_packet_side_data_get(parameters->coded_side_data, parameters->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF);
        dovi = side != NULL;
        if (side && side->size >= sizeof(AVDOVIDecoderConfigurationRecord)) {
            // 鸿蒙以 C++ 编译此共享头文件，需要显式转换 FFmpeg 的字节缓冲。
            const AVDOVIDecoderConfigurationRecord *configuration =
                (const AVDOVIDecoderConfigurationRecord *)side->data;
            dovi_profile = configuration->dv_profile;
            dovi_compatibility = configuration->dv_bl_signal_compatibility_id;
            dovi_enhancement = configuration->el_present_flag;
        }
        side = av_packet_side_data_get(parameters->coded_side_data, parameters->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
        if (side && side->size >= 9 * sizeof(int32_t)) rotation = av_display_rotation_get((const int32_t *)side->data);
        if (!isfinite(rotation)) rotation = 0;
        descriptor = av_pix_fmt_desc_get((enum AVPixelFormat)parameters->format);
        depth = descriptor ? descriptor->comp[0].depth : parameters->bits_per_raw_sample;
        pixel = av_get_pix_fmt_name((enum AVPixelFormat)parameters->format);
        primaries = av_color_primaries_name(parameters->color_primaries);
        transfer = av_color_transfer_name(parameters->color_trc);
        space = av_color_space_name(parameters->color_space);
        range = av_color_range_name(parameters->color_range);
        // 动态 HDR 类型从首个解码帧识别，不能只靠 HLG/PQ 标签猜测 Vivid。
        decoder = avcodec_alloc_context3(avcodec_find_decoder(parameters->codec_id));
        frame = av_frame_alloc();
        packet = av_packet_alloc();
        if (!decoder || !frame || !packet) { ret = AVERROR(ENOMEM); goto done; }
        ret = avcodec_parameters_to_context(decoder, parameters);
        if (ret < 0) goto done;
        decoder->thread_count = 1;
        ret = avcodec_open2(decoder, avcodec_find_decoder(parameters->codec_id), NULL);
        if (ret < 0) goto done;
        for (int count = 0; count < 256 && !got_frame; count++) {
            ret = av_read_frame(format, packet);
            if (ret < 0) {
                if (ret != AVERROR_EOF) goto done;
                ret = avcodec_send_packet(decoder, NULL);
            } else if (packet->stream_index == video) {
                ret = avcodec_send_packet(decoder, packet);
            } else { av_packet_unref(packet); continue; }
            av_packet_unref(packet);
            if (ret < 0 && ret != AVERROR(EAGAIN)) goto done;
            ret = avcodec_receive_frame(decoder, frame);
            if (ret == 0) got_frame = 1;
            else if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) goto done;
            if (tiny_probe_interrupt(&deadline)) { ret = AVERROR_EXIT; goto done; }
        }
        if (!got_frame) { ret = AVERROR_INVALIDDATA; goto done; }
        vivid = av_frame_get_side_data(frame, AV_FRAME_DATA_DYNAMIC_HDR_VIVID) != NULL;
        hdr_plus = av_frame_get_side_data(frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS) != NULL;
        dovi |= av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA) != NULL;
    }
    ret = snprintf(json, capacity,
        "{\"durationMilliseconds\":%lld,\"videoStreamIndex\":%d,\"hasVideo\":%s,\"hasAudio\":%s,"
        "\"audioStreams\":%s,\"width\":%d,\"height\":%d,\"bitDepth\":%d,\"rotation\":%.0f,"
        "\"pixelFormat\":\"%s\",\"colorPrimaries\":\"%s\",\"colorTransfer\":\"%s\","
        "\"colorSpace\":\"%s\",\"colorRange\":\"%s\",\"hdrVivid\":%s,\"hdr10Plus\":%s,\"dolbyVision\":%s,"
        "\"dolbyVisionProfile\":%d,\"dolbyVisionCompatibilityId\":%d,\"dolbyVisionEnhancementLayer\":%s}",
        (long long)duration, video, parameters ? "true" : "false", audio ? "true" : "false",
        audio_streams, parameters ? parameters->width : 0, parameters ? parameters->height : 0, depth, rotation,
        pixel ? pixel : "unknown", primaries ? primaries : "unknown", transfer ? transfer : "unknown",
        space ? space : "unknown", range ? range : "unknown", vivid ? "true" : "false",
        hdr_plus ? "true" : "false", dovi ? "true" : "false",
        dovi_profile, dovi_compatibility, dovi_enhancement ? "true" : "false");
    ret = ret < 0 || (size_t)ret >= capacity ? AVERROR(ENOSPC) : 0;
done:
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&decoder);
    avformat_close_input(&format);
    if (ret < 0) snprintf(json, capacity, "{\"error\":%d}", ret);
    return ret;
}
#endif
