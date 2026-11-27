# WorkTool for Windows (Flutter)

Flutter로 만든 Windows 데스크톱 앱이에요. 탁송체크와 하나은행 이체파일을 웹·Mac 앱과 같은 규칙으로 처리해요.

- Windows 10/11 x64. 압축을 풀고 `WorkTool.exe`를 실행해요 (포터블, 설치 없음).
- 모양: Windows 11 Fluent(Mica 배경, 왼쪽 탐색 창, 명령 막대, 오른쪽 입력 패널). 글꼴은 앱에 넣은 Pretendard예요.
- 입력과 파일은 이 PC 안에서만 처리하고 서버로 보내지 않아요. 앱은 송금을 실행하지 않아요.

## 구조

| 위치 | 내용 |
| --- | --- |
| `lib/core/engine.dart` | 규칙 JavaScript 엔진(FFI). 문자열만 주고받아요. |
| `lib/core/rules.dart` | 웹 규칙 호출 창구. 가벼운 계산은 화면 쪽, 엑셀 읽기·쓰기는 뒤쪽 isolate에서 해요. |
| `lib/core/exports.dart` | 결과 캡처(PNG)·PDF를 웹과 같은 배치로 그려요. |
| `lib/models/`, `lib/pages/` | 탁송체크·이체파일 화면과 상태 |
| `native/` | `worktool_js.dll`: [quickjs-ng](https://github.com/quickjs-ng/quickjs) v0.18.0(MIT) + `wtjs.c` |
| `assets/js/` | 웹(dispatch.html·transfer.html)에서 뽑은 규칙. 직접 고치지 마세요. |

- 규칙 엔진에는 파일·네트워크·OS 기능을 붙이지 않았어요 (quickjs-libc 없이 빌드).
- 암호 엑셀(Agile 암호화)의 10만 번 해시 반복은 `wtjs.c`의 SHA-512/SHA-1(`__nativeSpin`)로 해요. 나머지 암호 풀기·엑셀 읽기는 웹과 같은 JavaScript예요.
- 업체 파일은 `.xlsx`만 받아요 (웹·Mac과 같아요).

## 규칙 동기화

웹 규칙을 바꾼 뒤 실행하고 `assets/`를 커밋해요. CI가 웹과 다른지 확인해요.

```bash
python3 windows-flutter/tool/sync_rules.py
```

## 빌드 (Windows)

Flutter 3.50 이상, Visual Studio 2022(C++ 데스크톱), CMake가 필요해요. 첫 빌드 때 quickjs-ng 소스를 받아요(해시 고정).

```powershell
cd windows-flutter
flutter build windows --release
```

결과: `build/windows/x64/runner/Release/` 폴더 전체. 포터블 ZIP은 이 폴더에 `portable.mode`를 넣어 묶어요(설정·오늘 기록이 `Data` 폴더에 저장돼요).

GitHub Actions `Windows Flutter`가 빌드·검사·화면 캡처·포터블 ZIP(`WorkTool-Windows-x64.zip`)을 만들어요.

## 검사

```powershell
WorkTool.exe --smoke-test <결과 폴더> <WorkTool 저장소 폴더>
WorkTool.exe --demo <WorkTool 저장소 폴더> dispatch:all --shot shot.png --light
```

`--smoke-test`는 가상 자료(`scripts/fixtures`, 암호 파일 `windows/WorkTool.Tests/Fixtures/encrypted.xlsx`)로 엑셀 읽기·조회·내보내기·이체파일을 확인해요.

Mac에서 개발할 때는 엔진 라이브러리를 직접 빌드하고 `WORKTOOL_JS_LIB`로 알려 줘요.

```bash
clang -std=c11 -O2 -w -funsigned-char -D_GNU_SOURCE -shared -fPIC -I<quickjs-ng> native/wtjs.c <quickjs-ng>/{quickjs,libregexp,libunicode,dtoa}.c -o libworktool_js.dylib
```

## 라이선스

Pretendard: SIL OFL 1.1 (`assets/fonts/Pretendard-LICENSE.txt`). quickjs-ng: MIT. 엑셀 라이브러리(xlsx-populate, JSZip 등)는 웹과 같아요.
