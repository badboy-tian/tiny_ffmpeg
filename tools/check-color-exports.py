#!/usr/bin/env python3
"""验证 color_parity_device.dart 拉回的原生导出文件；需要 PATH 中的 ffprobe。"""

import argparse
import json
from pathlib import Path
import subprocess


def check(directory):
    rows = json.loads((directory / 'results.json').read_text())
    assert rows[-1]['name'] == 'PASS', '设备测试尚未通过'
    records = {row['name']: row for row in rows}
    checked = []
    for kind in ('hlg', 'vivid', 'dovi', 'hdr10plus'):
        for mode in ('trim', 'crop', 'sdr'):
            name = f'{kind}_{mode}'
            result = subprocess.run(
                ['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                 '-show_streams', '-show_frames', '-of', 'json',
                 str(directory / f'{name}.mp4')],
                check=True, capture_output=True, text=True)
            # 原生接口只能抽样探测；这里解完整段，避免首帧正确而后续损坏或残留元数据。
            assert not result.stderr.strip(), f'{name}: {result.stderr}'
            data = json.loads(result.stdout)
            stream = data['streams'][0]
            frames = data['frames']
            assert frames, f'{name}: 无可解码画面'
            row = records[name]
            assert (stream['width'], stream['height']) == (row['width'], row['height']), name
            pts = [float(frame['best_effort_timestamp_time']) for frame in frames]
            assert all(b > a for a, b in zip(pts, pts[1:])), f'{name}: 时间戳不递增'
            for item in [stream, *frames]:
                assert item.get('color_transfer') == row['transfer'], f'{name}: 传递函数错误'
                assert item.get('color_primaries') == row['primaries'], f'{name}: 色域错误'
                assert item.get('color_space') == row['matrix'], f'{name}: 色彩矩阵错误'
            types = {s['side_data_type'] for item in [stream, *frames]
                     for s in item.get('side_data_list', [])}
            if mode == 'sdr':
                assert stream['pix_fmt'] == 'yuv420p', f'{name}: SDR 位深错误'
                # 不只检查 isHdr；静态亮度、动态 HDR 和 Dolby Vision 容器信令也必须消失。
                forbidden = ('HDR', 'Dolby', 'DOVI', 'Mastering display', 'Content light')
                assert not any(any(key in value for key in forbidden) for value in types), (name, types)
            else:
                assert stream['pix_fmt'] == 'yuv420p10le', f'{name}: HDR 位深错误'
                required = {'vivid': 'HDR Dynamic Metadata CUVA 005.1 2021 (Vivid)',
                            'dovi': 'Dolby Vision RPU Data',
                            'hdr10plus': 'HDR Dynamic Metadata SMPTE2094-40 (HDR10+)'}
                if kind in required:
                    assert all(any(s['side_data_type'] == required[kind]
                                   for s in frame.get('side_data_list', []))
                               for frame in frames), f'{name}: 有画面丢失动态元数据'
                if kind == 'dovi':
                    assert 'DOVI configuration record' in types, f'{name}: 缺少容器配置'
            checked.append({'name': name, 'frames': len(frames),
                            'bytes': (directory / f'{name}.mp4').stat().st_size,
                            'sideData': sorted(types)})
    return checked


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    checked = check(args.directory)
    (args.directory / 'validation.json').write_text(json.dumps(checked, ensure_ascii=False, indent=2))
    print(f'PASS: {len(checked)} 个导出文件完整解码、时间戳、色彩与逐帧元数据检查通过')
