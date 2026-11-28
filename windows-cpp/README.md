# WorkTool for Windows (C++ / WinUI 3)

[chowonki/WorkTool](https://github.com/chowonki/WorkTool)의 Windows 앱(`windows/`, C# WinUI 3)을 **C++/WinRT + WinUI 3**로 다시 만든 버전이에요. 화면 구성·문구·동작은 원본 C# 앱을 그대로 따르고, 업무 규칙은 웹과 같은 JavaScript를 앱 안에서 실행해요.

- 탁송체크: 차량목록 입력, 업체 엑셀(.xlsx·암호 파일) 첨부, 보기 필터·검색·정렬, 일정/비용 선택, 슬랙봇 회신 반영, 차량번호 복사, TXT·Excel·PNG·PDF 저장, 캡처 복사
- 하나은행 이체파일: Slack 요청 추출, 확인·수정, 1억 초과 분할, 중복 검토, 최대 100건 Excel 생성, 송금 코멘트
- 시스템/라이트/다크 화면 모드, 사용법, 변경 이력

입력과 파일은 이 기기 안에서만 처리하고 서버로 보내지 않아요. 앱은 송금을 실행하지 않아요.

## 빌드와 실행

Visual Studio 2026(C++ 데스크톱 개발, v145 도구 집합)과 Windows SDK가 필요해요. .NET, Node.js는 필요 없어요.

```powershell
./build.ps1          # build\Release\WorkTool.exe
./build.ps1 -Test    # 빌드 후 가상 자료로 화면 검증
```

`build\Release` 폴더 전체가 배포 단위예요(Windows App SDK 런타임 포함, 설치 필요 없음). 실행 파일 하나만 복사하면 실행되지 않아요. 코드 서명은 없어요.

## 구조

| 파일 | 내용 |
| --- | --- |
| `src/main.cpp` | 앱 진입점, 글꼴·강조색 리소스 (XAML 파일 없이 코드로만 만들어요) |
| `src/MainWindow.*`, `Colors.cpp` | 창, 탐색 창, 공용 도구, 홈·사용법·변경 이력·설정, 색 |
| `src/DispatchPage.cpp`, `DispatchOptions.cpp` | 탁송체크 |
| `src/TransferPage.cpp`, `TransferOptions.cpp` | 하나은행 이체파일 |
| `src/Rules.*`, `JsEngine.*` | 규칙 호출 창구와 quickjs-ng 엔진 래퍼 |
| `src/Spreadsheets.*` | .xlsx 쓰기(외부 라이브러리 없음), 이 기기에 남기는 작은 상태 파일 |
| `src/SmokeTest.cpp` | `--smoke-test <결과 폴더> <가상 자료 폴더>` 화면 검증 |
| `Rules/*.js` | 원본 저장소의 규칙(`windows/WorkTool.Core/Rules`)과 엑셀 읽기 라이브러리. 실행 파일 안에 리소스로 들어가요 |
| `third_party/quickjs` | quickjs-ng v0.18.0 소스 |

원본 C# 앱과 다른 점:

- 규칙 엔진이 Jint 대신 **quickjs-ng**예요. 파일·네트워크·OS 기능은 붙이지 않았어요.
- 업체 엑셀은 ExcelDataReader 대신 원본 Mac·Flutter 앱과 같은 `excel.js`로 읽어요. 그래서 **예전 `.xls` 형식은 읽지 못해요**(.xlsx와 암호 .xlsx만 지원). 암호 풀기의 해시 반복은 Windows CNG로 계산해요.
- 화면은 XAML 파일 없이 모두 C++ 코드로 만들어요.

웹 규칙이 바뀌면 원본 저장소에서 `node windows/sync-rules.cjs`를 돌린 뒤 `windows/WorkTool.Core/Rules/*.js`를 이 폴더의 `Rules/`로 복사하고 다시 빌드하세요.

## 라이선스 안내

quickjs-ng: MIT, Pretendard: SIL OFL 1.1 (`Licenses/`). `Rules/excel.js`에는 xlsx-populate(MIT)와 SheetJS cfb/crc32 등이 묶여 있어요. 배포할 때 함께 보관하세요.
