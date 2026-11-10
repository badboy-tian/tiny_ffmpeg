# 第三方组件与交付说明

当前 FFmpeg 构建为 9.0.2，x265 为 4.2。Android、iOS、HarmonyOS 均包含 CUVA 和 HDR10+ 写回修改，Dolby Vision 使用 FFmpeg 的 RPU 编码实现。原始许可证全文保存在 licenses 目录，头文件和源文件中的原始版权声明随代码保留。

| 组件 | 当前启用方式 | 随附许可文本 |
| --- | --- | --- |
| FFmpeg 9.0.2 | 启用 GPL 组件，CONFIG_GPL=1 | licenses/FFmpeg-LICENSE.md、FFmpeg-GPLv2.txt、FFmpeg-LGPLv2.1.txt |
| x264 | 静态依赖 | licenses/x264-COPYING.txt |
| x265 4.2 Main 10 | 静态依赖，FFmpeg 侧添加 CUVA、HDR10+ 写回 | licenses/x265-COPYING.txt |
| LAME | MP3 编码静态依赖 | licenses/LAME-COPYING.txt |
| zimg | zscale 静态依赖 | licenses/zimg-COPYING.txt |
| Aki 1.3.1 | HarmonyOS 原生桥接 | licenses/AKI-LICENSE.txt、AKI-NOTICE.txt |
| zlib | HarmonyOS 静态依赖，其他平台系统依赖 | licenses/zlib-LICENSE.txt |

Android/iOS 的默认脚本关闭 FDK AAC，使用 FFmpeg 原生 AAC。三端目标均为 CONFIG_NONFREE=0。旧命令中的 libfdk_aac 应改为 aac，HE-AAC 专用参数需要由调用方重新评估。HarmonyOS 的 Aki、系统库和 Flutter 依赖继续保留各自上游许可证。

商业发布前由权利人确认自有代码的授权声明、实际组合软件的授权路线、第三方声明的展示位置，以及完整对应源码的提供方式。闭源授权需覆盖实际使用的全部组件。编码相关的专利许可需要单独核查。

对应源码交付应包括固定版本源码、完整编译脚本、CUVA 修改、平台补丁及构建配置。现有源码修改位于 ffmpeg-build-scripts/tools/ffmpeg-vivid 和 tpc_c_cplusplus/thirdparty/FFmpeg/vivid。发布时需要提供接收方可实际获取的源码地址或源码包。

上游依据为 `https://ffmpeg.org/legal.html`、`https://ffmpeg.org/doxygen/trunk/md_LICENSE.html` 和 `https://www.x265.org/`。
