#!/usr/bin/env python3
"""WorkTool 웹 저장소에서 브라우저와 상관없는 규칙·엑셀 라이브러리만 뽑아 앱 리소스로 복사해요.

사용법: python3 scripts/sync-rules.py <WorkTool 저장소 폴더>
웹(dispatch.html·transfer.html)의 규칙을 바꾼 뒤 다시 실행하세요.
"""
import re, shutil, sys
from pathlib import Path

if len(sys.argv) != 2:
    sys.exit(__doc__)
web = Path(sys.argv[1]).resolve()
app = Path(__file__).resolve().parent.parent
out = app / 'Sources/WorkToolCore/Resources'
(out / 'js').mkdir(parents=True, exist_ok=True)
(out / 'Fonts').mkdir(parents=True, exist_ok=True)

def between(text, start, end):
    a = text.find(start)
    b = -1 if a < 0 else text.find(end, a + len(start))
    if b < 0:
        sys.exit(f'규칙을 찾지 못했어요: "{start}" ~ "{end}". 웹 코드가 바뀌었는지 확인해 주세요.')
    return text[a:b]

def script(html, pattern):
    m = re.search(pattern, html, re.S)
    if not m:
        sys.exit(f'스크립트를 찾지 못했어요: {pattern}')
    return m.group(1)

dispatch = (web / 'dispatch.html').read_text(encoding='utf-8')
transfer = (web / 'transfer.html').read_text(encoding='utf-8')

# 1) 엑셀 읽기·쓰기·암호 풀기 라이브러리 (웹과 같은 파일)
(out / 'js/excel.js').write_text(script(dispatch, r'<script id="xlsx-lib">(.*?)</script>'), encoding='utf-8')

# 2) 탁송 규칙 + 웹의 엑셀 저장 함수
core = script(dispatch, r'<script id="core-source"[^>]*>(.*?)</script>')
dapp = [s for s in re.findall(r'<script>(.*?)</script>', dispatch, re.S) if 'const C=DispatchCore' in s]
if not dapp:
    sys.exit('탁송 화면 스크립트를 찾지 못했어요.')
excel_fn = between(dapp[0], 'async function excelBlob(', 'function pdfBlob(')
(out / 'js/dispatch.js').write_text(core + '\n;(()=>{const C=DispatchCore;\n' + excel_fn +
    '\nglobalThis.DispatchExport={excelBlob};})();\n', encoding='utf-8')

# 3) 이체 규칙: 원문 해석기(+ 웹의 엑셀 작성기) + 화면 스크립트의 검사·분할·코멘트·파일 함수
scripts = re.findall(r'<script>(.*?)</script>', transfer, re.S)
parser = next((s for s in scripts if 'function parseOne(' in s), None)
tapp = next((s for s in scripts if 'function check(r)' in s), None)
if not parser or not tapp:
    sys.exit('이체 스크립트를 찾지 못했어요.')
pieces = [
    between(tapp, 'const MAX=', 'function toast('),
    between(tapp, '// ---------- 검증', '// ---------- 1단계'),
    between(tapp, 'function amountText(', 'function renderComments('),
    between(tapp, 'function fileRows(', "$('download').onclick"),
]
for need in ['function check(r)', 'function splitRow(r)', 'function buildComments()', 'function xlsxBlob(', 'function fileName(stamp)', 'function sentKey(r)']:
    if not any(need in p for p in pieces):
        sys.exit(f'이체 규칙에서 {need}를 찾지 못했어요.')
glue = (app / 'scripts/transfer-glue.js').read_text(encoding='utf-8')
(out / 'js/transfer.js').write_text(parser + '\n;(()=>{\n' + '\n'.join(pieces) + '\n' + glue + '\n})();\n', encoding='utf-8')

# 4) 변경 이력과 글꼴
shutil.copy(web / 'CHANGELOG.md', out / 'CHANGELOG.md')
fonts = web / 'windows/WorkTool.WinUI/Assets/Fonts'
for f in ['Pretendard-Regular.otf', 'Pretendard-SemiBold.otf', 'Pretendard-Bold.otf', 'Pretendard-LICENSE.txt']:
    if (fonts / f).exists():
        shutil.copy(fonts / f, out / 'Fonts' / f)

rev = ''
head = web / '.git/HEAD'
try:
    import subprocess
    rev = subprocess.run(['git', '-C', str(web), 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip()
except Exception:
    pass
(out / 'rules-version.txt').write_text(rev or 'unknown', encoding='utf-8')
print(f'규칙을 동기화했어요 (WorkTool {rev or "?"}) → {out}')
