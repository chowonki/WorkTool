# WorkTool for Windows

C++/WinRT와 WinUI 3로 만든 Windows x64 데스크톱 앱이에요. Windows 기본 제목 표시줄, Mica 배경, 왼쪽 탐색 창과 시스템·라이트·다크 화면 모드를 사용해요.

## 실행

포터블 ZIP 전체를 풀고 맨 위의 `WorkTool.exe`를 실행하세요. `App`에는 실행 파일과 Windows App SDK 런타임이 들어 있어요. `Data`에는 화면 모드, 오늘의 탁송 표시와 이체파일 중복 확인 기록을 저장해요. 포터블 앱을 업데이트할 때에는 `App`과 `WorkTool.exe`를 교체하고 `Data` 폴더를 이어서 사용하세요.

## 기능

- 탁송체크에서 차량목록과 한빛탁송·오픈마일·렌톡 `.xlsx`를 대조해요. 암호 파일, 끌어다 놓기, 상태·업체 필터, 검색, 일정·비용 중첩 선택을 지원해요.
- Slack 회신의 `X`는 0원으로, `확인 중`은 그대로 반영해요. 결과 표의 차량번호·일정·금액을 눌러 각 값을 복사할 수 있어요. 선택한 차량을 우선으로 TXT·Excel·PNG·PDF를 저장하거나 이미지를 복사할 수 있어요.
- 하나은행 이체파일에서 여러 Slack 스레드를 차례로 추출하고 원문과 내역을 대조해요. 은행·계좌·금액·차량번호를 수정하거나 메모와 CMS 코드를 일괄 적용할 수 있어요. 1억 원 초과 분할, 100건 제한, 검토 확인, 당일 중복 확인, 송금 코멘트 복사와 Excel 미리보기를 지원해요.
- 차량목록, 이체 요청과 첨부 파일은 PC에서 처리해요. 당일 이체 기록에는 중복 확인용 지문과 저장 시각만 들어 있어요.

## 빌드 및 검증

Visual Studio 2022 또는 2026의 C++ 데스크톱 도구, Windows SDK 10.0.26100, x64 VC++ 재배포 파일, .NET 10 SDK(NuGet 복원용), CMake가 필요해요.

```powershell
./windows/build-native.ps1
./windows/build-portable.ps1
```

빌드 폴더는 `windows/artifacts/WorkTool-Windows-x64/`예요. 포터블 ZIP은 `windows/artifacts/WorkTool-Portable-Windows-x64.zip`에 생성돼요. 빌드 스크립트는 가상 업무 파일로 조회·암호 처리·Slack 회신·이체 분할과 출력 파일을 검사해요.

```powershell
./windows/artifacts/WorkTool-Windows-x64/WorkTool.WinUI.exe --smoke-test "$PWD/windows/artifacts/native-smoke" "$PWD"
node --test tests/*.test.cjs
```

`WorkTool.Native`는 창, 화면, 파일 선택·저장, 클립보드와 캡처를 C++로 구현해요. 웹에서 추출한 검증·계산 규칙과 Excel 처리는 기존 QuickJS-ng 엔진에서 실행해요. 엔진에 브라우저, 파일 시스템 또는 네트워크 접근 권한은 제공하지 않아요. 웹 규칙을 바꾼 뒤에는 `python windows-flutter/tool/sync_rules.py`로 공용 리소스를 동기화하세요.

이전 C# 프로젝트는 `WorkTool.WinUI`에 있고 `./windows/build.ps1`로 빌드할 수 있어요. Flutter 프로젝트는 [별도 안내](../windows-flutter/README.md)를 참고하세요.
