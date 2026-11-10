"""使用主机 FFmpeg 开发库验证三端共享探测器：python3 test/native_media_info_regression.py。"""

import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADERS = [
    ROOT / "android/src/main/cpp/tiny_media_info.h",
    ROOT / "ios/Classes/tiny_media_info.h",
    ROOT / "ohos/src/main/cpp/tiny_media_info.h",
]


class MediaInfoRegression(unittest.TestCase):
    def test_delayed_video_and_decoder_drain(self):
        # 三端必须使用同一实现，防止只修 Android 后其它平台继续误拒绝输入。
        self.assertTrue(all(p.read_bytes() == HEADERS[0].read_bytes() for p in HEADERS))
        flags = shlex.split(subprocess.check_output(
            ["pkg-config", "--cflags", "--libs", "libavformat", "libavcodec", "libavutil"],
            text=True))
        with tempfile.TemporaryDirectory(prefix="tiny_probe_regression_") as directory:
            work = Path(directory)

            def ffmpeg(*args):
                subprocess.run(["ffmpeg", "-v", "error", "-y", *args],
                               check=True, capture_output=True)

            late = work / "late.mp4"
            # 8 秒 AAC 足以在首个视频包之前产生 256 个以上音频包。
            ffmpeg("-f", "lavfi", "-i", "sine=sample_rate=48000:duration=10",
                   "-itsoffset", "8", "-f", "lavfi", "-i", "color=red:size=64x64:rate=10:duration=1",
                   "-map", "0:a", "-map", "1:v", "-c:a", "aac", "-c:v", "libx264",
                   "-fps_mode", "passthrough", str(late))
            packets = json.loads(subprocess.check_output(
                ["ffprobe", "-v", "error", "-show_packets", "-show_entries", "packet=stream_index",
                 "-of", "json", str(late)], text=True))["packets"]
            self.assertGreater(next(i for i, p in enumerate(packets) if p["stream_index"] == 1), 256)
            single = work / "single.mp4"
            # 单帧 H.264 往往需要 EOF 冲刷才能拿到首帧，不能修复上限后破坏短视频。
            ffmpeg("-f", "lavfi", "-i", "color=red:size=64x64:rate=10", "-frames:v", "1",
                   "-c:v", "libx264", str(single))
            audio = work / "audio.m4a"
            ffmpeg("-f", "lavfi", "-i", "sine=duration=0.1", "-c:a", "aac", str(audio))
            invalid = work / "invalid.mp4"
            invalid.write_bytes(b"invalid")
            for compiler, suffix in [("clang", "c"), ("clang++", "cpp")]:
                source = work / f"probe.{suffix}"
                include = f'#include "{HEADERS[0]}"\n'
                if suffix == "cpp":
                    include = 'extern "C" {\n' + include + '}\n'
                source.write_text(include + 'int main(int argc, char **argv) { char json[2048]; '
                                  'int ret = tiny_media_info(argv[1], json, sizeof(json)); '
                                  'puts(json); return ret < 0; }\n')
                binary = work / f"probe-{suffix}"
                subprocess.run([compiler, str(source), *flags, "-o", str(binary)], check=True)
                for path, index in [(late, 1), (single, 0), (audio, -1)]:
                    result = subprocess.run([str(binary), str(path)], capture_output=True,
                                            text=True, timeout=12, check=True)
                    info = json.loads(result.stdout)
                    self.assertEqual(info["videoStreamIndex"], index)
                    self.assertEqual(info["hasVideo"], index >= 0)
                    if index >= 0:
                        # 仅 hasVideo 成功还不够：晚起视频的像素格式必须由首帧补齐才能继续编码。
                        self.assertEqual(info["bitDepth"], 8)
                        self.assertEqual(info["pixelFormat"], "yuv420p")
                # 损坏文件仍应明确失败，不能为兼容晚起视频而虚构探测成功。
                result = subprocess.run([str(binary), str(invalid)], capture_output=True,
                                        text=True, timeout=12)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("error", json.loads(result.stdout))


if __name__ == "__main__":
    unittest.main()
