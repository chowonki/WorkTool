# WorkTool for Mac

[WorkTool](https://github.com/chowonki/WorkTool)의 **탁송체크**와 **하나은행 이체파일**을 Mac 앱으로 쓰는 프로젝트예요. 화면은 SwiftUI로 새로 만들었고 WebView나 브라우저를 쓰지 않아요. 업무 규칙과 엑셀 처리는 **웹과 같은 JavaScript를 macOS 내장 JavaScriptCore에서 실행**해서, 결과가 웹과 같아요.

입력과 파일은 이 Mac 안에서만 처리하고 서버로 보내지 않아요. 앱이 인터넷에 접속하는 곳은 변경 이력 화면의 ‘최신 내용 불러오기’(GitHub)뿐이에요. 이 앱은 송금을 실행하지 않아요.

## 설치

1. [릴리스](https://github.com/chowonki/WorkTool/releases)의 `WorkTool-macOS.zip`을 풀고 `WorkTool.app`을 ‘응용 프로그램’ 폴더로 옮겨요.
2. 처음 열 때 “확인되지 않은 개발자” 경고가 나오면 앱을 **Control-클릭 → 열기**로 한 번 열어요(개인 배포용 ad-hoc 서명이에요).

macOS 14 Sonoma 이상, Apple Silicon·Intel 모두 지원해요.

## 기능

| 화면 | 내용 |
| --- | --- |
| 탁송체크 | 차량목록 붙여넣기(빈 줄 자동 정리)·정리, 업체 .xlsx 끌어다 놓기(암호 파일 포함), 최신 기록 대조, 사이드바 보기(대수 표시)·검색·업체·상태 필터, 정렬, 중첩 일정·탁송비용 고르기, 완료 표시, 차량번호 복사(슬랙봇 문의는 제주·일반 구분), 결과 캡처(클립보드·PNG), 엑셀·PDF·TXT 저장, 슬랙봇 회신 반영 |
| 하나은행 이체파일 | 스레드 여러 개 입력·추출(⌘↩), 차량번호 없는 건 입력 안내, 1억 초과 자동 분할, 은행(하나은행 코드표)·계좌·금액·차량번호·통장 메모·CMS 수정, 원문 대조, 일괄 입력, 오류·검토 필터, 미리보기, 검토 확인 후 다운로드, 오늘 받은 건 중복 경고, 송금 코멘트 복사, 받지 않고 종료할 때 확인 |
| 사용법·변경 이력 | 두 도구 사용 순서와 조회 기준, CHANGELOG(앱 내장 + GitHub 최신) |

- 탁송체크의 ‘복사함·완료’ 표시는 오늘 하루 이 Mac에 남아요. 이체 지문(은행·계좌·금액을 섞은 값)만 오늘 하루 남기고 계좌번호 원문은 남기지 않아요.
- 화면: macOS 27 Golden Gate 스타일 — 전체 높이 사이드바(보기·단계), 도구 막대, 표, 오른쪽 입력·정보 패널. 색 아이콘·상태 알약은 라이트·다크 모두 맞춰요.
- 화면 모드: 보기 메뉴 → 화면(자동·라이트·다크). 이동: ⌘0 홈 · ⌘1 탁송체크 · ⌘2 이체파일.

## 구조

```
Sources/WorkToolCore/      업무 규칙 엔진(JavaScriptCore)·엑셀·캡처·PDF — 화면과 상관없음
  Resources/js/            웹에서 뽑은 규칙 (scripts/sync-rules.py가 만들어요)
    excel.js               웹 dispatch.html의 엑셀 라이브러리(읽기·쓰기·암호 풀기)
    dispatch.js            DispatchCore + 웹 엑셀 저장 함수
    transfer.js            이체 원문 해석기 + 검사·분할·코멘트·파일명·엑셀 작성기 + Mac 연결부
  Resources/Fonts/         Pretendard (SIL OFL 1.1)
Sources/WorkTool/          SwiftUI 앱
Sources/worktool-check/    가상 자료 검증 도구
scripts/                   규칙 동기화·앱 빌드
```

JavaScript에는 DOM·파일·네트워크 접근을 주지 않아요. 엑셀 파일 내용과 결과만 주고받아요.

## 웹 규칙이 바뀌었을 때

```bash
# 저장소의 mac/ 폴더에서
python3 scripts/sync-rules.py ..
swift run worktool-check ..                   # 가상 자료(scripts/fixtures)로 검증
./scripts/build-app.sh                        # dist/WorkTool.app, dist/WorkTool-macOS.zip
```

`sync-rules.py`는 웹 코드에서 필요한 함수를 이름으로 찾아 복사해요. 웹 구조가 바뀌어 찾지 못하면 멈추고 어떤 함수인지 알려 줘요.

## 빌드

Xcode 16 이상(또는 Swift 6 도구)이 필요해요.

```bash
swift build                 # 개발용
swift run WorkTool          # 바로 실행
./scripts/build-app.sh      # 배포용 앱 (VERSION=0.3.0, SIGN_ID="Developer ID Application: …" 지정 가능)
```

확인용 실행 옵션(가상 자료):

```bash
.build/debug/WorkTool --demo-dispatch ../scripts/fixtures
.build/debug/WorkTool --demo-transfer --step 2
```

## 검증

`swift run worktool-check <WorkTool 폴더>`가 확인하는 것:

- 3사 가상 엑셀 읽기, 암호 파일 읽기와 틀린 암호 거부(windows 테스트의 `encrypted.xlsx`)
- 차량목록 인식, 대조 결과(상태·금액·일정·비용·메모), 중첩 일정 고르기, 슬랙봇 회신 반영
- TXT(BOM 포함)·엑셀·캡처 PNG·PDF 만들기
- 이체 추출·1억 분할·검사·코멘트·파일명·이체 엑셀

결과 파일은 `check-output/`에 남아요.

라이선스: Pretendard SIL OFL 1.1 (`Resources/Fonts/Pretendard-LICENSE.txt`). 웹에서 가져온 엑셀 라이브러리(xlsx-populate·JSZip 등)는 각 라이선스를 따라요.
