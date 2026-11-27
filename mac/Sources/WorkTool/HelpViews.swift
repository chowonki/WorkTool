import SwiftUI
import WorkToolCore

struct GuideView: View {
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                Text("사용법").font(Theme.font(24, .bold))
                Divider()
                VStack(alignment: .leading, spacing: 10) {
                    Label { Text("탁송체크") } icon: { Image(systemName: Page.dispatch.icon).foregroundStyle(Page.dispatch.color) }.font(Theme.font(16, .semibold))
                    steps([
                        "사내 ‘탁송출발 주유비 미입력’ 스크립트를 실행하고, 결과에서 차량번호를 추출·복사해요.",
                        "차량목록 입력에 붙여넣어요. 날짜·시간이 함께 있는 목록도 그대로 쓸 수 있어요.",
                        "한빛탁송·오픈마일·렌톡에서 ‘탁송기한’ 조회 → 기간 설정 → 조회 → 시트 다운로드를 해요. 렌톡은 요청시각이 아닌 출발시각 기준으로 조회해요.",
                        "받은 3사 엑셀 시트를 업체 파일 칸에 끌어다 놓거나 ‘파일 선택…’으로 넣어요. 암호 파일은 암호를 물어봐요(저장하지 않아요).",
                        "차량 조회하기(⌘↩)를 누르고 ‘확인 필요’ 보기부터 살펴본 뒤, 결과 캡처로 Slack에 붙여넣거나 차량번호 복사·TXT·엑셀·PDF로 저장해요.",
                    ])
                    Text("조회 기준").font(Theme.font(13, .semibold)).padding(.top, 6)
                    bullets([
                        "차량번호로만 맞춰요. 차량목록의 날짜·시간은 표시용이에요.",
                        "같은 차량의 기록이 여러 개면 업체와 상관없이 탁송일자가 가장 최신인 기록 하나를 써요. 그 기록이 탁송완료일 때만 금액을 확정해요.",
                        "주유비용/충전비용: 한빛탁송 ‘주유/부대비용 총액’(빈칸은 0원), 오픈마일 ‘주유비’(빈칸은 미입력). 10원 단위로 반올림해요.",
                        "렌톡은 금액 없이 슬랙봇 문의 목록으로 보여요. J열 Y는 캐리어탁송(문의 제외), N은 일반탁송, 빈칸은 확인 필요예요.",
                        "같은 차량의 기록끼리 탁송일정·탁송비용이 다르면 칸의 메뉴에서 직접 골라요. 고르지 않으면 저장하지 않아요.",
                        "표에서 줄을 고르면(⌘·⇧ 클릭) 복사·저장·캡처가 고른 줄만 대상으로 해요. 값은 두 번 눌러 복사할 수 있어요(숫자는 쉼표 없이).",
                        "슬랙봇 회신 반영: ‘차량번호 - 값’을 줄마다 붙여넣어요. X는 0원, ‘확인 중’은 텍스트로 기록해요.",
                    ])
                }
                Divider()
                VStack(alignment: .leading, spacing: 10) {
                    Label { Text("하나은행 이체파일") } icon: { Image(systemName: Page.transfer.icon).foregroundStyle(Page.transfer.color) }.font(Theme.font(16, .semibold))
                    steps([
                        "Slack 스레드를 입력칸에 붙여넣어요. 스레드가 여러 개면 ‘입력칸 추가’로 나눠 넣어요.",
                        "이체정보 추출하기(⌘↩)를 누르면 2단계로 넘어가요.",
                        "은행·계좌번호·금액·차량번호를 원문과 대조하고, 빨간 칸(오류)을 고쳐요. 줄을 고르면 오른쪽에 원문이 보여요. 필요하면 통장 메모와 CMS코드를 넣어요.",
                        "3단계에서 미리보기를 확인하고 ‘하나은행 이체파일 다운로드’를 눌러요. 송금 코멘트도 복사할 수 있어요.",
                    ])
                    bullets([
                        "1억을 넘는 금액은 1억씩 나눠 여러 줄로 만들어요(‘분할’ 표시).",
                        "오늘 이미 받은 이체파일과 같은 은행·계좌·금액이 있으면 한 번 더 물어봐요. 계좌번호 원문은 남기지 않아요.",
                        "입력 내용은 앱을 닫으면 지워져요. 받지 않은 내역이 있으면 종료 전에 물어봐요. 이 앱은 송금을 실행하지 않아요.",
                    ])
                }
            }
            .padding(.horizontal, 36).padding(.vertical, 30)
            .frame(maxWidth: 860, alignment: .leading)
        }
        .navigationTitle("사용법")
    }

    private func steps(_ list: [String]) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            ForEach(Array(list.enumerated()), id: \.offset) { i, s in
                HStack(alignment: .top, spacing: 8) {
                    Text("\(i + 1)").font(Theme.font(11, .bold)).foregroundStyle(.white).frame(width: 18, height: 18).background(Circle().fill(Theme.accent))
                    Text(s).fixedSize(horizontal: false, vertical: true)
                }
            }
        }
    }

    private func bullets(_ list: [String]) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            ForEach(list, id: \.self) { s in
                HStack(alignment: .top, spacing: 6) { Text("•").foregroundStyle(Theme.accent); Text(s).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true) }
            }
        }
    }
}

struct ChangeLogView: View {
    @State private var text = AppResources.text("CHANGELOG.md")
    @State private var source = "앱에 들어 있는 변경 이력"
    @State private var loading = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Text("변경 이력").font(Theme.font(24, .bold))
                    Spacer()
                    Text(source).font(Theme.small).foregroundStyle(.secondary)
                    Button { Task { await refresh() } } label: { Label("최신 내용 불러오기", systemImage: "arrow.clockwise") }.disabled(loading)
                }
                ForEach(Array(blocks.enumerated()), id: \.offset) { _, b in b }
            }
            .padding(.horizontal, 36).padding(.vertical, 30)
            .frame(maxWidth: 860, alignment: .leading)
            .textSelection(.enabled)
        }
        .navigationTitle("변경 이력")
        .task { await refresh() }
    }

    private var blocks: [AnyView] {
        text.components(separatedBy: "\n").compactMap { line -> AnyView? in
            let t = line.trimmingCharacters(in: .whitespaces)
            if t.isEmpty || t.hasPrefix("# ") || t.hasPrefix("[웹에서 보기]") { return nil }
            if t.hasPrefix("## ") {
                return AnyView(Text(t.dropFirst(3)).font(Theme.font(15, .semibold)).padding(.top, 14))
            }
            if t.hasPrefix("- ") {
                return AnyView(HStack(alignment: .top, spacing: 6) {
                    Text("•").foregroundStyle(Theme.accent)
                    Text(inline(String(t.dropFirst(2)))).fixedSize(horizontal: false, vertical: true)
                })
            }
            return AnyView(Text(inline(t)).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true))
        }
    }

    private func inline(_ s: String) -> AttributedString {
        (try? AttributedString(markdown: s, options: .init(interpretedSyntax: .inlineOnlyPreservingWhitespace))) ?? AttributedString(s)
    }

    /// GitHub에서 최신 CHANGELOG.md를 받아요. 안 되면 앱에 든 내용을 그대로 보여줘요.
    private func refresh() async {
        loading = true; defer { loading = false }
        guard let url = URL(string: "https://raw.githubusercontent.com/chowonki/WorkTool/main/CHANGELOG.md") else { return }
        var req = URLRequest(url: url, timeoutInterval: 8)
        req.cachePolicy = .reloadIgnoringLocalCacheData
        if let (data, res) = try? await URLSession.shared.data(for: req), (res as? HTTPURLResponse)?.statusCode == 200,
           let s = String(data: data, encoding: .utf8), s.contains("## ") {
            text = s; source = "GitHub 최신 · \(UI.stamp("HH:mm"))"
        }
    }
}
