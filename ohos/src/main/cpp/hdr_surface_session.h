#pragma once
#include <chrono>
#include <cmath>
#include <map>
#include <dlfcn.h>
#include <multimedia/player_framework/native_avbuffer.h>
#include "cuva_packet.h"
#include "ohos_sdr_gpu.h"
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/display.h"
#include "libavformat/avio.h"

}

// 独立协议只由本插件的任务入口识别；普通FFmpeg命令及其它平台不受影响。
static int getOhosHdrSurfaceVersion() { return 1; }
// 独立版本门控避免旧插件误执行GPU协议，并保留现有HDR协议兼容性。
static int getOhosSdrGpuVersion() { return 1; }
static int executeHdrSurface(int64_t session, const std::vector<std::string>& args) {
 // GPU对象必须晚于解码器释放，避免Surface回调访问已销毁的纹理。
 std::unique_ptr<OhosSdrGpu> gpu;
 const bool sdr=args.size()>1&&args[1]=="-tiny_sdr_gpu_v1";
 AVFormatContext *in=nullptr,*out=nullptr;AVCodecContext *dec=nullptr,*enc=nullptr;
 AVPacket *packet=av_packet_alloc(),*encoded=av_packet_alloc();AVFrame *frame=av_frame_alloc();
 int index=-1,frames=0,packets=0,cuvaFrames=0;
 bool decoderEof=false,encoderEof=false;
 std::map<int64_t,std::vector<cuva_packet::Message>> metadata;
 bool headerWritten=false;
 std::vector<AVPacket *> pendingAudio;
 std::vector<int> audio;
 std::vector<int64_t> origins;
 auto started=std::chrono::steady_clock::now();
 // 原生阻塞最长两秒返回，循环再查取消；五分钟总限时也必须以失败结束后才能回退。
 auto stopped=[&](){return isSessionCancelled(session)||std::chrono::steady_clock::now()-started>std::chrono::minutes(5);};
 auto stopCode=[&](){return isSessionCancelled(session)?AVERROR_EXIT:AVERROR(ETIMEDOUT);};
 const char *stage="arguments";
 auto run=[&]()->int {
  if(args.size()!=(sdr?14:13)||!packet||!encoded||!frame)return AVERROR(EINVAL);
  double peak=0;
  int stream,bitrate,x,y,w,h;int64_t start,duration;bool mute;
  try {
   stream=std::stoi(args[4]);bitrate=std::stoi(args[5]);start=std::stoll(args[6]);duration=std::stoll(args[7]);
   if(sdr){peak=std::stod(args[13]);if(!std::isfinite(peak)||peak<1||peak>1000)return AVERROR(EINVAL);}
   x=std::stoi(args[8]);y=std::stoi(args[9]);w=std::stoi(args[10]);h=std::stoi(args[11]);mute=args[12]=="1";
  } catch(...) { return AVERROR(EINVAL); }
  if(start<0||duration<=0||duration>600000000LL||start>INT64_MAX-duration||bitrate<1000||x<0||y<0||w<2||h<2||(x|y|w|h)&1)return AVERROR(EINVAL);
  stage="open_input";
  in=avformat_alloc_context();if(!in)return AVERROR(ENOMEM);
  in->interrupt_callback.callback=[](void *opaque)->int{return (*static_cast<decltype(stopped)*>(opaque))();};in->interrupt_callback.opaque=&stopped;
  int rc=avformat_open_input(&in,args[2].c_str(),nullptr,nullptr);if(rc<0)return rc;
  if((rc=avformat_find_stream_info(in,nullptr))<0)return rc;
  if(stream<0||stream>=static_cast<int>(in->nb_streams))return AVERROR(EINVAL);
  index=stream;auto st=in->streams[index];auto par=st->codecpar;
  // 只放行已真机验证的单层10位HEVC/HLG；其它HDR沿用完整软件能力。
  if(par->codec_id!=AV_CODEC_ID_HEVC||par->color_trc!=AVCOL_TRC_ARIB_STD_B67||par->color_primaries!=AVCOL_PRI_BT2020||par->color_space!=AVCOL_SPC_BT2020_NCL||par->color_range!=AVCOL_RANGE_MPEG||par->format!=AV_PIX_FMT_YUV420P10LE||x>par->width-w||y>par->height-h)return AVERROR(ENOSYS);
  auto cap=OH_AVCodec_GetCapabilityByCategory(sdr?"video/avc":"video/hevc",true,HARDWARE);
  if(!cap||!OH_AVCodec_GetCapabilityByCategory("video/hevc",false,HARDWARE))return AVERROR(ENOSYS);
  const int32_t *profiles=nullptr;uint32_t count=0;bool main10=false;
  if(OH_AVCapability_GetSupportedProfiles(cap,&profiles,&count)!=AV_ERR_OK)return AVERROR(ENOSYS);
  for(uint32_t i=0;i<count;++i)main10|=profiles[i]==1;
  OH_AVRange range{};double fps=av_q2d(av_guess_frame_rate(in,st,nullptr));
  if((!sdr&&!main10)||!std::isfinite(fps)||fps<=0||!OH_AVCapability_AreVideoSizeAndFrameRateSupported(cap,sdr?w:par->width,sdr?h:par->height,fps)||OH_AVCapability_GetEncoderBitrateRange(cap,&range)!=AV_ERR_OK||bitrate<range.minVal||range.maxVal<=0)return AVERROR(ENOSYS);
  // 能力上限和有效画面预算共同约束码率，不能超过应用给出的源体积预算。
  bitrate=std::min(bitrate,range.maxVal);
  bitrate=std::min(bitrate,static_cast<int>(std::min(100000000.0,w*1.0*h*fps*.5)));
  if(bitrate<range.minVal)return AVERROR(ENOSYS);
  bool crop=x||y||w!=par->width||h!=par->height;
  if(!sdr&&crop&&(!dlsym(RTLD_DEFAULT,"OH_VideoEncoder_CreatePrimaryWithPreproc")||!OH_AVCapability_IsFeatureSupported(cap,static_cast<OH_AVCapabilityFeature>(10))))return AVERROR(ENOSYS);
  if((rc=avformat_alloc_output_context2(&out,nullptr,"mp4",args[3].c_str()))<0)return rc;
  out->interrupt_callback=in->interrupt_callback;av_dict_copy(&out->metadata,in->metadata,0);
  enc=avcodec_alloc_context3(avcodec_find_encoder_by_name(sdr?"h264_ohcodec":"hevc_ohcodec"));if(!enc)return AVERROR_ENCODER_NOT_FOUND;
  enc->width=sdr?w:par->width;enc->height=sdr?h:par->height;enc->pix_fmt=AV_PIX_FMT_OHCODEC;
  enc->time_base=st->time_base;enc->framerate=av_guess_frame_rate(in,st,nullptr);enc->bit_rate=bitrate;enc->gop_size=30;enc->max_b_frames=0;
  enc->color_primaries=par->color_primaries;enc->color_trc=par->color_trc;enc->colorspace=par->color_space;enc->color_range=par->color_range;
  // SDR输出必须实际映射到709和8位，不能沿用HDR标签。
  if(sdr){enc->color_primaries=AVCOL_PRI_BT709;enc->color_trc=AVCOL_TRC_BT709;enc->colorspace=AVCOL_SPC_BT709;enc->color_range=AVCOL_RANGE_MPEG;}
  enc->profile=sdr?AV_PROFILE_H264_MAIN:AV_PROFILE_HEVC_MAIN_10;enc->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
  av_opt_set_int(enc->priv_data,"allow_sw",0,0);av_opt_set_int(enc->priv_data,"surface_timeout_ms",2000,0);
  av_opt_set(enc->priv_data,"bitrate_mode","vbr",0);
  if(!sdr&&crop){av_opt_set_int(enc->priv_data,"hdr_crop_left",x,0);av_opt_set_int(enc->priv_data,"hdr_crop_top",y,0);av_opt_set_int(enc->priv_data,"hdr_crop_right",x+w-1,0);av_opt_set_int(enc->priv_data,"hdr_crop_bottom",y+h-1,0);}
  stage="open_encoder";if((rc=avcodec_open2(enc,enc->codec,nullptr))<0)return rc;if(!enc->hw_device_ctx)return AVERROR(ENOSYS);
  dec=avcodec_alloc_context3(avcodec_find_decoder_by_name("hevc_ohcodec"));if(!dec)return AVERROR_DECODER_NOT_FOUND;
  if((rc=avcodec_parameters_to_context(dec,par))<0)return rc;
  dec->pkt_timebase=st->time_base;
  // HDR转SDR在两端Surface之间插入GPU，HDR保留仍直接连接原有Surface。
  if(sdr){
   auto device=reinterpret_cast<AVHWDeviceContext *>(enc->hw_device_ctx->data);
   auto window=static_cast<AVOHCodecDeviceContext *>(device->hwctx)->native_window;
   gpu=std::make_unique<OhosSdrGpu>();
   stage="gpu_init";if((rc=gpu->init(static_cast<OHNativeWindow *>(window),w,h,par,peak))<0)return rc;
   if((rc=av_hwdevice_ctx_create(&dec->hw_device_ctx,AV_HWDEVICE_TYPE_OHCODEC,nullptr,nullptr,0))<0)return rc;
   auto target=reinterpret_cast<AVHWDeviceContext *>(dec->hw_device_ctx->data);
   static_cast<AVOHCodecDeviceContext *>(target->hwctx)->native_window=gpu->inputWindow();
  }else dec->hw_device_ctx=av_buffer_ref(enc->hw_device_ctx);
  if(!dec->hw_device_ctx)return AVERROR(ENOMEM);
  av_opt_set_int(dec->priv_data,"allow_sw",0,0);av_opt_set_int(dec->priv_data,"surface_timeout_ms",2000,0);
  stage="open_decoder";if((rc=avcodec_open2(dec,dec->codec,nullptr))<0)return rc;
  auto dst=avformat_new_stream(out,nullptr);if(!dst)return AVERROR(ENOMEM);dst->time_base=enc->time_base;
  if((rc=avcodec_parameters_from_context(dst->codecpar,enc))<0)return rc;
  // 鸿蒙播放器对默认hev1的Main10输出会显示纯绿；hvc1使hvcC参数集完整声明。
  // 只改变HEVC封装描述，不降位深、不重编码或修改CUVA画面负载。
  dst->codecpar->codec_tag=sdr?MKTAG('a','v','c','1'):MKTAG('h','v','c','1');
  dst->codecpar->width=w;dst->codecpar->height=h;av_dict_copy(&dst->metadata,st->metadata,0);
  auto matrix=av_packet_side_data_get(par->coded_side_data,par->nb_coded_side_data,AV_PKT_DATA_DISPLAYMATRIX);
  if(matrix){auto copy=av_packet_side_data_new(&dst->codecpar->coded_side_data,&dst->codecpar->nb_coded_side_data,AV_PKT_DATA_DISPLAYMATRIX,matrix->size,0);if(!copy)return AVERROR(ENOMEM);memcpy(copy->data,matrix->data,matrix->size);}
  audio.assign(in->nb_streams,-1);origins.assign(in->nb_streams,0);
  // 所有轨道减去相同的裁剪原点，而非分别清零首个样本，避免新增音画偏移。
  int64_t base=(in->start_time==AV_NOPTS_VALUE?0:in->start_time)+start;
  for(unsigned i=0;i<in->nb_streams;++i){
   auto source=in->streams[i];origins[i]=av_rescale_q(base,AV_TIME_BASE_Q,source->time_base);
   if(!mute&&source->codecpar->codec_type==AVMEDIA_TYPE_AUDIO){
    if(source->codecpar->codec_id!=AV_CODEC_ID_AAC)return AVERROR(ENOSYS);
    auto target=avformat_new_stream(out,nullptr);if(!target)return AVERROR(ENOMEM);
    if((rc=avcodec_parameters_copy(target->codecpar,source->codecpar))<0)return rc;
    target->codecpar->codec_tag=0;target->time_base=source->time_base;target->disposition=source->disposition;
    av_dict_copy(&target->metadata,source->metadata,0);audio[i]=target->index;
   }
  }
  if((rc=avio_open2(&out->pb,args[3].c_str(),AVIO_FLAG_WRITE,&out->interrupt_callback,nullptr))<0)return rc;
  auto writeAudio=[&](AVPacket *value)->int {
   int i=value->stream_index;auto source=in->streams[i];auto target=out->streams[audio[i]];
   av_packet_rescale_ts(value,source->time_base,target->time_base);
   value->stream_index=target->index;value->pos=-1;
   return av_interleaved_write_frame(out,value);
  };
  auto ensureHeader=[&]()->int {
   if(headerWritten)return 0;
   // OHCodec通过首包NEW_EXTRADATA交付配置；AVCodecContext不会自动同步，提前写头会生成空hvcC。
   size_t codecSize=0;
   const uint8_t *codecData=av_packet_get_side_data(encoded,AV_PKT_DATA_NEW_EXTRADATA,&codecSize);
   if(!codecData||!codecSize||codecSize>65536)return AVERROR_INVALIDDATA;
   // HDR验证VPS/SPS/PPS，SDR验证SPS/PPS，避免系统播放器缺少参数集。
   std::vector<cuva_packet::Bytes> nals;
   if(!cuva_packet::split(codecData,codecSize,0,nals))return AVERROR_INVALIDDATA;
   unsigned types=0;for(const auto &nal:nals){if(sdr){auto type=nal[0]&31;if(type==7||type==8)types|=1U<<(type-7);}else{auto type=(nal[0]>>1)&63;if(type>=32&&type<=34)types|=1U<<(type-32);}}
   if(types!=(sdr?3:7))return AVERROR_INVALIDDATA;
   av_freep(&dst->codecpar->extradata);
   dst->codecpar->extradata=static_cast<uint8_t *>(av_mallocz(codecSize+AV_INPUT_BUFFER_PADDING_SIZE));
   if(!dst->codecpar->extradata)return AVERROR(ENOMEM);
   memcpy(dst->codecpar->extradata,codecData,codecSize);dst->codecpar->extradata_size=codecSize;
   // 输出参数描述编码后的真实位深，避免把仅供传递的OHCODEC句柄格式写进容器。
   dst->codecpar->format=sdr?AV_PIX_FMT_YUV420P:AV_PIX_FMT_YUV420P10LE;dst->codecpar->bits_per_raw_sample=sdr?8:10;
   AVDictionary *options=nullptr;av_dict_set(&options,"movflags","+faststart+use_metadata_tags",0);
   int er=avformat_write_header(out,&options);av_dict_free(&options);if(er<0)return er;
   headerWritten=true;
   // 配置已在写头时消费，移除同一侧数据，避免muxer再次更新样本描述。
   av_packet_side_data_remove(encoded->side_data,&encoded->side_data_elems,AV_PKT_DATA_NEW_EXTRADATA);
   // 音频先到时暂存原时间基包，MP4头确定各轨时间基后再换算，保留共同裁剪偏移。
   for(auto &value:pendingAudio){if((er=writeAudio(value))<0)return er;av_packet_free(&value);}pendingAudio.clear();
   return 0;
  };
  int lengthSize=par->extradata_size>=23&&par->extradata[0]==1?(par->extradata[21]&3)+1:0;
  int64_t end=origins[index]+av_rescale_q(duration,AV_TIME_BASE_Q,st->time_base);
  auto drainEncoder=[&](bool wait)->int {
   while(!stopped()){
    int er=avcodec_receive_packet(enc,encoded);if(er==AVERROR_EOF){encoderEof=true;return 0;}
    if(er==AVERROR(EAGAIN)){if(!wait)return 0;av_usleep(1000);continue;}if(er<0)return er;
    if((er=ensureHeader())<0)return er;
    auto found=metadata.find(encoded->pts);if(found==metadata.end()) { av_log(nullptr, AV_LOG_ERROR, "HDR Surface PTS unmatched output=%lld pending=%zu\n", (long long)encoded->pts, metadata.size()); return AVERROR_INVALIDDATA; }
    cuva_packet::Bytes repaired;if(!sdr){if(!cuva_packet::replace(encoded->data,encoded->size,found->second,repaired)) { av_log(nullptr, AV_LOG_ERROR, "HDR Surface CUVA replace failed pts=%lld packet=%d\n", (long long)encoded->pts, encoded->size); return AVERROR_INVALIDDATA; }
    int previous=encoded->size;if(repaired.size()>static_cast<size_t>(previous)){if((er=av_grow_packet(encoded,repaired.size()-previous))<0)return er;}else av_shrink_packet(encoded,repaired.size());
    memcpy(encoded->data,repaired.data(),repaired.size());cuvaFrames+=!found->second.empty();}metadata.erase(found);
    // 无B帧时补DTS，末帧时长不能越过用户裁剪终点。
    if(encoded->pts==AV_NOPTS_VALUE)return AVERROR_INVALIDDATA;
    encoded->dts=encoded->pts;
    int64_t nominal=av_rescale_q(1,av_inv_q(enc->framerate),enc->time_base);
    encoded->duration=std::min(nominal,end-encoded->pts);if(encoded->duration<=0)return AVERROR_INVALIDDATA;
    encoded->pts-=origins[index];encoded->dts-=origins[index];
    int64_t progress=av_rescale_q(encoded->pts+encoded->duration,enc->time_base,AV_TIME_BASE_Q);
    av_packet_rescale_ts(encoded,enc->time_base,dst->time_base);encoded->stream_index=dst->index;
    er=av_interleaved_write_frame(out,encoded);av_packet_unref(encoded);if(er<0)return er;++packets;
    tiny_ffmpeg_report_progress(session,progress,0,0);
   }return stopCode();
  };
  auto drainDecoder=[&](bool wait)->int {
   while(!stopped()){
    int er=avcodec_receive_frame(dec,frame);if(er==AVERROR_EOF){decoderEof=true;return 0;}
    if(er==AVERROR(EAGAIN)){if(!wait)return 0;av_usleep(1000);continue;}if(er<0)return er;
    if(frame->pts==AV_NOPTS_VALUE||frame->format!=AV_PIX_FMT_OHCODEC)return AVERROR_INVALIDDATA;
    if(frame->pts<origins[index]||frame->pts>=end){
     // OHCodec默认在unref时渲染Surface，单纯丢弃AVFrame仍会把区外画面送进编码器。
     // 必须标记底层输出缓冲区DISCARD，release回调才会Free而非Render。
     auto buffer=reinterpret_cast<OH_AVBuffer *>(frame->data[3]);OH_AVCodecBufferAttr attr;
     if(!buffer||OH_AVBuffer_GetBufferAttr(buffer,&attr)!=AV_ERR_OK)return AVERROR_EXTERNAL;
     attr.flags|=AVCODEC_BUFFER_FLAGS_DISCARD;
     if(OH_AVBuffer_SetBufferAttr(buffer,&attr)!=AV_ERR_OK)return AVERROR_EXTERNAL;
     metadata.erase(frame->pts);av_frame_unref(frame);continue;
    }
    // 编解码器的Surface时间戳均使用微秒，不能套用EGL常见的纳秒时间基。
    // 释放解码帧才会提交NativeImage，GPU必须在同一工作线程消费后再取下一帧。
    if(sdr){int64_t timestamp=av_rescale_q(frame->pts,st->time_base,AV_TIME_BASE_Q);av_frame_unref(frame);if((er=gpu->render(timestamp,x,y,w,h,par->width,par->height,stopped))<0)return er;}
    else while((er=avcodec_send_frame(enc,frame))==AVERROR(EAGAIN)){if((er=drainEncoder(false))<0)return er;if(stopped())return stopCode();}
    av_frame_unref(frame);if(er<0)return er;++frames;if((er=drainEncoder(false))<0)return er;
   }return stopCode();
  };
  // 从临近关键帧开始解码，精确丢弃裁剪起点以前的画面，而不是关键帧级粗剪。
  if(base>0){rc=avformat_seek_file(in,-1,INT64_MIN,base,base,AVSEEK_FLAG_BACKWARD);if(rc<0)return rc;}
  stage="encode";
  bool videoPastEnd=false;
  while(!stopped()&&(rc=av_read_frame(in,packet))>=0){
   int i=packet->stream_index;
   if(i==index&&!videoPastEnd){
    // 仅无重排序样本越界时可提前停止送包；其它情况继续解码到EOF保证B帧不丢。
    if(packet->pts==AV_NOPTS_VALUE)return AVERROR_INVALIDDATA;
    if(metadata.size()>=512||metadata.count(packet->pts))return AVERROR_INVALIDDATA;
    std::vector<cuva_packet::Message> cuva;if(!cuva_packet::extract(packet->data,packet->size,lengthSize,cuva))return AVERROR_INVALIDDATA;
    metadata.emplace(packet->pts,std::move(cuva));
    while((rc=avcodec_send_packet(dec,packet))==AVERROR(EAGAIN)){if((rc=drainDecoder(false))<0)return rc;if(stopped())return stopCode();}
    if(rc<0)return rc;if((rc=drainDecoder(false))<0)return rc;
    if(packet->pts>=end+av_rescale_q(2000000,AV_TIME_BASE_Q,st->time_base))videoPastEnd=true;
   }else if(audio[i]>=0){
    auto source=in->streams[i];int64_t pts=packet->pts;
    if(pts==AV_NOPTS_VALUE||packet->dts==AV_NOPTS_VALUE)return AVERROR_INVALIDDATA;
    int64_t audioEnd=origins[i]+av_rescale_q(duration,AV_TIME_BASE_Q,source->time_base);
    // AAC整包直拷保留共同时间基准，边界最多一个音频包；无兼容音轨时整任务回退。
    if(pts>=origins[i]&&pts<audioEnd){packet->pts-=origins[i];packet->dts-=origins[i];packet->duration=std::min(packet->duration,audioEnd-pts);
     if(headerWritten){if((rc=writeAudio(packet))<0)return rc;}
     else {
      // 有界缓存防止音频先开始的长文件无限占内存；超限回退现有软件路径。
      if(pendingAudio.size()>=512)return AVERROR(ENOBUFS);
      AVPacket *copy=av_packet_clone(packet);if(!copy)return AVERROR(ENOMEM);
      try { pendingAudio.push_back(copy); } catch (...) { av_packet_free(&copy); throw; }
     }
    }
   }
   av_packet_unref(packet);
  }
  if(stopped())return stopCode();if(rc!=AVERROR_EOF)return rc;
  while((rc=avcodec_send_packet(dec,nullptr))==AVERROR(EAGAIN)){if((rc=drainDecoder(false))<0)return rc;if(stopped())return stopCode();}
  if(rc<0&&rc!=AVERROR_EOF)return rc;if((rc=drainDecoder(true))<0)return rc;
  while((rc=avcodec_send_frame(enc,nullptr))==AVERROR(EAGAIN)){if((rc=drainEncoder(false))<0)return rc;if(stopped())return stopCode();}
  if(rc<0&&rc!=AVERROR_EOF)return rc;if((rc=drainEncoder(true))<0)return rc;
  // 只有选区内的元数据必须全部匹配；为解码依赖读取的区外包允许保留。
  for(auto &entry:metadata)if(entry.first>=origins[index]&&entry.first<end)return AVERROR_INVALIDDATA;
  if(!headerWritten||!decoderEof||!encoderEof||frames==0||frames!=packets)return AVERROR_INVALIDDATA;
  stage="trailer";if(stopped())return stopCode();return av_write_trailer(out);
 };
 int code;
 // 解析器和容器的内存分配异常也必须走同一释放出口，不能绕过任务终态。
 try { code=run(); } catch(const std::bad_alloc&) { code=AVERROR(ENOMEM); }
 catch(...) { code=AVERROR(EINVAL); }
 // Codec先释放，任务才返回终态；外层因此可以安全删除残片或软件重试。
 for(auto &value:pendingAudio)av_packet_free(&value);
 av_frame_free(&frame);av_packet_free(&packet);av_packet_free(&encoded);avcodec_free_context(&dec);avcodec_free_context(&enc);avformat_close_input(&in);
 if(out){if(out->pb)avio_closep(&out->pb);avformat_free_context(out);}
 av_log(nullptr,code<0?AV_LOG_ERROR:AV_LOG_INFO,"%s stage=%s code=%d frames=%d packets=%d cuva=%d\n",sdr?"SDR GPU":"HDR Surface",stage,code,frames,packets,cuvaFrames);
 return code;
}
