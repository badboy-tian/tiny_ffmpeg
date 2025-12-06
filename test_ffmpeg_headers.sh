#!/bin/bash
# 测试头文件路径配置

cd /Users/tian/Documents/work/Android/tiny_ffmpeg/example

echo "=== 清理构建缓存 ==="
fvm flutter clean
rm -rf ios/Pods ios/Podfile.lock ios/.symlinks

echo ""
echo "=== 重新安装 pods ==="
cd ios
pod install | tail -10

echo ""
echo "=== 检查生成的配置 ==="
grep "HEADER_SEARCH_PATHS\|USE_HEADERMAP\|ALWAYS_SEARCH" Pods/Target\ Support\ Files/tiny_ffmpeg/tiny_ffmpeg.debug.xcconfig

echo ""
echo "=== 尝试编译（仅显示错误）==="
cd ..
fvm flutter build ios --debug --no-codesign 2>&1 | grep -E "(error:|BUILD|Lexical)" | head -20
