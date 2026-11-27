import SwiftUI
import WorkToolCore

struct TransferView: View {
    @ObservedObject var model: TransferModel
    @AppStorage("transfer-inspector") private var showInspector = true

    static let stepTitles = [1: "1단계 · 이체 요청 입력", 2: "2단계 · 확인·수정", 3: "3단계 · 다운로드"]

    var body: some View {
        Group {
            switch model.step {
            case 1: TransferInputStep(model: model)
            case 2: TransferReviewStep(model: model)
            default: TransferDownloadStep(model: model)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .navigationTitle("하나은행 이체파일")
        .navigationSubtitle(Self.stepTitles[model.step] ?? "")
        .toolbar { toolbar }
        .inspector(isPresented: $showInspector) {
            Group {
                switch model.step {
                case 1: TransferInputInspector(model: model)
                case 2: TransferReviewInspector(model: model)
                default: TransferDownloadInspector(model: model)
                }
            }
            .inspectorColumnWidth(min: 270, ideal: 320, max: 460)
        }
        .onChange(of: model.step, initial: true) { _, step in
            // 3단계의 검토 확인·다운로드 안내는 정보 패널에 있어서 늘 보여 줘요.
            if step == 3 { showInspector = true }
        }
        .overlay(ToastOverlay(toaster: model.toaster))
        .sheet(isPresented: Binding(get: { !model.askPlates.isEmpty }, set: { if !$0 { model.askPlates = [] } })) {
            AskPlatesSheet(model: model, rows: model.askPlates, warnings: model.askWarnings)
        }
    }

    @ToolbarContentBuilder private var toolbar: some ToolbarContent {
        ToolbarItemGroup(placement: .primaryAction) {
            Group {
                switch model.step {
                case 1:
                    Button { model.addThread() } label: { Label { Text("입력칸 추가") } icon: { Image(systemName: "plus.circle.fill").foregroundStyle(Theme.accent) } }
                case 2:
                    Button { model.step = 1 } label: { Label { Text("1단계로") } icon: { Image(systemName: "chevron.left.circle.fill").foregroundStyle(Theme.info) } }
                    Button { model.addRow() } label: { Label { Text("행 추가") } icon: { Image(systemName: "plus.circle.fill").foregroundStyle(Theme.accent) } }
                    Button { model.deleteSelected() } label: { Label { Text("삭제") } icon: { Image(systemName: "trash.fill").foregroundStyle(Theme.bad) } }.disabled(model.selection.isEmpty)
                default:
                    Button { model.step = 2 } label: { Label { Text("2단계에서 고치기") } icon: { Image(systemName: "pencil.circle.fill").foregroundStyle(Theme.warn) } }
                }
                Button { model.resetAll() } label: { Label { Text("처음부터") } icon: { Image(systemName: "arrow.counterclockwise.circle.fill").foregroundStyle(Theme.purple) } }
                    .disabled(model.rows.isEmpty && model.step == 1)
            }
            .labelStyle(.titleAndIcon)
        }
        ToolbarItem(placement: .primaryAction) {
            switch model.step {
            case 1:
                Button { model.extract() } label: { Text("이체정보 추출하기") }
                    .buttonStyle(ProminentToolbarStyle())
                    .keyboardShortcut(.return, modifiers: .command).help("⌘↩")
            case 2:
                Button { model.goStep3() } label: { Text("다운로드 단계로 →") }
                    .buttonStyle(ProminentToolbarStyle())
                    .keyboardShortcut(.return, modifiers: .command).help("⌘↩")
            default:
                Button { model.download() } label: { Label("이체파일 다운로드", systemImage: "square.and.arrow.down").labelStyle(.titleAndIcon) }
                    .buttonStyle(ProminentToolbarStyle())
                    .disabled(!model.canDownload)
            }
        }
        ToolbarItem(placement: .primaryAction) {
            Button { showInspector.toggle() } label: { Label("정보 패널", systemImage: "sidebar.trailing") }
                .help("오른쪽 정보 패널 보이기/숨기기")
        }
    }
}

// MARK: 1단계

struct TransferInputStep: View {
    @ObservedObject var model: TransferModel
    var body: some View {
        VStack(spacing: 0) {
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    ForEach(Array(model.threads.enumerated()), id: \.element.id) { i, t in
                        if i > 0 { Divider().padding(.vertical, 14) }
                        VStack(alignment: .leading, spacing: 8) {
                            HStack(spacing: 8) {
                                Text("이체 요청 \(i + 1)").font(Theme.font(13, .semibold))
                                if !t.imported.isEmpty && t.imported == t.text.trimmingCharacters(in: .whitespacesAndNewlines) {
                                    Badge(text: "추출함", color: Theme.ok)
                                }
                                Spacer()
                                Button { model.removeThread(t.id) } label: { Image(systemName: "trash") }
                                    .buttonStyle(.borderless).help(model.threads.count > 1 ? "입력칸 삭제" : "내용 지우기")
                            }
                            TextEditor(text: Binding(get: { model.threads.first { $0.id == t.id }?.text ?? "" },
                                                     set: { v in if let k = model.threads.firstIndex(where: { $0.id == t.id }) { model.threads[k].text = v } }))
                                .font(Theme.font(13.5))
                                .scrollContentBackground(.hidden)
                                .padding(6)
                                .frame(minHeight: model.threads.count == 1 ? 420 : 240)
                                .background(Color(nsColor: .textBackgroundColor), in: RoundedRectangle(cornerRadius: 6))
                                .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(Color.primary.opacity(0.12)))
                        }
                    }
                }
                .padding(20)
            }
            FooterBar {
                Text("Slack 스레드 내용을 그대로 붙여넣어요. 스레드가 여러 개면 입력칸을 추가해 나눠 넣어요.").lineLimit(1)
                Spacer()
                if !model.extractInfo.isEmpty { Text(model.extractInfo) }
                if !model.rows.isEmpty { Button("2단계로 (\(model.rows.count)건)") { model.step = 2 }.controlSize(.small) }
            }
        }
    }
}

struct TransferInputInspector: View {
    @ObservedObject var model: TransferModel
    var body: some View {
        InspectorScroll(title: "추출 설정", icon: "wand.and.stars", color: Theme.info) {
            VStack(alignment: .leading, spacing: 16) {
                VStack(alignment: .leading, spacing: 8) {
                    SectionHeader("CMS코드 (선택)", icon: "number", color: Theme.indigo)
                    TextField("새로 추출하는 모든 건에 넣어요", text: $model.cmsAll).textFieldStyle(.roundedBorder)
                }
                Divider()
                VStack(alignment: .leading, spacing: 8) {
                    SectionHeader("이렇게 추출해요", icon: "sparkles", color: Theme.purple)
                    ForEach(["은행·계좌번호·금액·차량번호를 줄마다 찾아요.", "1억을 넘는 금액은 1억씩 나눠요(‘분할’ 표시).", "차량번호가 없는 건은 추출 뒤 한 번 물어봐요.", "입력 내용은 앱을 닫으면 지워져요."], id: \.self) { t in
                        Label(t, systemImage: "checkmark").font(Theme.small).foregroundStyle(.secondary)
                    }
                }
            }
            .padding(16)
        }
    }
}

// MARK: 2단계

struct TransferReviewStep: View {
    @ObservedObject var model: TransferModel

    var body: some View {
        let c = model.counts
        VStack(spacing: 0) {
            ScopeBar {
                Picker("보기", selection: $model.filter) {
                    Text("전체 \(c.all)").tag("all")
                    Text("정상 \(c.ok)").tag("ok")
                    Text("검토 필요 \(c.warn)").tag("warn")
                    Text("오류 \(c.bad)").tag("bad")
                }
                .pickerStyle(.segmented).labelsHidden().fixedSize()
                Toggle("계좌번호 하이픈·공백 자동 제거", isOn: $model.autoFix).toggleStyle(.checkbox).lineLimit(1).fixedSize()
                Spacer()
                Text("총 입금액").foregroundStyle(.secondary).fixedSize()
                Text("\(UI.won(c.sum))원").fontWeight(.bold).monospacedDigit().foregroundStyle(Theme.accent).fixedSize()
            }
            table
            FooterBar {
                if !model.extractedAt.isEmpty { Text("추출 일시 \(model.extractedAt)") }
                Spacer()
                if !model.selection.isEmpty { Text("선택 \(model.selection.count)건").foregroundStyle(Theme.accent) }
                Text("\(model.visibleRows.count)건 표시")
            }
        }
    }

    private var table: some View {
        Table(model.visibleRows, selection: $model.selection) {
            TableColumn("#") { (r: TRow) in
                VStack(alignment: .leading, spacing: 1) {
                    Text("\((model.rows.firstIndex { $0.id == r.id } ?? 0) + 1)").monospacedDigit().foregroundStyle(.secondary)
                    if let s = model.splitLabels[r.id] { Text(s.components(separatedBy: " · ").first ?? s).font(Theme.font(10.5, .semibold)).foregroundStyle(Theme.warn) }
                }
            }
            .width(40)
            TableColumn("상태") { r in statusCell(model.checks[r.id] ?? TCheck()) }
                .width(min: 110, ideal: 150)
            TableColumn("은행") { r in BankPicker(selection: model.binding(r.id, \.bank), groups: model.rules.bankGroups, codes: model.rules.bankCodes) }
                .width(min: 112, ideal: 128)
            TableColumn("계좌번호") { r in field(model.binding(r.id, \.account), "계좌번호").monospacedDigit() }
                .width(min: 115, ideal: 132)
            TableColumn("예금주(참고)") { r in field(Binding(get: { model.rows.first { $0.id == r.id }?.holder ?? "" },
                                                        set: { v in if let i = model.rows.firstIndex(where: { $0.id == r.id }) { model.rows[i].holder = v } }), "예금주") }
                .width(min: 56, ideal: 68)
            TableColumn("입금액") { r in field(model.amountBinding(r.id), "입금액").multilineTextAlignment(.trailing).monospacedDigit() }
                .width(min: 90, ideal: 104)
            TableColumn("차량번호") { r in field(model.binding(r.id, \.plate), "차량번호") }
                .width(min: 84, ideal: 94)
            TableColumn("출금통장표시") { r in field(model.binding(r.id, \.outMemo), "출금통장표시") }
                .width(min: 80, ideal: 100)
            TableColumn("입금통장표시") { r in field(model.binding(r.id, \.inMemo), "입금통장표시") }
                .width(min: 80, ideal: 100)
            TableColumn("CMS") { r in field(model.binding(r.id, \.cms), "CMS코드") }
                .width(min: 48, ideal: 58)
        }
        .tableStyle(.inset(alternatesRowBackgrounds: true))
        .overlay {
            if model.rows.isEmpty {
                ContentUnavailableView("이체 내역이 없어요", systemImage: "tray", description: Text("1단계에서 추출하거나 행 추가로 직접 넣어요."))
            }
        }
    }

    private func field(_ b: Binding<String>, _ prompt: String) -> some View {
        TextField(prompt, text: b).textFieldStyle(.plain).labelsHidden()
    }

    private func statusCell(_ c: TCheck) -> some View {
        let color = c.state == "bad" ? Theme.bad : c.state == "warn" ? Theme.warn : Theme.ok
        let text = c.state == "bad" ? (c.errors.first ?? "오류") : c.state == "warn" ? (c.reviews.first ?? "검토") : "정상"
        let more = c.errors.count + c.reviews.count - 1
        return StatusDot(text: text + (more > 0 ? " 외 \(more)" : ""), color: color)
            .help((c.errors.map { "오류: \($0)" } + c.reviews.map { "검토: \($0)" }).joined(separator: "\n"))
    }
}

struct TransferReviewInspector: View {
    @ObservedObject var model: TransferModel
    var body: some View {
        InspectorScroll(title: "선택한 건", icon: "doc.text.magnifyingglass", color: Theme.indigo) {
            VStack(alignment: .leading, spacing: 16) {
                VStack(alignment: .leading, spacing: 8) {
                    SectionHeader("원문", icon: "text.quote", color: Theme.info)
                    if let id = model.selection.first, model.selection.count == 1, let r = model.rows.first(where: { $0.id == id }) {
                        let c = model.checks[id] ?? TCheck()
                        let text = r.ctx ?? r.source ?? ""
                        Text(text.isEmpty ? "(원문 없음)" : text)
                            .font(Theme.font(12.5)).textSelection(.enabled)
                            .frame(maxWidth: .infinity, alignment: .leading)
                        if !c.errors.isEmpty || !c.reviews.isEmpty || model.splitLabels[id] != nil {
                            Divider()
                            SectionHeader("확인 사항", icon: "exclamationmark.triangle.fill", color: Theme.warn)
                            ForEach(c.errors, id: \.self) { Label($0, systemImage: "xmark.octagon.fill").foregroundStyle(Theme.bad) }
                            ForEach(c.reviews, id: \.self) { Label($0, systemImage: "exclamationmark.triangle.fill").foregroundStyle(Theme.warn) }
                            if let s = model.splitLabels[id] { Label(s, systemImage: "scissors").foregroundStyle(Theme.warn) }
                        }
                        Text("은행·계좌번호·금액·차량번호를 원문과 대조해 주세요.").font(Theme.small).foregroundStyle(.secondary)
                    } else {
                        Text(model.selection.count > 1 ? "\(model.selection.count)건을 골랐어요. 원문은 한 줄을 고르면 보여요." : "표에서 한 줄을 고르면 원문과 확인할 내용을 보여줘요.")
                            .font(Theme.small).foregroundStyle(.secondary)
                    }
                }
                Divider()
                VStack(alignment: .leading, spacing: 8) {
                    SectionHeader("통장 메모·CMS 일괄 입력", icon: "square.stack.3d.up.fill", color: Theme.teal)
                    bulk("출금통장표시", $model.outMemoAll) { model.applyMemo(\.outMemo, model.outMemoAll, title: "출금통장표시") }
                    bulk("입금통장표시", $model.inMemoAll) { model.applyMemo(\.inMemo, model.inMemoAll, title: "입금통장표시") }
                    bulk("CMS코드", $model.cmsAll) { model.applyMemo(\.cms, model.cmsAll, title: "CMS코드") }
                    Toggle("빈 칸에만 적용", isOn: $model.onlyEmpty).toggleStyle(.checkbox)
                }
            }
            .padding(16)
        }
    }

    private func bulk(_ title: String, _ value: Binding<String>, apply: @escaping () -> Void) -> some View {
        HStack {
            TextField(title, text: value).textFieldStyle(.roundedBorder)
            Button("적용", action: apply)
        }
    }
}

/// 은행 선택 (하나은행 코드표 묶음)
struct BankPicker: View {
    @Binding var selection: String
    let groups: [Rules.BankGroup]
    let codes: [String: String]
    var body: some View {
        Picker("은행", selection: $selection) {
            Text("은행 선택").tag("")
            if !selection.isEmpty && !groups.contains(where: { $0.banks.contains(selection) }) {
                Text("\(selection) ?").tag(selection)
            }
            ForEach(groups) { g in
                Section(g.id) {
                    ForEach(g.banks, id: \.self) { b in Text("\(b) (\(codes[b] ?? ""))").tag(b) }
                }
            }
        }
        .labelsHidden()
    }
}

// MARK: 3단계

struct TransferDownloadStep: View {
    @ObservedObject var model: TransferModel

    var body: some View {
        let c = model.counts
        let rows = model.fileRows
        VStack(spacing: 0) {
            ScopeBar {
                Text("미리보기 · 은행 업로드 파일에 들어가는 값이에요").foregroundStyle(.secondary)
                Spacer()
                StatusDot(text: c.bad > 0 ? "오류 \(c.bad)건" : c.warn > 0 ? "검토 필요 \(c.warn)건" : "오류 없음",
                          color: c.bad > 0 ? Theme.bad : c.warn > 0 ? Theme.warn : Theme.ok)
                Text("\(c.all)건 · ").foregroundStyle(.secondary) + Text("\(UI.won(c.sum))원").fontWeight(.semibold)
            }
            Table(Array(rows.dropFirst().enumerated()).map { PreviewRow(index: $0.offset, values: $0.element) }) {
                TableColumn("#") { (p: PreviewRow) in
                    VStack(alignment: .leading, spacing: 0) {
                        Text("\(p.index + 1)").monospacedDigit().foregroundStyle(.secondary)
                        if p.index < model.rows.count, let s = model.splitLabels[model.rows[p.index].id] {
                            Text(s.components(separatedBy: " · ").first ?? s).font(Theme.font(10.5, .semibold)).foregroundStyle(Theme.warn)
                        }
                    }
                }.width(40)
                TableColumn("입금은행") { p in Text(p.text(0)) }.width(min: 70, ideal: 90)
                TableColumn("입금계좌번호") { p in Text(p.text(1)).monospacedDigit() }.width(min: 130, ideal: 150)
                TableColumn("입금액(원)") { p in Text(p.money(2)).monospacedDigit().frame(maxWidth: .infinity, alignment: .trailing) }.width(min: 100, ideal: 120)
                TableColumn("출금통장표시") { p in Text(p.text(3)) }
                TableColumn("입금통장표시") { p in Text(p.text(4)) }
                TableColumn("CMS코드") { p in Text(p.text(5)) }.width(min: 50, ideal: 70)
            }
            .tableStyle(.inset(alternatesRowBackgrounds: true))
            FooterBar {
                Text("은행에 올리기 전에 금액과 계좌를 원문과 꼭 대조해 주세요. 이 앱은 송금을 실행하지 않아요.").lineLimit(1)
                Spacer()
            }
        }
    }
}

struct TransferDownloadInspector: View {
    @ObservedObject var model: TransferModel
    var body: some View {
        let c = model.counts
        let comments = model.comments
        InspectorScroll(title: "받기", icon: "arrow.down.circle", color: Theme.accent) {
            VStack(alignment: .leading, spacing: 16) {
                VStack(alignment: .leading, spacing: 8) {
                    SectionHeader("이체파일 받기", icon: "arrow.down.doc.fill", color: Theme.accent)
                    if c.bad > 0 {
                        Label("오류 \(c.bad)건을 2단계에서 고쳐야 받을 수 있어요.", systemImage: "xmark.octagon.fill").foregroundStyle(Theme.bad)
                    } else if c.warn > 0 {
                        Toggle(isOn: Binding(get: { model.ack }, set: { model.setAck($0) })) {
                            Text("검토 필요 \(c.warn)건을 원문과 확인했어요")
                        }.toggleStyle(.checkbox)
                    }
                    Button { model.download() } label: {
                        Label("하나은행 이체파일 다운로드", systemImage: "square.and.arrow.down").fontWeight(.semibold).frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent).controlSize(.large).tint(Theme.accent)
                    .disabled(!model.canDownload)
                    if !model.hasUnsaved && !model.rows.isEmpty {
                        Label("지금 내용으로 받았어요", systemImage: "checkmark.circle.fill").foregroundStyle(Theme.ok).font(Theme.small)
                    }
                }
                Divider()
                VStack(alignment: .leading, spacing: 8) {
                    SectionHeader("송금 코멘트", icon: "text.bubble.fill", color: Theme.purple, trailing: AnyView(Button("모두 복사") { model.copyComments(comments) }.controlSize(.small).disabled(comments.isEmpty)))
                    if comments.isEmpty { Text("이체 내역이 없어요.").foregroundStyle(.secondary) }
                    ForEach(Array(comments.enumerated()), id: \.offset) { i, line in
                        if i > 0 { Divider() }
                        HStack(alignment: .top) {
                            Text(line).font(Theme.font(12.5)).textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
                            Spacer()
                            Button { model.copyComments([line]) } label: { Image(systemName: "doc.on.doc") }.buttonStyle(.borderless).help("복사")
                        }
                    }
                }
            }
            .padding(16)
        }
    }
}

struct PreviewRow: Identifiable {
    let index: Int
    let values: [Any]
    var id: Int { index }
    func text(_ i: Int) -> String { let s = i < values.count ? "\(values[i])" : ""; return s.isEmpty ? "—" : s }
    func money(_ i: Int) -> String { (i < values.count ? values[i] as? NSNumber : nil).map { UI.won($0.doubleValue) } ?? text(i) }
}

// MARK: 차량번호 없는 건

struct AskPlatesSheet: View {
    @ObservedObject var model: TransferModel
    let rows: [TRow]
    let warnings: [String]
    @State private var usePlate: [String: Bool] = [:]
    @State private var texts: [String: String] = [:]
    @State private var error = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("차량번호가 입력되지 않은 건이 있어요").font(Theme.font(15, .semibold))
            Text("차량번호를 찾지 못한 건이 \(rows.count)건 있어요. 건마다 차량번호나 입금통장 이체 메모 중 하나를 입력하고 계속하세요. 비워 두면 ‘검토’ 상태로 남아요.")
                .font(Theme.small).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            if !warnings.isEmpty { Text(warnings.joined(separator: "\n")).font(Theme.small).foregroundStyle(Theme.warn) }
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    ForEach(rows) { r in
                        VStack(alignment: .leading, spacing: 6) {
                            Text("\(r.bank.isEmpty ? "은행 미확인" : r.bank) \(r.account) · \(Double(r.amount).map { UI.won($0) + "원" } ?? "금액 미확인")").fontWeight(.semibold)
                            Text(r.source?.isEmpty == false ? r.source! : "(원문 없음)").font(Theme.small).foregroundStyle(.secondary).lineLimit(4)
                            Picker("", selection: Binding(get: { usePlate[r.id] ?? true }, set: { usePlate[r.id] = $0 })) {
                                Text("차량번호 입력").tag(true)
                                Text("입금통장 이체 메모 입력").tag(false)
                            }.pickerStyle(.segmented).labelsHidden().frame(width: 320)
                            TextField((usePlate[r.id] ?? true) ? "예: 123가4567" : "예: 오픈마일_운송비", text: Binding(get: { texts[r.id] ?? "" }, set: { texts[r.id] = $0 }))
                                .textFieldStyle(.roundedBorder)
                        }
                        .padding(10)
                        .background(RoundedRectangle(cornerRadius: 8).fill(Color.primary.opacity(0.04)))
                    }
                }
            }
            .frame(maxHeight: 420)
            if !error.isEmpty { Text(error).foregroundStyle(Theme.bad).font(Theme.small) }
            HStack {
                Spacer()
                Button("나중에 표에서 입력") { model.askPlates = [] }.keyboardShortcut(.cancelAction)
                Button("확인하고 계속") {
                    if let bad = model.applyPlates(rows.map { (id: $0.id, usePlate: usePlate[$0.id] ?? true, text: texts[$0.id] ?? "") }) {
                        error = "차량번호 형식을 확인해 주세요: \(bad)"
                    }
                }
                .keyboardShortcut(.defaultAction).tint(Theme.accent)
            }
        }
        .padding(20)
        .frame(width: 560)
        .font(Theme.body)
    }
}
