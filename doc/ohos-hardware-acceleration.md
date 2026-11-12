# OHOS 硬件编码修复与使用边界

本地插件版本：0.0.14。仅更新OHOS原生实现及arm64库，Android/iOS产物与公共Dart API不变。应用通过能力判断显式选择硬编码。

## 已修复

- 编码帧 PTS 按 AVCodecContext.time_base 换算到系统微秒时间基，避免120帧被压缩为约0.024秒。
- 输入像素布局改从 OH_VideoEncoder_GetInputDescription 查询实际 stride、slice height；不能用压缩码流输出描述或假定 stride=width，否则非对齐裁剪可能花屏。
- 对齐图像写入前检查缓冲区容量，布局无效时返回失败。
- 将已知的原色、传递函数、矩阵和电平传给系统编码器，未知字段保持未知；这些参数不能替代真实的像素色彩转换。

构建修复保存在 tpc_c_cplusplus/thirdparty/FFmpeg 的 apply-ohcodec.sh、patches/ 和 HPKBUILD。使用同一FFmpeg发布包，解压后应用鸿蒙专用补丁；重复应用可通过，源码不匹配立即停止。

## 真机验证范围

设备 KLE-AL00U，HarmonyOS API26。验证普通SDR裁剪、602×322和600×320等非对齐尺寸、4K60 HLG经过现有CPU映射再硬编，以及带初始音轨偏移的5fps精确剪辑/半速输出。核对逐帧时间戳、音画轨间偏移、完整解码、尺寸、色彩和参考画面。

硬件名称为 h264_ohcodec / hevc_ohcodec；configure中的 *_oh 不是命令运行时名称。需要明确禁止系统软件回退时设置 -allow_sw 0。

此设备通过官方能力接口返回 H264/HEVC 硬编支持4K60，码率范围10,000–100,000,000bps；不能把这些数值硬编码给所有设备。先前约166Mbps配置超出此范围。

2秒SDR隔离样本的软件编码约3.62秒，硬解硬编约1.3秒，输出约10MB；不含封面、音频、相册保存，不能据此承诺整个业务流程固定快几倍。4K软件和硬件测试码率不同，不能把耗时差称为同画质收益。

## Buffer与Surface边界

当前OHCodec软件帧输入仍只有NV12，不支持P010；不能用其默认Buffer解码路径声称保留10bit HDR。已验证的HDR保留改用独立Main10 Surface路径，其余格式继续软件。

HDR Vivid官方系统映射属于独立Surface实验：Buffer模式Prepare失败；Surface配合默认像素格式/NV12可Prepare，显式SURFACE_FORMAT的转换配置被拒绝。后续真实Surface转码已通过：2.033秒Vivid素材导出61帧8bit BT.709 SDR，逐帧PTS与源一致，完整解码无错，显示旋转保留，约3.11MB、耗时2.741秒。这是无音频、无crop的隔离原型，系统映射比现有mobius映射偏暗；不能据此宣称完整业务已支持或固定提速。实验解码开关与原生探针未加入正式插件。

## ClipImage正式业务接入（本地未发布）

单视频与批量导出已接入同一能力选择和软件回退：明确的8位BT.709有限范围SDR可硬解硬编；已验证的HLG/Vivid转SDR可走下述GPU映射路径，保留HDR使用Main10 Surface路径，其余HDR真实映射为SDR后使用硬编，未知色彩继续软件。无编辑的直拷不改。通过getOhosVideoEncoder通道在工作线程检查本机NV12、VBR、尺寸、帧率和码率能力；旧插件缺接口时安全沿用软件。

目标码率受源文件预算、设备上限以及0.5 bit/pixel/frame高质量像素预算共同约束，不承诺与软件CRF18像素等价或所有素材体积都更小。硬编失败须确认原生停止、删除残片后最多重试一次原始软件命令；取消和停止未确认不重试。SDR成功校验8位709，HDR成功校验10位2020 HLG与实际编码尺寸，并共同检查输出时长，避免快放成品流入相册。

本机完整流程（探测、编码、封面、系统实况封装，非上传）2秒样本：SDR5.028→2.884秒，32.05MB源输出10.97MB；4K60 HLG转SDR56.497→36.876秒，20.70MB源输出16.98MB；Vivid转SDR7.883→3.782秒，3.60MB源输出2.93MB。软件/硬件码率控制不同，这些是当前策略的完整业务收益，不是同画质固定倍数。小尺寸短片可能因初始化略慢。

真实原生不支持10位输入触发软件重试、硬编中取消、取消后同一服务再次导出均通过；另有36项相关自动测试。系统Surface映射原型仍不在正式插件。插件和App尚未提交发布，当前真机验收使用本地0.0.14；远端固定依赖更新前需保留本地开发覆盖。

## 官方资料

- [编码输入描述与Surface接口](https://developer.huawei.com/consumer/cn/doc/harmonyos-references/capi-native-avcodec-videoencoder-h)
- [stride、slice height与HDR Vivid输出色彩空间](https://developer.huawei.com/consumer/cn/doc/harmonyos-references/capi-native-avcodec-base-h)
- [设备编解码能力查询](https://developer.huawei.com/consumer/cn/doc/harmonyos-references/capi-native-avcapability-h)


## OHOS HDR Surface接入说明

- HDR保留：HEVC Main10 / BT.2020 HLG，包括HDR Vivid，支持精确时间剪辑、偶数边界crop、源显示旋转和AAC音轨直拷/静音。
- UI显示选区在Dart反向转换到编码坐标，输出保留源显示矩阵；其它用户旋转、变速、倒放、HDR10+和Dolby Vision继续软件处理。
- 所有任务通过现有executeAsync Session运行；取消后等待Codec与文件释放，取消不触发重试；硬编失败或成品格式异常时，清理残片后只重试一次原软件命令。
- CUVA按源PTS匹配，原始T.35负载去重写回，首帧同样处理；不重算裁剪后的场景统计信息。
- 仅新Surface命令启用2秒Codec条件等待上限和5分钟总限时；旧SDR命令不启用该选项。
- API26裁剪创建入口和参数键动态查找；旧系统、无Main10/前处理能力、非AAC音轨及其它不支持情况回退软件，不能以NV12降位深冒充HDR。
- VBR目标不超过应用源体积预算、设备上限和画面像素预算；不保证与原视频数学无损或所有源必定更小。

自定义命令`-tiny_hdr_surface_v1`属于OHOS插件内部协议，由`getOhosHdrSurfaceVersion`协商后才能提交，不可直接传给普通FFmpeg或其它平台。


## HDR绿屏封装修复

OHOS Main10 Surface输出显式使用hvc1，muxer为hvcC参数集设置完整声明。默认hev1输出虽然FFmpeg软件解码正常，但本机AVPlayer/Flutter纹理播放呈纯绿、音频正常。用户原片hvc1可正常播放；同一失败输出无重编码重封装为hvc1后正常，视频包SHA256逐包完全一致。该修复保留10位HLG/Vivid，不引入SDR预览转码。

验收必须包含产品实际ExportPreviewScreen的屏幕播放，不能以ffprobe和软件解码通过替代。


## 2026-11-12 预览绿屏修复

真机对照确认：原片 `hvc1` 正常，硬编输出默认 `hev1` 且 hvcC 参数盒为空，HarmonyOS AVPlayer/Flutter外部纹理显示纯绿但音频正常；FFmpeg软件解码仍能显示，不能作为系统播放验收。将同一失败视频只重封装为 `hvc1` 后画面恢复，视频包逐包字节一致。

最终实现从首个OHCodec编码包读取 `AV_PKT_DATA_NEW_EXTRADATA`，校验VPS/SPS/PPS并交给MP4 muxer生成hvcC。写头等待首包配置到齐，同时暂存先到的音频包。KLE-AL00U已通过产品实际预览页播放验证，HDR Vivid的CUVA与时间戳均逐包核对通过。

## 2026-11-12 SDR GPU映射

HLG/Vivid转SDR使用硬件HEVC解码、NativeImage纹理、65³ RGB16F查找表和H264 Surface编码。查找表由现有FFmpeg zscale/Mobius滤镜使用同一峰值生成，保留203参考白与BT.709输出。GPU插值及硬件采样、编码会产生像素差异。

通过 `getOhosSdrGpuVersion` 协商 `-tiny_sdr_gpu_v1`。当前支持10位有限范围BT.2020 HLG/HEVC，包括Vivid，支持精确剪辑、偶数边界crop、源旋转矩阵、AAC直拷和静音。用户额外旋转、变速、倒放、PQ、HDR10+、Dolby Vision及其它滤镜继续走现有处理路线。

编码端手动RequestBuffer，在申请前写入微秒时间戳，EGLImage/FBO绘制后FlushBuffer提交。该方式避免EGL预取缓冲造成重复首帧PTS，也避免纳秒与Codec微秒混用。NativeImage原点与编码选区原点分别处理，非居中裁剪通过参考图核验。原生资源在Session终态前释放，现有停止确认、残片清理和软件回退继续生效。

KLE-AL00U上的同一映射参数、同一目标码率预算比较如下，均为2秒素材的转码阶段，峰值分析、封面和实况封装另计。

| 素材 | CPU映射加硬编 | GPU映射加硬编 | 输出视频大小 |
| --- | --- | --- | --- |
| 1080p HDR Vivid | 3.968秒 | 1.413秒 | 2,904,133字节 |
| 4K60 HLG | 26.402秒 | 5.895秒 | 17,261,145字节 |

完整业务中，已完成峰值分析后的编码、封面和实况封装分别为1.586秒和7.306秒。首次峰值分析本轮分别花费1.899秒和6.205秒，重复同一素材有缓存。

全分辨率逐帧相对于CPU映射加硬编，Vivid的SSIM为0.9871，4K HLG为0.9568。输出分辨率保持源选区尺寸。相对于软件映射编码的旋转裁剪样本，SSIM为0.9887。以上指标衡量两种SDR输出的差异，不能作为HDR原片无损指标。

真机验证包括61/120帧与源PTS一致、旋转素材非居中crop、44个AAC包原样保留且音画偏移一致、完整软件解码、实际ExportPreviewScreen播放、真实取消、取消后恢复、原生失败后软件回退，以及HDR保留分支的61帧CUVA逐包一致。测试包结束后恢复正常入口。
