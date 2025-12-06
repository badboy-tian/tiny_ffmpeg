#!/bin/bash
# 快速编译测试脚本

echo "=== 清理并重新安装 pods ==="
cd "$(dirname "$0")/example/ios"
rm -rf Pods Podfile.lock
pod install --verbose 2>&1 | grep -E "(tiny_ffmpeg|HEADER_SEARCH_PATHS)" || true

echo ""
echo "=== 检查生成的 xcconfig 文件 ==="
echo "--- tiny_ffmpeg.xcconfig ---"
grep "HEADER_SEARCH_PATHS" "Pods/Target Support Files/tiny_ffmpeg/tiny_ffmpeg.xcconfig" 2>/dev/null || echo "文件不存在"

echo ""
echo "=== 尝试编译 ==="
cd ..
fvm flutter build ios --debug --no-codesign 2>&1 | tail -50
