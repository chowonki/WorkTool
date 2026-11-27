import SwiftUI
import AppKit
import WorkToolCore

@main
struct WorkToolApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    @StateObject private var dispatch = DispatchModel()
    @StateObject private var transfer = TransferModel()
    @AppStorage("appearance") private var appearance = "system"
    @State private var page: Page? = .home

    init() { AppResources.registerFonts() }

    var body: some Scene {
        WindowGroup("WorkTool") {
            ContentView(page: $page, dispatch: dispatch, transfer: transfer)
                .font(Theme.body)
                .tint(Theme.accent)
                .preferredColorScheme(appearance == "light" ? .light : appearance == "dark" ? .dark : nil)
                .frame(minWidth: 1080, minHeight: 680)
                .onAppear { appDelegate.transfer = transfer }
        }
        .defaultSize(width: 1520, height: 940)
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandMenu("이동") {
                Button("홈") { page = .home }.keyboardShortcut("0")
                Button("탁송체크") { page = .dispatch }.keyboardShortcut("1")
                Button("하나은행 이체파일") { page = .transfer }.keyboardShortcut("2")
                Button("사용법") { page = .guide }.keyboardShortcut("3")
                Button("변경 이력") { page = .changelog }.keyboardShortcut("4")
            }
            CommandGroup(after: .toolbar) {
                Picker("화면", selection: $appearance) {
                    Text("자동").tag("system"); Text("라이트").tag("light"); Text("다크").tag("dark")
                }
            }
        }
    }
}

/// 받지 않은 이체 내역이 있으면 종료 전에 한 번 물어봐요.
final class AppDelegate: NSObject, NSApplicationDelegate {
    weak var transfer: TransferModel?
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    @MainActor func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let t = transfer, t.hasUnsaved else { return .terminateNow }
        return UI.confirm("받지 않은 이체 내역이 있어요", "이체 내역 \(t.rows.count)건을 아직 파일로 받지 않았어요. 종료하면 입력 내용이 지워져요.", ok: "종료", destructive: true)
            ? .terminateNow : .terminateCancel
    }
}

enum Page: String, Hashable, CaseIterable {
    case home, dispatch, transfer, guide, changelog
    var title: String {
        switch self {
        case .home: "홈"; case .dispatch: "탁송체크"; case .transfer: "하나은행 이체파일"; case .guide: "사용법"; case .changelog: "변경 이력"
        }
    }
    var icon: String {
        switch self {
        case .home: "house.fill"; case .dispatch: "car.2.fill"; case .transfer: "banknote.fill"; case .guide: "book.fill"; case .changelog: "clock.arrow.circlepath"
        }
    }
    var color: Color {
        switch self {
        case .home: Theme.indigo; case .dispatch: Theme.accent; case .transfer: Theme.info; case .guide: .orange; case .changelog: Theme.purple
        }
    }
}

/// 사이드바 항목: 화면, 탁송체크 보기, 이체파일 단계
enum NavItem: Hashable {
    case page(Page)
    case filter(String)
    case step(Int)
}

struct ContentView: View {
    @Binding var page: Page?
    @ObservedObject var dispatch: DispatchModel
    @ObservedObject var transfer: TransferModel

    static let filterIcons: [String: (String, Color)] = [
        "all": ("tray.full.fill", Theme.teal), "review": ("exclamationmark.circle.fill", Theme.warn), "confirmed": ("checkmark.circle.fill", Theme.ok),
        "progress": ("clock.fill", Theme.progress), "missing": ("questionmark.circle.fill", Theme.bad), "jeju": ("airplane", Theme.info),
        "carrier": ("box.truck.fill", Theme.brown), "slack": ("bubble.left.fill", Theme.purple), "slack-replies": ("arrowshape.turn.up.left.fill", Theme.pink),
    ]

    /// 사이드바 선택은 지금 화면·보기·단계에서 바로 계산해요.
    private var selection: Binding<NavItem?> {
        Binding(get: {
            switch page ?? .home {
            case .dispatch: .filter(dispatch.filter)
            case .transfer: .step(transfer.step)
            case let p: .page(p)
            }
        }, set: { item in
            switch item {
            case .page(let p)?: page = p
            case .filter(let f)?: page = .dispatch; if dispatch.filter != f { dispatch.filter = f; dispatch.selection = [] }
            case .step(let n)?:
                page = .transfer
                if n == 3 { transfer.goStep3() }
                else if n == 2 && transfer.rows.isEmpty { transfer.toaster.show("먼저 이체정보를 추출해 주세요.") }
                else { transfer.step = n }
            case nil: break
            }
            // 이동이 거절되거나 같은 화면을 다시 눌러도 사이드바 선택을 실제 화면에 맞게 다시 그려요.
            DispatchQueue.main.async { dispatch.objectWillChange.send(); transfer.objectWillChange.send() }
        })
    }

    var body: some View {
        NavigationSplitView {
            List(selection: selection) {
                pageRow(.home)
                Section("업무 도구") {
                    pageRow(.dispatch)
                    ForEach(DispatchModel.filterTitles.filter { $0.0 != "slack-replies" || dispatch.hasSlackReplies }, id: \.0) { key, title in
                        let icon = Self.filterIcons[key] ?? ("circle", .secondary)
                        childRow(title, icon: icon.0, color: icon.1)
                            .badge(dispatch.results.isEmpty ? 0 : key == "all" ? dispatch.distinctCount : dispatch.count(key))
                            .tag(NavItem.filter(key))
                    }
                    pageRow(.transfer)
                    ForEach([(1, "이체 요청 입력"), (2, "확인·수정"), (3, "다운로드")], id: \.0) { n, title in
                        let done = n < transfer.step || (n == 1 && !transfer.rows.isEmpty)
                        Label {
                            Text(title)
                        } icon: {
                            Image(systemName: done && transfer.step != n ? "checkmark.circle.fill" : "\(n).circle.fill")
                                .foregroundStyle(done && transfer.step != n ? Theme.ok : [Theme.info, Theme.indigo, Theme.purple][n - 1])
                        }
                        .padding(.leading, 14)
                        .badge(n == 2 && !transfer.rows.isEmpty ? transfer.rows.count : 0)
                        .tag(NavItem.step(n))
                    }
                }
                Section("도움말") {
                    pageRow(.guide)
                    pageRow(.changelog)
                }
            }
            .listStyle(.sidebar)
            .navigationSplitViewColumnWidth(min: 200, ideal: 224, max: 280)
        } detail: {
            switch page ?? .home {
            case .home: HomeView(page: $page)
            case .dispatch: DispatchView(model: dispatch)
            case .transfer: TransferView(model: transfer)
            case .guide: GuideView()
            case .changelog: ChangeLogView()
            }
        }
        .task { await demoFromArguments() }
    }

    private func pageRow(_ p: Page) -> some View {
        Label { Text(p.title) } icon: { IconTile(symbol: p.icon, color: p.color) }
            .tag(NavItem.page(p))
    }

    private func childRow(_ title: String, icon: String, color: Color) -> some View {
        Label { Text(title) } icon: { Image(systemName: icon).foregroundStyle(color) }
            .padding(.leading, 14)
    }

    /// 확인용 실행 옵션: --demo-dispatch <가상 자료 폴더> / --demo-transfer / --open <page> / --step <n>
    private func demoFromArguments() async {
        let args = ProcessInfo.processInfo.arguments
        func value(_ k: String) -> String? { args.firstIndex(of: k).flatMap { $0 + 1 < args.count ? args[$0 + 1] : nil } }
        if let dir = value("--demo-dispatch") {
            let d = URL(fileURLWithPath: dir)
            dispatch.input = (try? String(contentsOf: d.appendingPathComponent("차량목록.txt"), encoding: .utf8)) ?? ""
            await dispatch.addFiles(["한빛탁송_가상.xlsx", "오픈마일_가상.xlsx", "렌톡_가상.xlsx"].map { d.appendingPathComponent($0) })
            dispatch.lookup()
            page = .dispatch
        }
        if args.contains("--demo-transfer") {
            transfer.threads[0].text = "현대 쏘나타 123가4567\n국민은행\n123-456-789012\n35,000원\n\n기아 K5 45나6789\n신한은행\n110-123-456789\n1억 2천만원\n\n(압류 계좌)\n우리은행\n1002-345-678901\n80,000원"
            transfer.extract()
            transfer.askPlates = []
            page = .transfer
            if let s = value("--step"), let n = Int(s) { if n == 3 { transfer.goStep3() } else { transfer.step = n } }
        }
        if let p = value("--open"), let pg = Page(rawValue: p) { page = pg }
    }
}

struct HomeView: View {
    @Binding var page: Page?
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                HStack(spacing: 14) {
                    if let icon = NSImage(named: "AppIcon") ?? NSApp.applicationIconImage {
                        Image(nsImage: icon).resizable().frame(width: 52, height: 52)
                    }
                    VStack(alignment: .leading, spacing: 3) {
                        Text("업무 도구").font(Theme.font(24, .bold))
                            .foregroundStyle(LinearGradient(colors: [Theme.accent, Theme.teal, Theme.info], startPoint: .leading, endPoint: .trailing))
                        Text("넣은 파일과 내용은 이 Mac 안에서만 처리하고 서버로 보내지 않아요.").foregroundStyle(.secondary)
                    }
                }
                .padding(.bottom, 22)
                Divider()
                tool(title: "탁송체크", page: .dispatch,
                     text: "차량목록과 한빛탁송·오픈마일·렌톡 엑셀을 대조해 완료 차량의 금액, 진행 중인 차량, 제주 출발 차량을 정리해요.",
                     steps: ["차량목록을 붙여넣어요.", "업체 .xlsx 파일을 첨부해요. 암호 파일도 돼요.", "차량 조회하기를 누르고 결과를 캡처해 Slack에 붙여넣거나 TXT·엑셀·PDF로 받아요."],
                     button: "탁송체크 열기", shortcut: "⌘1")
                Divider()
                tool(title: "하나은행 이체파일", page: .transfer,
                     text: "Slack 스레드를 붙여넣으면 은행·계좌번호·금액·차량번호를 뽑아 하나은행 다계좌이체 엑셀로 만들어요.",
                     steps: ["이체 요청 입력: 스레드를 붙여넣고 추출해요.", "확인·수정: 은행을 고르고 오류와 검토 필요 건을 정리해요.", "다운로드: 미리보기를 확인하고 엑셀을 받아요."],
                     button: "이체파일 열기", shortcut: "⌘2")
                Divider()
                Label("이체파일은 은행에 올리기 전에 금액과 계좌를 원문과 꼭 대조해 주세요.", systemImage: "exclamationmark.triangle.fill")
                    .symbolRenderingMode(.multicolor)
                    .padding(.vertical, 16)
                Text("규칙 버전: WorkTool \(AppResources.rulesVersion) · 웹과 같은 규칙을 Mac 안에서 실행해요.").font(Theme.small).foregroundStyle(.tertiary)
            }
            .padding(.horizontal, 36).padding(.vertical, 30)
            .frame(maxWidth: 820, alignment: .leading)
        }
        .navigationTitle("홈")
    }

    private func tool(title: String, page target: Page, text: String, steps: [String], button: String, shortcut: String) -> some View {
        HStack(alignment: .top, spacing: 16) {
            IconTile(symbol: target.icon, color: target.color, size: 40)
            VStack(alignment: .leading, spacing: 8) {
                Text(title).font(Theme.font(16, .semibold))
                Text(text).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                VStack(alignment: .leading, spacing: 3) {
                    ForEach(Array(steps.enumerated()), id: \.offset) { i, s in
                        Text("\(i + 1). \(s)").foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
            Spacer(minLength: 20)
            VStack(alignment: .trailing, spacing: 4) {
                Button(button) { page = target }.buttonStyle(.borderedProminent).tint(target.color)
                Text(shortcut).font(Theme.small).foregroundStyle(.tertiary)
            }
        }
        .padding(.vertical, 20)
    }
}
