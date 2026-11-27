#!/bin/bash
# WorkTool.app을 만들어요 (Apple Silicon + Intel 범용). 결과: dist/WorkTool.app, dist/WorkTool-macOS.zip
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${VERSION:-0.3.2}"
APP=dist/WorkTool.app

swift build -c release --arch arm64 --arch x86_64 --product WorkTool
BIN="$(swift build -c release --arch arm64 --arch x86_64 --show-bin-path)"

rm -rf "$APP" && mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN/WorkTool" "$APP/Contents/MacOS/WorkTool"
cp -R "$BIN/WorkTool_WorkToolCore.bundle" "$APP/Contents/Resources/"

# 앱 아이콘
ICONSET="$(mktemp -d)/AppIcon.iconset"; mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
  sips -z $s $s Resources/icon-512.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  d=$((s*2)); if [ $d -le 512 ]; then sips -z $d $d Resources/icon-512.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null; fi
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"

# 강조색(WorkTool 녹색): 사이드바·표 선택 색이 시스템 '멀티컬러'일 때 이 색을 따라요.
ASSETS="$(mktemp -d)/Assets.xcassets"; mkdir -p "$ASSETS/AccentColor.colorset"
echo '{"info":{"version":1,"author":"xcode"}}' > "$ASSETS/Contents.json"
cat > "$ASSETS/AccentColor.colorset/Contents.json" <<'JSON'
{"info":{"version":1,"author":"xcode"},"colors":[
 {"idiom":"universal","color":{"color-space":"srgb","components":{"red":"0x26","green":"0x73","blue":"0x58","alpha":"1.000"}}},
 {"idiom":"universal","appearances":[{"appearance":"luminosity","value":"dark"}],"color":{"color-space":"srgb","components":{"red":"0x3A","green":"0x9A","blue":"0x76","alpha":"1.000"}}}]}
JSON
xcrun actool "$ASSETS" --compile "$APP/Contents/Resources" --platform macosx --minimum-deployment-target 14.0 \
  --accent-color AccentColor --output-partial-info-plist "$(mktemp -d)/assets.plist" >/dev/null

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>WorkTool</string>
  <key>CFBundleDisplayName</key><string>WorkTool</string>
  <key>CFBundleIdentifier</key><string>com.chowonki.worktool</string>
  <key>CFBundleExecutable</key><string>WorkTool</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>NSAccentColorName</key><string>AccentColor</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>CFBundleDevelopmentRegion</key><string>ko</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.business</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>WorkTool · Pretendard (SIL OFL 1.1)</string>
</dict></plist>
PLIST

# 서명(개인 배포용 ad-hoc). 개발자 인증서가 있으면 SIGN_ID로 지정하세요.
codesign --force --deep --options runtime --sign "${SIGN_ID:--}" "$APP"
codesign --verify --deep --strict "$APP"
(cd dist && rm -f WorkTool-macOS.zip && ditto -c -k --keepParent WorkTool.app WorkTool-macOS.zip)
du -sh "$APP" dist/WorkTool-macOS.zip
