# WorkTool for Windows

WinUI 3 컨트롤로 작성한 Windows 데스크톱 앱입니다. Pretendard를 앱에 포함하며 헤더에 Windows 기본 최소화·최대화·닫기 단추를 통합합니다. 시스템 창 이동, 크기 조절, 최대화와 Snap Layouts를 유지합니다.

## 실행과 빌드

Windows 10 2004 이상 / Windows 11, x64 환경을 지원합니다. 배포 폴더 전체를 유지한 상태에서 `WorkTool.WinUI.exe`를 실행합니다. .NET과 Windows App SDK 런타임을 함께 배포합니다.

빌드에는 Windows, .NET 10 SDK 이상, Windows SDK 및 WinUI XAML 빌드 도구, Node.js가 필요합니다.

```powershell
./windows/build.ps1
```

결과는 `windows/artifacts/WorkTool-Windows-x64/`에 생성됩니다. ZIP 배포 시 폴더 전체를 압축하세요. 실행 파일 하나만 복사하면 실행되지 않습니다. 현재 배포물에는 코드 서명이 없습니다.

## 기능

- 탁송체크: 차량번호/일시 입력, 업체 .xlsx/.xls와 암호 Excel 첨부, 최신 기록 비교, 상태 필터와 번호 검색, 일정/비용 충돌 선택, 선택 범위, 완료 표시, 차량번호 복사, TXT/Excel/PNG/PDF 저장, 이미지 클립보드 복사
- 이체파일: Slack 요청 추가 추출, 원문 검토, 은행 선택, 계좌/금액/차량번호 수정, 통장 메모/CMS 일괄 적용, 추가/삭제, 1억 초과 분할, 중복 검토, 수정 후 확인 해제, 최대 100건 Excel 생성, 코멘트 복사, 미저장 종료 확인
- 시스템/라이트/다크 화면 모드와 내장 사용 방법

입력과 파일은 로컬 메모리에서 처리하며 서버에 전송하지 않습니다. 입력 내용은 앱 종료 시 지워집니다. 설정은 현재 실행 동안 적용됩니다. 앱에서 송금을 실행하지 않습니다.

## 구조와 검증

`WorkTool.WinUI`는 네이티브 UI, `WorkTool.Core`는 업무 규칙과 Excel 입출력, `WorkTool.Tests`는 실행형 회귀 검증입니다. WebView나 브라우저를 포함하지 않습니다. 기존 웹의 순수 JavaScript 규칙만 Jint에서 실행합니다. Jint에는 CLR/파일/네트워크 접근을 제공하지 않습니다.

웹 규칙을 바꾼 뒤 `node windows/sync-rules.cjs`로 내장 규칙을 동기화하세요. Windows 입력칸의 CR 줄바꿈은 LF로 변환합니다. ExcelDataReader로 업체 파일을 읽고 .NET ZIP/XML로 은행 양식의 6개 열을 생성합니다. 계좌번호는 문자열, 금액은 숫자로 기록합니다.

```powershell
dotnet run --project windows/WorkTool.Tests/WorkTool.Tests.csproj -c Release
node --test tests/*.test.cjs
```

`WorkTool.Tests/Fixtures/encrypted.xlsx`는 가상 차량 한 건으로 만든 암호 테스트 파일이며 암호는 `fixture-password`입니다. 실제 업무 자료가 없습니다.

PDF는 내장 글꼴로 렌더링한 결과 표를 A4 가로 페이지에 나눠 담는 이미지 PDF입니다. 캡처는 300대까지 지원합니다. 탁송 표의 차량번호·날짜·금액은 개별 복사할 수 있습니다. 탁송은 필터·정렬과 열 너비 대화 상자를 사용하고, 이체 확인은 상태와 열별로 검색할 수 있습니다. 실제 은행 업로드와 실제 업무 파일의 모든 변형은 별도 현장 검증이 필요합니다.

탁송체크는 자료 입력 → 결과 확인 → 슬랙봇 확인 → 복사·저장 순서로 진행합니다. 슬랙봇 확인에서는 `차량번호 - 값`을 줄마다 붙여넣고 확인 내용을 반영합니다. `X`는 0원, `1,234원` 또는 `1.234원`은 1234원, `확인 중`은 텍스트로 기록됩니다. 확인이 필요 없는 조회는 바로 복사·저장으로 이동할 수 있습니다.

결과 확인과 복사·저장 단계 간에 선택한 차량과 필터가 유지됩니다. 조회 표는 화면당 100대씩 표시하며, 좁은 창에서는 가로로 스크롤할 수 있습니다. ‘현재 표시 목록’ 저장 범위는 페이지와 관계없이 현재 필터·검색에 해당하는 모든 결과를 포함합니다. 체크한 차량이 있으면 체크한 줄이 우선합니다.

배포본 화면 검증(가상 자료):

```powershell
./windows/artifacts/WorkTool-Windows-x64/WorkTool.WinUI.exe --smoke-test "$PWD/windows/artifacts/ui-smoke" "$PWD/scripts/fixtures"
```

검증 결과와 가상 데이터 출력 파일은 지정한 폴더에 생성됩니다.

Pretendard: SIL OFL 1.1 (`WorkTool.WinUI/Assets/Fonts/Pretendard-LICENSE.txt`). Jint: BSD-2-Clause. ExcelDataReader: MIT. 배포 시 해당 패키지 라이선스를 함께 보관하세요.
