#!/usr/bin/env python3
"""웹(dispatch.html·transfer.html)의 규칙을 Flutter 앱 assets로 복사해요.

사용법: python3 windows-flutter/tool/sync_rules.py   (저장소 어디서 실행해도 돼요)
Mac 앱과 같은 추출 스크립트(mac/scripts/sync-rules.py)를 써서 웹·Mac·Windows가 같은 규칙을 써요.
"""
import shutil, subprocess, sys
from pathlib import Path

app = Path(__file__).resolve().parent.parent
repo = app.parent
mac = repo / 'mac'
subprocess.run([sys.executable, str(mac / 'scripts/sync-rules.py'), str(repo)], check=True)
res = mac / 'Sources/WorkToolCore/Resources'
for f in ['excel.js', 'dispatch.js', 'transfer.js']:
    shutil.copy(res / 'js' / f, app / 'assets/js' / f)
shutil.copy(repo / 'CHANGELOG.md', app / 'assets/CHANGELOG.md')
fonts = repo / 'windows/WorkTool.WinUI/Assets/Fonts'
if not fonts.exists():
    fonts = res / 'Fonts'
for f in ['Pretendard-Regular.otf', 'Pretendard-SemiBold.otf', 'Pretendard-Bold.otf', 'Pretendard-LICENSE.txt']:
    if (fonts / f).exists():
        shutil.copy(fonts / f, app / 'assets/fonts' / f)
# Mac 쪽 rules-version.txt는 Mac 커밋 때만 바꿔요.
subprocess.run(['git', '-C', str(repo), 'checkout', '--', 'mac/Sources/WorkToolCore/Resources/rules-version.txt'], capture_output=True)
print(f'Flutter 앱 규칙을 동기화했어요 → {app / "assets"}')
